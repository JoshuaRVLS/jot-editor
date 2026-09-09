// Frame painting: walks the cell grid and emits batched GL passes -- opaque
// background quads (coalesced into horizontal runs), glyph quads from the
// FreeType atlas, and underline quads. Animated panes draw as two
// translated sprites (the retained previous frame sliding out, the current
// frame settling in) so both pane edges always have content; every static
// row paints on top afterward so borders, tab strips and neighbouring
// panes win where sliding content crossed them.
#include "gui/gui.h"

#include <SDL2/SDL.h>
// Mesa's gl.h only declares core 2.0+ entry points under this macro.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#include <algorithm>
#include <vector>

void UIGui::render()
{
  glViewport(0, 0, std::max(1, pixel_w_), std::max(1, pixel_h_));
  float cr = 0, cg = 0, cb = 0;
  xterm_rgb(default_bg, cr, cg, cb);
  glClearColor(cr, cg, cb, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(program_);
  // Quad coordinates are in points (the grid's cell size); the viewport is
  // in pixels. Normalizing with the point size makes the content scale by
  // the drawable/point ratio (HiDPI) instead of rendering tiny on high-DPI
  // displays.
  int point_w = 0, point_h = 0;
  SDL_GetWindowSize(window_, &point_w, &point_h);
  glUniform2f(glGetUniformLocation(program_, "u_scale"),
              2.0f / std::max(1, point_w), -2.0f / std::max(1, point_h));
  glBindVertexArray(vao_);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const float dt = frame_dt();
  advance_animations(dt);
  advance_cursor_glide(dt);

  // Draw order matters: each animating pane paints every retained frame
  // translated to its position in the sliding strip (viewport tops span
  // the whole slide, so both edges always have content -- including quick
  // direction reversals), then every static row paints on top of any
  // sprite content that slid across it (tab strips, borders, neighbouring
  // panes).
  for (auto &kv : scroll_anims_)
  {
    GuiScrollAnim &a = kv.second;
    if (std::abs(a.offset_px) < 0.25f)
    {
      continue;
    }
    const int body_rows = std::max(1, a.y2 - a.y1);
    // Display position of the slide, in rows from the chain start.
    const float s = (a.total_px - a.offset_px) / cell_h_;
    const int grid_top = (int)std::lround(a.total_px / cell_h_);
    for (const GuiFrame &f : a.frames)
    {
      // The live grid paints the current viewport (top == grid_top); skip
      // its retained twin and any frame fully off the pane.
      if (f.top_row == grid_top)
      {
        continue;
      }
      if ((float)(f.top_row + body_rows) <= s || (float)f.top_row >= s + (float)body_rows)
      {
        continue;
      }
      paint_sprite(a, &f.rows, (float)(f.top_row - s) * cell_h_);
    }
    paint_sprite(a, nullptr, a.offset_px);
  }
  paint_plain();
  paint_cursor();

  // Keep a copy of this frame's pane bodies so the next scroll can slide
  // them out instead of leaving the revealed edges empty.
  capture_pane_rows();

  glBindVertexArray(0);
  SDL_GL_SwapWindow(window_);
}

// Pushes the background quads of one row slice: source columns [col0,col1)
// of `src`, whose top edge sits at `y_top` (pixel) translated by `dy`, with
// source column 0 landing at pixel x `x0px`. Background runs coalesce within
// the slice, so border columns around a pane never get caught up in a slide.
void UIGui::paint_row_bg(const std::vector<UICell> &src,
                         int col0,
                         int col1,
                         float x0px,
                         float y_top,
                         float dy,
                         size_t &quads)
{
  col0 = std::max(0, col0);
  col1 = std::min(col1, (int)src.size());
  if (col0 >= col1)
  {
    return;
  }
  const float py = y_top + dy;
  int c = col0;
  while (c < col1)
  {
    float fr, fg_, fb, br, bg_, bb;
    cell_colors(src[(size_t)c], fr, fg_, fb, br, bg_, bb);
    int run_end = c + 1;
    while (run_end < col1)
    {
      float nfr, nfg, nfb, nbr, nbg, nbb;
      cell_colors(src[(size_t)run_end], nfr, nfg, nfb, nbr, nbg, nbb);
      if (nbr != br || nbg != bg_ || nbb != bb)
      {
        break;
      }
      run_end++;
    }
    push_quad(x0px + (float)c * cell_w_, py, x0px + (float)run_end * cell_w_, py + cell_h_, 0, 0,
              1, 1, br, bg_, bb, 1.0f);
    quads++;
    c = run_end;
  }
}

// Pushes the glyph quads of one row slice (same geometry as paint_row_bg).
void UIGui::paint_row_glyphs(const std::vector<UICell> &src,
                             int col0,
                             int col1,
                             float x0px,
                             float y_top,
                             float dy)
{
  col0 = std::max(0, col0);
  col1 = std::min(col1, (int)src.size());
  const float py = y_top + dy;
  for (int c = col0; c < col1; c++)
  {
    const UICell &cell = src[(size_t)c];
    if (cell.ch.empty() || cell.ch == " ")
    {
      continue;
    }
    size_t i = 0;
    uint32_t cp = decode_utf8(cell.ch.c_str(), cell.ch.size(), i);
    if (cp == 0)
    {
      continue;
    }
    int style = kStyleRegular;
    if (cell.bold)
    {
      style |= kStyleBold;
    }
    if (cell.italic)
    {
      style |= kStyleItalic;
    }
    if (!ensure_glyph(cp, style))
    {
      clear_atlas();
      if (!ensure_glyph(cp, style))
      {
        continue;
      }
    }
    const GuiGlyph &g = glyphs_[((uint64_t)cp << 2) | (uint64_t)(style & 3)];
    float fr, fg_, fb, br, bg_, bb;
    cell_colors(cell, fr, fg_, fb, br, bg_, bb);
    // Pen: cell origin + baseline, then glyph bearing (y down).
    const float gx = x0px + (float)c * cell_w_ + g.bearing_x;
    const float gy = py + ascent_ - g.bearing_top;
    const float gw = (g.u1 - g.u0) * kAtlasW;
    const float gh = (g.v1 - g.v0) * kAtlasH;
    push_quad(gx, gy, gx + gw, gy + gh, g.u0, g.v0, g.u1, g.v1, fr, fg_, fb, 1.0f);
  }
}

// Pushes the underline quads of one row slice (wavy or straight).
void UIGui::paint_row_underlines(const std::vector<UICell> &src,
                                 int col0,
                                 int col1,
                                 float x0px,
                                 float y_top,
                                 float dy,
                                 bool &any)
{
  col0 = std::max(0, col0);
  col1 = std::min(col1, (int)src.size());
  const float py = y_top + dy;
  for (int c = col0; c < col1; c++)
  {
    const UICell &cell = src[(size_t)c];
    if (cell.underline == 0)
    {
      continue;
    }
    any = true;
    float fr, fg_, fb, br, bg_, bb;
    cell_colors(cell, fr, fg_, fb, br, bg_, bb);
    float ur = fr, ug = fg_, ub = fb;
    if (cell.underline_fg >= 0)
    {
      xterm_rgb(cell.underline_fg, ur, ug, ub);
    }
    const float x0 = x0px + (float)c * cell_w_;
    const float y0 = py + cell_h_ - 2.0f;
    if (cell.underline == 2)
    {
      // Wavy: three short segments zigzagging one pixel.
      const float seg = cell_w_ / 3.0f;
      for (int s = 0; s < 3; s++)
      {
        const float sy = y0 - (s % 2 == 0 ? 0.0f : 1.0f);
        push_quad(x0 + s * seg, sy, x0 + (s + 1) * seg, sy + 1.0f, 0, 0, 1, 1, ur, ug, ub, 1.0f);
      }
    }
    else
    {
      push_quad(x0, y0, x0 + cell_w_, y0 + 1.0f, 0, 0, 1, 1, ur, ug, ub, 1.0f);
    }
  }
}

// Paints one translated copy of a pane body at vertical offset `dy` (px):
// the retained rows of a GuiFrame (`rows` != nullptr, pane-relative) or the
// live grid (`rows` == nullptr, absolute columns). Three batches (bg,
// glyphs, underlines) per sprite.
void UIGui::paint_sprite(const GuiScrollAnim &a, const std::vector<std::vector<UICell>> *rows,
                         float dy)
{
  const int n = std::min(a.y2 - a.y1, std::max(0, height - a.y1));
  if (n <= 0)
  {
    return;
  }
  if (rows && (int)rows->size() < n)
  {
    return;
  }
  const int want_w = a.x2 - a.x1;
  if (want_w <= 0)
  {
    return;
  }
  // Column base: retained rows are pane-relative (their column 0 is grid
  // column a.x1), so they land at a.x1*cell_w_. The live grid rows are
  // absolute (their column c is grid column c), so they land at 0. Using
  // the pane base for both would paint the grid shifted right by a.x1
  // cells -- a ghost copy beside the real content.
  const float x0px = rows ? (float)a.x1 * cell_w_ : 0.0f;

  // Backgrounds.
  size_t quads = 0;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : grid[(size_t)(a.y1 + r)];
    paint_row_bg(row, rows ? 0 : a.x1, rows ? want_w : a.x2, x0px, (float)(a.y1 + r) * cell_h_, dy,
                 quads);
  }
  if (quads)
  {
    flush_tex(program_, white_tex_);
    end_batch();
  }

  // Glyphs.
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : grid[(size_t)(a.y1 + r)];
    paint_row_glyphs(row, rows ? 0 : a.x1, rows ? want_w : a.x2, x0px, (float)(a.y1 + r) * cell_h_,
                     dy);
  }
  flush_tex(program_, atlas_tex_);
  end_batch();

  // Underlines.
  bool any = false;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : grid[(size_t)(a.y1 + r)];
    paint_row_underlines(row,
                         rows ? 0 : a.x1,
                         rows ? want_w : a.x2,
                         x0px,
                         (float)(a.y1 + r) * cell_h_,
                         dy,
                         any);
  }
  if (any)
  {
    flush_tex(program_, white_tex_);
    end_batch();
  }
}

