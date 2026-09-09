// Cursor painting + UI invalidation. The cursor is drawn as an inverse
// block or bar quad at its glide position (see gui_anim), with the cell's
// glyph redrawn on top in the background color so it stays legible inside
// the block. invalidate() resets all paint state -- the next render snaps
// instead of sliding stale animation offsets.
#include "gui/gui.h"

// Mesa's gl.h only declares core 2.0+ entry points under this macro.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

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
  float fr, fg_, fb, br, bg_, bb;
  cell_colors(cell, fr, fg_, fb, br, bg_, bb);
  // Cursor is drawn in the cell's foreground color (inverse video); with a
  // block cursor the glyph is re-drawn on top in the background color. The
  // glide position eases toward the cursor cell (+ pane scroll offset).
  float x0 = cursor_x * cell_w_;
  float y0 = cursor_y * cell_h_;
  if (cursor_px_ >= 0.0f)
  {
    x0 = cursor_px_;
    y0 = cursor_py_;
  }

  begin_batch();
  if (cursor_shape == UICursorShape::Block)
  {
    push_quad(x0, y0, x0 + cell_w_, y0 + cell_h_, 0, 0, 1, 1, fr, fg_, fb, 1.0f);
  }
  else
  {
    push_quad(x0, y0, x0 + 2.0f, y0 + cell_h_, 0, 0, 1, 1, fr, fg_, fb, 1.0f);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, white_tex_);
  glUniform1i(glGetUniformLocation(program_, "u_tex"), 0);
  end_batch();

  // Redraw the cursor cell's glyph in the background color so it stays
  // legible inside the block.
  if (cursor_shape == UICursorShape::Block && !cell.ch.empty() && cell.ch != " ")
  {
    begin_batch();
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
        push_quad(gx, gy, gx + gw, gy + gh, g.u0, g.v0, g.u1, g.v1, br, bg_, bb, 1.0f);
      }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);
    glUniform1i(glGetUniformLocation(program_, "u_tex"), 0);
    end_batch();
  }
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