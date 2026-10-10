// Cursor painting + UI invalidation. The cursor is drawn as an inverse
// block or bar quad at its glide position (see gui_anim), with the cell's
// glyph redrawn on top in the background color so it stays legible inside
// the block. invalidate() resets all paint state -- the next render snaps
// instead of sliding stale animation offsets.
//
// The composing (IME) text is painted here as well, since it lives on the
// caret: the buffer holds nothing until the composition commits, so this is
// the only place it can appear.
#include "gui/gui.h"
#include "gui/gui_fit.h"
#include "ui/xterm_palette.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

void UIGui::paint_cursor()
{
  if (cursor_hidden || !cursor_blink_visible)
  {
    return;
  }
  if (cursor_x < 0 || cursor_x >= width || cursor_y < 0 || cursor_y >= height)
  {
    return;
  }
  const UICell &cell = grid[cursor_y][cursor_x];
  // The caret keeps the theme's cursor colours instead of borrowing the cell's
  // syntax colour: a caret painted in a comment's grey, a keyword's blue, or
  // black over a cyan selection is exactly what made it hard to find. Whichever
  // member of the pair contrasts most with the cell's own background becomes the
  // body; the other stays the ink for the glyph inside a block.
  const int cell_bg = cell.reverse ? cell.fg : cell.bg;
  const jot_ui::CursorColors cc = jot_ui::cursor_colors(cursor_fg, cursor_bg, cell_bg);
  float fr, fg_, fb;
  xterm_rgb(cc.fill, fr, fg_, fb);
  float ir, ig, ib;
  xterm_rgb(cc.ink, ir, ig, ib);
  // Cursor is drawn in the cursor colour (inverse video); with a block cursor
  // the glyph is re-drawn on top in the ink colour. The glide position eases
  // toward the cursor cell (+ pane scroll offset).
  float x0 = 0.0f, y0 = 0.0f;
  cursor_origin_px(x0, y0);

  begin_batch(white_tex_);
  if (cursor_shape == UICursorShape::Block)
  {
    push_quad(x0, y0, x0 + cell_w_, y0 + cell_h_, 0, 0, 1, 1, fr, fg_, fb, 1.0f);
  }
  else
  {
    // Proportional bar, never a hairline: 2 device px was invisible at the
    // cell sizes a HiDPI display or a large font produce.
    const float bar_w = std::max(2.0f, std::round(cell_w_ / 6.0f));
    push_quad(x0, y0, x0 + bar_w, y0 + cell_h_, 0, 0, 1, 1, fr, fg_, fb, 1.0f);
  }
  end_batch();

  // Redraw the cursor cell's glyph in the ink color so it stays
  // legible inside the block.
  if (cursor_shape == UICursorShape::Block && !cell.ch.empty() && cell.ch != " ")
  {
    begin_batch(atlas_tex_);
    size_t i = 0;
    uint32_t cp = decode_utf8(cell.ch.c_str(), cell.ch.size(), i);
    if (cp != 0)
    {
      int style = kStyleRegular;
      if (cell.bold)
      {
        style |= kStyleBold;
      }
      if (cell.italic)
      {
        style |= kStyleItalic;
      }
      if (ensure_glyph(cp, style))
      {
        const GuiGlyph &g = glyphs_[((uint64_t)cp << 2) | (uint64_t)(style & 3)];
        float gx = x0 + g.bearing_x;
        float gy = y0 + ascent_ - g.bearing_top;
        float gw = (g.u1 - g.u0) * kAtlasW;
        float gh = (g.v1 - g.v0) * kAtlasH;
        push_quad(gx, gy, gx + gw, gy + gh, g.u0, g.v0, g.u1, g.v1, ir, ig, ib, 1.0f);
      }
    }
    end_batch();
  }
}

// The caret's top-left corner in device pixels: the glide position while the
// glide is live, the cell's own corner before the first one (and after
// invalidate(), which drops it).
void UIGui::cursor_origin_px(float &x0, float &y0) const
{
  x0 = (float)cursor_x * cell_w_;
  y0 = (float)cursor_y * cell_h_;
  if (cursor_px_ >= 0.0f)
  {
    x0 = cursor_px_;
    y0 = cursor_py_;
  }
}