// Paints every static row (no animation): the whole grid except the body
// columns of animating panes, which the sprites already covered. Runs after
// the sprites so borders, tab strips, status lines and neighbouring panes
// always win where sliding content crossed them.
void UIGui::paint_plain()
{
  size_t quads = 0;
  begin_batch();
  for (int y = 0; y < height; y++)
  {
    int e0 = -1;
    int e1 = -1;
    for (const auto &kv : scroll_anims_)
    {
      const GuiScrollAnim &a = kv.second;
      if (std::abs(a.offset_px) < 0.25f)
      {
        continue;
      }
      if (y >= a.y1 && y < a.y2)
      {
        e0 = a.x1;
        e1 = a.x2;
        break;
      }
    }
    const float y_top = (float)y * cell_h_;
    if (e0 <= 0 && (e1 < 0 || e1 >= width))
    {
      paint_row_bg(grid[(size_t)y], 0, width, 0.0f, y_top, 0.0f, quads);
    }
    else
    {
      if (e0 > 0)
      {
        paint_row_bg(grid[(size_t)y], 0, std::min(e0, width), 0.0f, y_top, 0.0f, quads);
      }
      if (e1 >= 0 && e1 < width)
      {
        paint_row_bg(grid[(size_t)y], std::max(0, e1), width, 0.0f, y_top, 0.0f, quads);
      }
    }
  }
  if (quads)
  {
    flush_tex(program_, white_tex_);
    end_batch();
  }

  begin_batch();
  for (int y = 0; y < height; y++)
  {
    int e0 = -1;
    int e1 = -1;
    for (const auto &kv : scroll_anims_)
    {
      const GuiScrollAnim &a = kv.second;
      if (std::abs(a.offset_px) < 0.25f)
      {
        continue;
      }
      if (y >= a.y1 && y < a.y2)
      {
        e0 = a.x1;
        e1 = a.x2;
        break;
      }
    }
    const float y_top = (float)y * cell_h_;
    if (e0 <= 0 && (e1 < 0 || e1 >= width))
    {
      paint_row_glyphs(grid[(size_t)y], 0, width, 0.0f, y_top, 0.0f);
    }
    else
    {
      if (e0 > 0)
      {
        paint_row_glyphs(grid[(size_t)y], 0, std::min(e0, width), 0.0f, y_top, 0.0f);
      }
      if (e1 >= 0 && e1 < width)
      {
        paint_row_glyphs(grid[(size_t)y], std::max(0, e1), width, 0.0f, y_top, 0.0f);
      }
    }
  }
  flush_tex(program_, atlas_tex_);
  end_batch();

  bool any = false;
  begin_batch();
  for (int y = 0; y < height; y++)
  {
    int e0 = -1;
    int e1 = -1;
    for (const auto &kv : scroll_anims_)
    {
      const GuiScrollAnim &a = kv.second;
      if (std::abs(a.offset_px) < 0.25f)
      {
        continue;
      }
      if (y >= a.y1 && y < a.y2)
      {
        e0 = a.x1;
        e1 = a.x2;
        break;
      }
    }
    const float y_top = (float)y * cell_h_;
    if (e0 <= 0 && (e1 < 0 || e1 >= width))
    {
      paint_row_underlines(grid[(size_t)y], 0, width, 0.0f, y_top, 0.0f, any);
    }
    else
    {
      if (e0 > 0)
      {
        paint_row_underlines(grid[(size_t)y], 0, std::min(e0, width), 0.0f, y_top, 0.0f, any);
      }
      if (e1 >= 0 && e1 < width)
      {
        paint_row_underlines(grid[(size_t)y], std::max(0, e1), width, 0.0f, y_top, 0.0f, any);
      }
    }
  }
  if (any)
  {
    flush_tex(program_, white_tex_);
    end_batch();
  }
}

