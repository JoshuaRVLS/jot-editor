// Scroll + cursor animation (neovide-style). The editor reports each pane's
// body region and its net scroll delta every render; a pane that scrolled
// starts an easing animation from delta*cell_h toward 0, and the render
// pass draws the pane body shifted by the current offset (see gui_render).
// The cursor glides toward its cell (plus any pane offset covering it)
// with a faster exponential approach, snapping on teleports. needs_repaint
// keeps the editor pump repainting while anything is in flight, so the
// animation advances at the monitor's refresh.
#include "gui/gui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>

void UIGui::notify_pane_scroll(int pane_id, int x, int y, int w, int h, int delta_rows)
{
  GuiScrollAnim &anim = scroll_anims_[pane_id];
  const int old_w = anim.x2 - anim.x1;
  const int old_h = anim.y2 - anim.y1;
  anim.x1 = x;
  anim.y1 = y;
  anim.x2 = x + std::max(0, w);
  anim.y2 = y + std::max(0, h);
  // A different pane now owns this slot (split closed, buffers swapped): the
  // retained frames belong to the old occupant and must not slide out.
  if (old_w != anim.x2 - anim.x1 || old_h != anim.y2 - anim.y1)
  {
    anim.frames.clear();
    anim.offset_px = 0.0f;
    anim.total_px = 0.0f;
  }
  if (delta_rows == 0)
  {
    return;
  }
  // The editor reports the net delta since the last rendered frame. The
  // slide display position is s = (total - offset)/cell_h rows from the
  // chain start; every viewport the chain visits is retained in `frames`,
  // so the painted strip always covers the pane in both directions -- a
  // quick reversal can't strand an edge without content. Deltas that arrive
  // while a slide is in flight ACCUMULATE into total/offset (the editor
  // repaints the grid with the newer viewport anyway), keeping wheel bursts
  // continuous instead of restarting the slide every notch.
  const float dpx = (float)delta_rows * cell_h_;
  const int body_rows = std::max(1, anim.y2 - anim.y1);
  // Up to one full pane of net slide animates (page flips glide); anything
  // bigger (buffer switches, huge jumps) snaps.
  const float max_slide = (float)body_rows * cell_h_;
  if (anim.frames.empty())
  {
    // Pane just appeared (no retained viewport yet): snap; the capture at
    // the end of this render seeds a fresh frame for the next chain.
    anim.offset_px = 0.0f;
    anim.total_px = 0.0f;
  }
  else if (std::abs(anim.offset_px) < 0.25f)
  {
    // Previous slide settled (or never started): fresh chain from the
    // settled viewport.
    anim.total_px = dpx;
    anim.offset_px = dpx;
  }
  else if (std::abs(anim.total_px + dpx) <= max_slide)
  {
    anim.total_px += dpx;
    anim.offset_px += dpx;
  }
  else
  {
    // Jump bigger than the pane: snap instead of sliding content across
    // the whole screen; the next capture re-seeds the retained set.
    anim.offset_px = 0.0f;
    anim.total_px = 0.0f;
    anim.frames.clear();
  }
}

bool UIGui::needs_repaint() const
{
  for (const auto &kv : scroll_anims_)
  {
    if (std::abs(kv.second.offset_px) > 0.25f)
    {
      return true;
    }
  }
  if (cursor_px_ >= 0.0f && cursor_target_x_ >= 0.0f)
  {
    if (std::abs(cursor_px_ - cursor_target_x_) > 0.05f
        || std::abs(cursor_py_ - cursor_target_y_) > 0.05f)
    {
      return true;
    }
  }
  // Float overlays: while a surface's entrance/exit transition, eased
  // position, colors, or the modal scrim are still converging (sidebar
  // slide, toast drift, fade dissolve), keep repainting so the easing
  // advances at the monitor's refresh instead of only on Lua's 50ms ticks.
  if (float_anims_transitioning_ || float_colors_transitioning_ || scrim_transitioning_)
  {
    return true;
  }
  // The RmlUi settings overlay: its transitions/animations advance in
  // update(), which only runs while frames are being painted.
  if (settings_.is_open())
  {
    return true;
  }
  return false;
}

float UIGui::frame_dt()
{
  const uint64_t now = SDL_GetPerformanceCounter();
  const uint64_t freq = SDL_GetPerformanceFrequency();
  float dt = 0.0f;
  if (last_frame_ticks_ != 0)
  {
    dt = (float)((double)(now - last_frame_ticks_) / (double)freq);
  }
  last_frame_ticks_ = now;
  // Clamp against stalls (resize, background hiccups) so animations never
  // jump past their target.
  return std::clamp(dt, 0.0f, 0.05f);
}

void UIGui::advance_animations(float dt)
{
  // Exponential approach: fast start, smooth settle, frame-rate independent.
  const float k = 1.0f - std::exp(-dt / 0.06f);
  for (auto &kv : scroll_anims_)
  {
    GuiScrollAnim &a = kv.second;
    if (a.offset_px == 0.0f)
    {
      continue;
    }
    a.offset_px += -a.offset_px * k;
    if (std::abs(a.offset_px) < 0.1f)
    {
      // Settled: drop the chain's retained viewports (the next render
      // captures the settled grid as the next chain's start) and reset the
      // net distance so the next delta starts a fresh chain.
      a.offset_px = 0.0f;
      a.total_px = 0.0f;
      a.frames.clear();
    }
  }
}

float UIGui::cursor_pane_offset(int x, int y) const
{
  for (const auto &kv : scroll_anims_)
  {
    const GuiScrollAnim &a = kv.second;
    if (std::abs(a.offset_px) < 0.25f)
    {
      continue;
    }
    if (x >= a.x1 && x < a.x2 && y >= a.y1 && y < a.y2)
    {
      return a.offset_px;
    }
  }
  return 0.0f;
}

void UIGui::advance_cursor_glide(float dt)
{
  if (cursor_hidden || cursor_x < 0 || cursor_y < 0 || cursor_x >= width || cursor_y >= height)
  {
    cursor_px_ = -1.0f;
    cursor_py_ = -1.0f;
    cursor_target_x_ = -1.0f;
    cursor_target_y_ = -1.0f;
    return;
  }
  const float soff = cursor_pane_offset(cursor_x, cursor_y);
  const float tx = cursor_x * cell_w_;
  const float ty = cursor_y * cell_h_ + soff;
  cursor_target_x_ = tx;
  cursor_target_y_ = ty;
  if (cursor_px_ < 0.0f)
  {
    cursor_px_ = tx;
    cursor_py_ = ty;
    return;
  }
  // Teleports (mouse click across the window, palette opening) snap instead
  // of streaking across the screen.
  const float dx = tx - cursor_px_;
  const float dy = ty - cursor_py_;
  if (std::sqrt(dx * dx + dy * dy) > 4.0f * std::max(cell_w_, cell_h_))
  {
    cursor_px_ = tx;
    cursor_py_ = ty;
    return;
  }
  const float k = 1.0f - std::exp(-dt / 0.045f);
  cursor_px_ += dx * k;
  cursor_py_ += dy * k;
  if (std::abs(cursor_px_ - tx) < 0.05f && std::abs(cursor_py_ - ty) < 0.05f)
  {
    cursor_px_ = tx;
    cursor_py_ = ty;
  }
}