// The composing text as cells: one per codepoint, underlined so it reads as
// not-yet-committed. Invalid UTF-8 ends the run (decode_utf8 reports 0), which
// is also where the painter stops -- a half-written sequence has no glyph.
void preedit_cells(const std::string &text, int fg, int bg, std::vector<UICell> &out)
{
  out.clear();
  for (size_t i = 0; i < text.size();)
  {
    const size_t start = i;
    if (decode_utf8(text.c_str(), text.size(), i) == 0)
    {
      break;
    }
    UICell cell;
    cell.ch = text.substr(start, i - start);
    cell.fg = fg;
    cell.bg = bg;
    cell.underline = 1;
    out.push_back(std::move(cell));
  }
}

// Composing (IME) text, painted inline over the content pass: the composition
// starts on the caret cell, which is where the text it commits to lands, and
// the caret block paints over its first cell afterwards. Nothing here touches
// the grid -- the composition is not part of the buffer -- so the next frame
// that repaints the band (any key, scroll or resize) takes it back.
void UIGui::paint_preedit()
{
  if (preedit_text_.empty() || cursor_hidden)
  {
    return;
  }
  if (cursor_x < 0 || cursor_y < 0 || cursor_x >= width || cursor_y >= height)
  {
    return;
  }
  std::vector<UICell> cells;
  preedit_cells(preedit_text_, default_fg, default_bg, cells);
  // Only the columns the row has room for: a composition longer than the rest
  // of the line would otherwise paint over the furniture to the right of the
  // pane. Clipping (rather than wrapping) is what a one-line composition does
  // in a text field, and the input method's own candidate window shows it all.
  const int cols = std::min((int)cells.size(), width - cursor_x);
  if (cols <= 0)
  {
    return;
  }
  float x0 = 0.0f, y0 = 0.0f;
  cursor_origin_px(x0, y0);
  size_t quads = 0;
  begin_batch(white_tex_);
  paint_row_bg(cells, 0, cols, x0, y0, 0.0f, quads);
  if (quads)
  {
    end_batch();
  }
  begin_batch(atlas_tex_);
  paint_row_glyphs(cells, 0, cols, x0, y0, 0.0f);
  end_batch();
  bool any = false;
  begin_batch(white_tex_);
  paint_row_underlines(cells, 0, cols, x0, y0, 0.0f, any);
  if (any)
  {
    end_batch();
  }
}

// Keeps the input method's candidate window anchored to the caret. SDL takes
// points (the window's coordinate space) while the caret is in device pixels,
// and the X11 backend turns each call into an XSetICValues round trip, so an
// unmoved caret re-issues nothing.
void UIGui::note_text_input_rect()
{
  if (cursor_x < 0 || cursor_y < 0 || cursor_x >= width || cursor_y >= height)
  {
    return;
  }
  float x0 = 0.0f, y0 = 0.0f;
  cursor_origin_px(x0, y0);
  const float scale = scale_ > 0.0f ? scale_ : 1.0f;
  const int x = (int)(x0 / scale);
  const int y = (int)(y0 / scale);
  if (x == text_rect_x_ && y == text_rect_y_)
  {
    return;
  }
  text_rect_x_ = x;
  text_rect_y_ = y;
  SDL_Rect rect;
  rect.x = x;
  rect.y = y;
  rect.w = (int)std::max(1.0f, jot_gui::point_cell_size(cell_w_, scale));
  rect.h = (int)std::max(1.0f, jot_gui::point_cell_size(cell_h_, scale));
  SDL_SetTextInputRect(&rect);
}

void UIGui::invalidate()
{
  cursor_x = -1;
  cursor_y = -1;
  cursor_shape = UICursorShape::Block;
  cursor_hidden = true;
  cursor_dirty = true;
  mark_all_rows_dirty();
  // A resize (or theme change) invalidates the pixel math of in-flight
  // animations; the editor re-reports pane viewports next frame, so the
  // content snaps to the new geometry instead of sliding stale offsets.
  scroll_anims_.clear();
  cursor_px_ = -1.0f;
  cursor_py_ = -1.0f;
  cursor_target_x_ = -1.0f;
  cursor_target_y_ = -1.0f;
  last_frame_ticks_ = 0;
}

// The cursor is painted inside render(); there are no terminal escapes to
// emit. Keeping cursor_dirty set would make render_frame() repaint every
// idle frame for no reason, so clear it like the base implementation does
// after writing.
void UIGui::flush_cursor()
{
  cursor_dirty = false;
}