// Retains this frame's grid as the chain's viewport at `total/cell_h` rows
// from the chain start (replacing any older copy of the same viewport), so
// every viewport a slide visits stays available to the animation -- the
// slide can always draw the strip positions it shows, in both directions.
void UIGui::capture_pane_rows()
{
  for (auto &kv : scroll_anims_)
  {
    GuiScrollAnim &a = kv.second;
    const int w = a.x2 - a.x1;
    const int h = a.y2 - a.y1;
    if (w <= 0 || h <= 0 || a.y1 < 0 || a.y2 > height || a.x1 < 0 || a.x2 > width)
    {
      continue;
    }
    const int top = (int)std::lround(a.total_px / cell_h_);
    GuiFrame *dst = nullptr;
    for (GuiFrame &f : a.frames)
    {
      if (f.top_row == top)
      {
        dst = &f;
        break;
      }
    }
    if (!dst)
    {
      a.frames.push_back(GuiFrame{});
      dst = &a.frames.back();
      dst->top_row = top;
      dst->rows.assign((size_t)h, std::vector<UICell>((size_t)w));
    }
    else if ((int)dst->rows.size() != h || (int)dst->rows[0].size() != w)
    {
      dst->rows.assign((size_t)h, std::vector<UICell>((size_t)w));
    }
    for (int r = 0; r < h; r++)
    {
      const std::vector<UICell> &src = grid[(size_t)(a.y1 + r)];
      std::vector<UICell> &drow = dst->rows[(size_t)r];
      for (int c = 0; c < w; c++)
      {
        drow[(size_t)c] = src[(size_t)(a.x1 + c)];
      }
    }
  }
}