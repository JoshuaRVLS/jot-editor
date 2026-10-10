// Scroll + cursor animation (neovide-style). The editor reports each pane's
// body region and its net scroll delta every render; the delta moves the
// strip's target and the display eases toward it on the critically damped
// curve in gui_scroll_math.h, drawn shifted by the render pass (gui_render).
// The cursor glides toward its cell (plus any pane offset covering it),
// snapping on teleports. needs_repaint keeps the editor pump repainting while
// anything is in flight, so the animation advances at the monitor's refresh.
#include "gui/gui.h"
#include "gui/gui_fit.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>

void UIGui::notify_pane_scroll(int pane_id, int x, int y, int w, int h, int delta_rows, bool jumped_x)
{
  GuiScrollAnim &anim = scroll_anims_[pane_id];
  const int old_w = anim.x2 - anim.x1;
  const int old_h = anim.y2 - anim.y1;
  anim.x1 = x;
  anim.y1 = y;
  anim.x2 = x + std::max(0, w);
  anim.y2 = y + std::max(0, h);
  // Refreshed every frame (the editor reports every pane every render), so it
  // describes this frame only -- and it must be set before the delta_rows == 0
  // early return, since a horizontal jump arrives with no vertical delta.
  anim.jumped_x = jumped_x;
  // A different pane now owns this slot (split closed, buffers swapped): the
  // retained frames belong to the old occupant and must not slide out.
  if (old_w != anim.x2 - anim.x1 || old_h != anim.y2 - anim.y1)
  {
    anim.frames.clear();
    anim.slide = jot_gui::SlideState{};
    anim.total_px = 0.0f;
  }
  if (delta_rows == 0)
  {
    return;
  }
  const float dpx = (float)delta_rows * cell_h_;
  const int body_rows = std::max(1, anim.y2 - anim.y1);
  // A step bigger than one pane is a jump, not a scroll: snap instead of
  // sliding content clear across the window (buffer switches, fold flips).
  const float max_slide = (float)body_rows * cell_h_;
  if (anim.frames.empty() || std::abs(dpx) > max_slide)
  {
    // Nothing retained to slide from (the pane just appeared) or a jump: the
    // capture at the end of this render seeds the next chain.
    anim.slide = jot_gui::SlideState{};
    anim.total_px = 0.0f;
    anim.frames.clear();
    return;
  }
  // The editor reports the net delta since the last rendered frame; it moves
  // only the target. The glide carries on from the position and velocity it
  // has, so a burst extends one glide and a reversal decelerates through zero.
  anim.total_px += dpx;
}

bool UIGui::needs_repaint() const
{
  for (const auto &kv : scroll_anims_)
  {
    if (!jot_gui::slide_settled(kv.second.slide, kv.second.total_px))
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
  for (auto &kv : scroll_anims_)
  {
    GuiScrollAnim &a = kv.second;
    if (jot_gui::slide_settled(a.slide, a.total_px))
    {
      continue;
    }
    a.slide = jot_gui::slide_step(a.slide, a.total_px, dt, jot_gui::kSlideOmega);
    if (jot_gui::slide_settled(a.slide, a.total_px))
    {
      // Settled on the target: the live grid alone describes the screen, so
      // the retained viewports go and the chain restarts from zero.
      a.slide = jot_gui::SlideState{};
      a.total_px = 0.0f;
      a.frames.clear();
    }
  }
}

UIGui::CursorPaneView UIGui::cursor_pane_view(int x, int y) const
{
  CursorPaneView view;
  for (const auto &kv : scroll_anims_)
  {
    const GuiScrollAnim &a = kv.second;
    const bool animating = !jot_gui::slide_settled(a.slide, a.total_px);
    if (!animating && !a.jumped_x)
    {
      continue;
    }
    if (x >= a.x1 && x < a.x2 && y >= a.y1 && y < a.y2)
    {
      view.offset_px = a.total_px - a.slide.pos_px;
      view.jumped_x = a.jumped_x;
      return view;
    }
  }
  return view;
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
  const CursorPaneView pane = cursor_pane_view(cursor_x, cursor_y);
  const float tx = cursor_x * cell_w_;
  const float ty = cursor_y * cell_h_ + pane.offset_px;
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
  // While the pane slides, the caret rides that same offset instead of easing
  // toward its cell: a second curve left it visibly trailing its own line,
  // which read as the caret being dragged along by the scroll. A horizontal
  // window jump has no slide to ride, so that axis snaps.
  const bool snap = pane.offset_px != 0.0f || pane.jumped_x;
  cursor_px_ = jot_gui::glide_axis(cursor_px_, tx, dt, snap);
  cursor_py_ = jot_gui::glide_axis(cursor_py_, ty, dt, snap);
}