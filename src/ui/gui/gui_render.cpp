// Frame painting: walks the cell grid and emits batched GL passes -- opaque
// background quads (coalesced into horizontal runs), glyph quads from the
// FreeType atlas, and underline quads. Animated panes draw as two
// translated sprites (the retained previous frame sliding out, the current
// frame settling in) so both pane edges always have content; every static
// row paints on top afterward so borders, tab strips and neighbouring
// panes win where sliding content crossed them.
#include "gui/gui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
  glUniform2f(u_scale_loc_, 2.0f / std::max(1, point_w), -2.0f / std::max(1, point_h));
  glBindVertexArray(vao_);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const float dt = frame_dt();
  advance_animations(dt);
  advance_cursor_glide(dt);
  scrim_transitioning_ = false;

  // Content passes paint from the pre-float snapshot when floats are
  // visible (see before_float_render), so float cells never slide with the
  // scroll; paint_float_overlays draws them as a fixed overlay on top.
  content_grid_ = pre_float_grid_.empty() ? &grid : &pre_float_grid_;

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

  // Native-floated surfaces mid-entrance: their rects are skipped in the
  // static pass below. Those surfaces (sidebar, right dock, home screen)
  // are also painted natively into the grid at their final position, so
  // without the skip the sliding float copy would ghost over the static
  // paint -- the "two explorers" artifact. Standalone floats (toasts,
  // modals) have pane content beneath and must not be blanked.
  //
  // The blanking keys off the ANIMATION state (not the overlay list): a
  // surface can skip an emit frame mid-resize, and on those frames the
  // static paint would flash through. The anim survives transient
  // absences (see the gone_frames grace in paint_float_overlays) and
  // keeps blanking until the entrance truly completes.
  entering_float_rects_.clear();
  const auto is_native_floated_key = [](const std::string &key)
  {
    return key == "s:sidebar" || key == "s:side_panel" || key == "s:home_screen";
  };
  for (const auto &kv : float_anims_)
  {
    const GuiFloatAnim &a = kv.second;
    if (a.exiting || a.enter_t >= 1.0f || !is_native_floated_key(kv.first))
    {
      continue;
    }
    if (a.exit_w <= 0 || a.exit_h <= 0)
    {
      continue;
    }
    entering_float_rects_.push_back(
        {a.exit_x, a.exit_y, a.exit_w, std::min(a.exit_h, std::max(0, height - a.exit_y))});
  }
  // First appearance: the anim is created during paint_float_overlays (after
  // the static pass), so an overlay whose key has no anim yet must blank too
  // or its first frame flashes the native paint through.
  for (const FloatOverlay &ov : float_overlays)
  {
    if (ov.w <= 0 || ov.h <= 0 || ov.x < 0 || ov.y < 0)
    {
      continue;
    }
    const std::string key = float_key(ov.surface, ov.handle);
    if (!is_native_floated_key(key) || float_anims_.count(key))
    {
      continue;
    }
    entering_float_rects_.push_back(
        {ov.x, ov.y, ov.w, std::min(ov.h, std::max(0, height - ov.y))});
  }
  paint_plain();

  // Modal scrim: a full-window black quad whose alpha eases toward
  // kDimAlpha while any modal is up (dim_rect records it during the
  // editor's frame paint). The quad itself draws inside
  // paint_float_overlays, between the background float layer (sidebar,
  // status line, toasts -- dimmed) and the modal surface's own panel
  // (quick pick, settings, ... -- bright on top), matching the terminal's
  // cell-dimming order.
  const float dim_target = dim_active_ ? kDimAlpha : 0.0f;
  if (std::fabs(dim_alpha_ - dim_target) > 0.004f)
  {
    dim_alpha_ += (dim_target - dim_alpha_) * (1.0f - std::exp(-dt / 0.08f));
    if (std::fabs(dim_alpha_ - dim_target) < 0.004f)
    {
      dim_alpha_ = dim_target;
    }
    scrim_transitioning_ = true;
  }
  else
  {
    dim_alpha_ = dim_target;
  }
  dim_active_ = false;

  paint_float_overlays(dt);
  paint_cursor();

  // Keep a copy of this frame's pane bodies so the next scroll can slide
  // them out instead of leaving the revealed edges empty.
  capture_pane_rows();
  content_grid_ = nullptr;

  glBindVertexArray(0);
  SDL_GL_SwapWindow(window_);
}

// Resolves one cell's effective colors (reverse/dim honored) into 9 floats:
// fg.r,g,b, bg.r,g,b, underline.r,g,b (underline defaults to the text
// color when the cell carries no explicit underline_fg).
void UIGui::resolve_cell_rgb(const UICell &cell, float *out)
{
  int fgi = cell.reverse ? cell.bg : cell.fg;
  int bgi = cell.reverse ? cell.fg : cell.bg;
  // A cell carrying an exact 24-bit colour (the inline colour preview) paints
  // that colour verbatim: the palette is only the fallback for every painter
  // that still works in xterm-256 indices.
  if (cell.fg_rgb != kNoRgb)
  {
    out[0] = ((cell.fg_rgb >> 16) & 0xFF) / 255.0f;
    out[1] = ((cell.fg_rgb >> 8) & 0xFF) / 255.0f;
    out[2] = (cell.fg_rgb & 0xFF) / 255.0f;
  }
  else
  {
    xterm_rgb(fgi, out[0], out[1], out[2]);
  }
  if (cell.bg_rgb != kNoRgb)
  {
    out[3] = ((cell.bg_rgb >> 16) & 0xFF) / 255.0f;
    out[4] = ((cell.bg_rgb >> 8) & 0xFF) / 255.0f;
    out[5] = (cell.bg_rgb & 0xFF) / 255.0f;
  }
  else
  {
    xterm_rgb(bgi, out[3], out[4], out[5]);
  }
  if (cell.dim)
  {
    out[0] *= 0.55f;
    out[1] *= 0.55f;
    out[2] *= 0.55f;
  }
  if (cell.underline_fg >= 0)
  {
    xterm_rgb(cell.underline_fg, out[6], out[7], out[8]);
  }
  else
  {
    out[6] = out[0];
    out[7] = out[1];
    out[8] = out[2];
  }
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
                         size_t &quads,
                         float clip_top,
                         float clip_bottom)
{
  col0 = std::max(0, col0);
  col1 = std::min(col1, (int)src.size());
  if (col0 >= col1)
  {
    return;
  }
  const float py = y_top + dy;
  // Vertical clip: sliding sprites must never paint outside their pane
  // rect (e.g. into the status line or its margin), or code text bleeds
  // under the status bar where nothing repaints it.
  const float top = std::max(py, clip_top);
  const float bottom = std::min(py + cell_h_, clip_bottom);
  if (top >= bottom)
  {
    return;
  }
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
    push_quad(x0px + (float)c * cell_w_, top, x0px + (float)run_end * cell_w_, bottom, 0, 0,
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
                             float dy,
                             float clip_top,
                             float clip_bottom)
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
    // Clip the glyph quad to the pane rect and remap the texture V so a
    // glyph straddling the pane edge never bleeds below it.
    const float gtop = std::max(gy, clip_top);
    const float gbottom = std::min(gy + gh, clip_bottom);
    if (gtop >= gbottom)
    {
      continue;
    }
    const float nv0 = g.v0 + (gtop - gy) / gh * (g.v1 - g.v0);
    const float nv1 = g.v1 - (gy + gh - gbottom) / gh * (g.v1 - g.v0);
    push_quad(gx, gtop, gx + gw, gbottom, g.u0, nv0, g.u1, nv1, fr, fg_, fb, 1.0f);
  }
}

// Pushes the underline quads of one row slice (wavy or straight).
void UIGui::paint_row_underlines(const std::vector<UICell> &src,
                                 int col0,
                                 int col1,
                                 float x0px,
                                 float y_top,
                                 float dy,
                                 bool &any,
                                 float clip_top,
                                 float clip_bottom)
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
    // 1-2px marks: skip outright when outside the clip band.
    if (y0 < clip_top || y0 + 2.0f > clip_bottom)
    {
      continue;
    }
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
  // The sprite is the pane body sliding; clip every quad to the pane rect
  // so shifted content can never paint below the pane (status line / its
  // margin) -- paint_plain only repaints backgrounds there, so glyphs
  // would otherwise stay visible as "code behind the status bar".
  const float clip_top = (float)a.y1 * cell_h_;
  const float clip_bottom = (float)a.y2 * cell_h_;

  // Backgrounds.
  size_t quads = 0;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : (*content_grid_)[(size_t)(a.y1 + r)];
    paint_row_bg(row, rows ? 0 : a.x1, rows ? want_w : a.x2, x0px, (float)(a.y1 + r) * cell_h_, dy,
                 quads, clip_top, clip_bottom);
  }
  if (quads)
  {
    flush_tex(white_tex_);
    end_batch();
  }

  // Glyphs.
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : (*content_grid_)[(size_t)(a.y1 + r)];
    paint_row_glyphs(row, rows ? 0 : a.x1, rows ? want_w : a.x2, x0px, (float)(a.y1 + r) * cell_h_,
                     dy, clip_top, clip_bottom);
  }
  flush_tex(atlas_tex_);
  end_batch();

  // Underlines.
  bool any = false;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row =
        rows ? (*rows)[(size_t)r] : (*content_grid_)[(size_t)(a.y1 + r)];
    paint_row_underlines(row,
                         rows ? 0 : a.x1,
                         rows ? want_w : a.x2,
                         x0px,
                         (float)(a.y1 + r) * cell_h_,
                         dy,
                         any,
                         clip_top,
                         clip_bottom);
  }
  if (any)
  {
    flush_tex(white_tex_);
    end_batch();
  }
}

// Paints every static row (no animation): the whole grid except the body
// columns of animating panes, which the sprites already covered. Runs after
// the sprites so borders, tab strips, status lines and neighbouring panes
// always win where sliding content crossed them.
void UIGui::paint_plain()
{
  // Per-row column ranges the static pass must not paint: the body of
  // animating panes (the slide sprites already covered them) and the final
  // rects of native-floated surfaces mid-entrance (sidebar / right dock /
  // home screen -- the sliding float copy would otherwise ghost over their
  // static paint). Ranges are merged and the complement painted as
  // segments, so several skips per row coexist.
  std::vector<std::pair<int, int>> skips;
  skips.reserve(4);
  std::vector<std::pair<int, int>> segs;
  segs.reserve(6);
  auto row_segments = [&](int y)
  {
    skips.clear();
    for (const auto &kv : scroll_anims_)
    {
      const GuiScrollAnim &a = kv.second;
      if (std::abs(a.offset_px) < 0.25f)
      {
        continue;
      }
      if (y >= a.y1 && y < a.y2)
      {
        skips.emplace_back(a.x1, a.x2);
        break;
      }
    }
    for (const GuiFloatRect &r : entering_float_rects_)
    {
      if (y >= r.y && y < r.y + r.h)
      {
        const int c0 = std::max(0, r.x);
        const int c1 = std::min(width, r.x + r.w);
        if (c1 > c0)
        {
          skips.emplace_back(c0, c1);
        }
      }
    }
    std::sort(skips.begin(), skips.end());
    segs.clear();
    int c = 0;
    for (const auto &s : skips)
    {
      if (s.second <= c)
      {
        continue; // contained in the previous skip
      }
      if (s.first > c)
      {
        segs.emplace_back(c, std::min(s.first, width));
      }
      c = std::max(c, std::min(s.second, width));
    }
    if (c < width)
    {
      segs.emplace_back(c, width);
    }
  };

  size_t quads = 0;
  begin_batch();
  for (int y = 0; y < height; y++)
  {
    row_segments(y);
    const float y_top = (float)y * cell_h_;
    for (const auto &s : segs)
    {
      paint_row_bg((*content_grid_)[(size_t)y], s.first, s.second, 0.0f, y_top, 0.0f, quads);
    }
  }
  if (quads)
  {
    flush_tex(white_tex_);
    end_batch();
  }

  begin_batch();
  for (int y = 0; y < height; y++)
  {
    row_segments(y);
    const float y_top = (float)y * cell_h_;
    for (const auto &s : segs)
    {
      paint_row_glyphs((*content_grid_)[(size_t)y], s.first, s.second, 0.0f, y_top, 0.0f);
    }
  }
  flush_tex(atlas_tex_);
  end_batch();

  bool any = false;
  begin_batch();
  for (int y = 0; y < height; y++)
  {
    row_segments(y);
    const float y_top = (float)y * cell_h_;
    for (const auto &s : segs)
    {
      paint_row_underlines((*content_grid_)[(size_t)y], s.first, s.second, 0.0f, y_top, 0.0f,
                           any);
    }
  }
  if (any)
  {
    flush_tex(white_tex_);
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
      const std::vector<UICell> &src = (*content_grid_)[(size_t)(a.y1 + r)];
      std::vector<UICell> &drow = dst->rows[(size_t)r];
      for (int c = 0; c < w; c++)
      {
        drow[(size_t)c] = src[(size_t)(a.x1 + c)];
      }
    }
  }
}

// Snapshots the float-free grid (Lua floats paint after this call), so the
// content passes can draw from a grid that never contains float cells.
// Cleared when no floats are visible, which reverts render() to painting
// the live grid directly.
void UIGui::before_float_render(bool has_visible)
{
  if (has_visible)
  {
    pre_float_grid_ = grid;
  }
  else
  {
    pre_float_grid_.clear();
  }
}

// Modal scrim: record the dim (all callers dim the whole window) instead
// of baking it into the grid; render() draws a smoothly-fading black quad
// so the dim fades in/out with the modal instead of popping.
void UIGui::dim_rect(const UIRect &rect)
{
  (void)rect;
  dim_active_ = true;
}

// Fixed-overlay pass: paints every visible float from the live grid at its
// absolute screen position, on top of the (possibly sliding) content. The
// slide sprites never contain float cells, so toasts and popups stay put
// while the content scrolls under them. Editor-relative floats (toasts)
// additionally ease between Lua's whole-cell animation steps at frame
// rate, turning the 50ms row-stepping into a smooth glide.
std::string UIGui::float_key(const std::string &surface, int handle)
{
  return surface.empty() ? "h:" + std::to_string(handle) : "s:" + surface;
}

UIGui::GuiFloatKind UIGui::float_kind_for(const std::string &surface, int x, int w, int width)
{
  if (surface == "sidebar")
    return GuiFloatKind::kLeft;
  if (surface == "side_panel")
    return GuiFloatKind::kRight;
  if (surface == "status_line" || surface == "command_palette")
    return GuiFloatKind::kBottom;
  // Geometric fallbacks for standalone floats (toasts, user floats).
  if (x <= 0 && w < width / 2)
    return GuiFloatKind::kLeft;
  if (x + w >= width && w < width / 2)
    return GuiFloatKind::kRight;
  return GuiFloatKind::kCenter;
}

void UIGui::paint_float_overlays(float dt)
{
  float_anims_transitioning_ = false;
  float_colors_transitioning_ = false;

  // Keys present this frame: surfaces re-emit every frame with a fresh
  // handle, so identity is the surface name (or the handle for standalone
  // floats like toasts).
  std::unordered_set<std::string> cur_keys;
  for (const FloatOverlay &ov : float_overlays)
  {
    if (ov.w <= 0 || ov.h <= 0 || ov.x < 0 || ov.y < 0)
    {
      continue;
    }
    cur_keys.insert(float_key(ov.surface, ov.handle));
  }

  // A key that vanished since the last frame has closed: after a short
  // grace period (a surface can skip a frame mid-resize) start its exit
  // animation from the retained capture (the live grid no longer holds
  // it), fading/sliding it toward its edge. Restarting an entrance or
  // starting an exit on a one-frame absence would let the native paint
  // flash through (the ghosted double-explorer) and stall the fade-in.
  constexpr int kFloatGoneGraceFrames = 3;
  for (auto &kv : float_anims_)
  {
    GuiFloatAnim &a = kv.second;
    if (a.exiting || cur_keys.count(kv.first))
    {
      a.gone_frames = 0;
      continue;
    }
    if (a.gone_frames < kFloatGoneGraceFrames)
    {
      a.gone_frames++;
      continue;
    }
    auto it = float_colors_.find(kv.first);
    if (it != float_colors_.end())
    {
      a.exit_cells = std::move(it->second.cells);
      a.exit_rgb = std::move(it->second.rgb);
      float_colors_.erase(it);
    }
    a.exiting = true;
    a.exit_t = 0.0f;
    float_anims_transitioning_ = true;
  }

  // Drop finished exits and stale non-exiting anims/colors.
  for (auto it = float_anims_.begin(); it != float_anims_.end();)
  {
    const bool done = it->second.exiting && it->second.exit_t >= 1.0f;
    const bool stale = !it->second.exiting && !cur_keys.count(it->first)
                       && it->second.gone_frames >= kFloatGoneGraceFrames;
    if (done || stale)
    {
      it = float_anims_.erase(it);
    }
    else
    {
      ++it;
    }
  }
  for (auto it = float_colors_.begin(); it != float_colors_.end();)
  {
    bool keep = cur_keys.count(it->first);
    if (!keep)
    {
      auto fa = float_anims_.find(it->first);
      keep = fa != float_anims_.end() && fa->second.exiting;
    }
    if (!keep)
    {
      it = float_colors_.erase(it);
    }
    else
    {
      ++it;
    }
  }

  // Exponential ease toward the layout position (~70ms settle).
  const float pos_k = 1.0f - std::exp(-dt / 0.07f);
  const float col_k = 1.0f - std::exp(-dt / kFloatColorTau);

  // Paint one float with its animation state. The paint loop below runs in
  // two layers around the modal scrim quad: background floats (sidebar,
  // status line, toasts) render first and get dimmed by the scrim, then
  // the open modal surface's own panel (quick pick, settings, telescope,
  // ...) paints bright on top -- the same order the terminal bakes into
  // the cell grid.
  auto paint_one = [&](const FloatOverlay &ov)
  {
    if (ov.w <= 0 || ov.h <= 0 || ov.x < 0 || ov.y < 0)
    {
      return;
    }
    const std::string key = float_key(ov.surface, ov.handle);
    const int n = std::min(ov.h, std::max(0, height - ov.y));
    GuiFloatAnim &anim = float_anims_[key];
    GuiFloatColors &fc = float_colors_[key];
    const float tx = (float)ov.x * cell_w_;
    const float ty = (float)ov.y * cell_h_;

    // First appearance (or a reopen during the exit): start the entrance
    // transition -- slide/fade in from the float's edge.
    if (!anim.seen || anim.exiting)
    {
      anim.seen = true;
      anim.exiting = false;
      anim.exit_t = 0.0f;
      anim.enter_t = 0.0f;
      anim.kind = float_kind_for(ov.surface, ov.x, ov.w, width);
      anim.x_px = tx;
      anim.y_px = ty;
    }
    // Remember the rect so a future exit can paint from it.
    anim.exit_x = ov.x;
    anim.exit_y = ov.y;
    anim.exit_w = ov.w;
    anim.exit_h = n;

    float dx = 0.0f, dy = 0.0f;
    if (ov.animate)
    {
      // Teleport (window resize, big jump): snap instead of gliding.
      if (std::fabs(anim.x_px - tx) > 4.0f * cell_w_
          || std::fabs(anim.y_px - ty) > 4.0f * cell_h_)
      {
        anim.x_px = tx;
        anim.y_px = ty;
      }
      else
      {
        anim.x_px += (tx - anim.x_px) * pos_k;
        anim.y_px += (ty - anim.y_px) * pos_k;
        if (std::fabs(anim.x_px - tx) < 0.05f && std::fabs(anim.y_px - ty) < 0.05f)
        {
          anim.x_px = tx;
          anim.y_px = ty;
        }
      }
      dx = anim.x_px - tx;
      dy = anim.y_px - ty;
      if (std::fabs(dx) > 0.25f || std::fabs(dy) > 0.25f)
      {
        float_anims_transitioning_ = true;
      }
    }

    // Entrance progress: alpha ramps in while the float slides from its
    // edge toward its slot.
    if (anim.enter_t < 1.0f)
    {
      anim.enter_t = std::min(1.0f, anim.enter_t + dt / kFloatEnterSecs);
      float_anims_transitioning_ = true;
    }
    const float ease_in = anim.enter_t * anim.enter_t * (3.0f - 2.0f * anim.enter_t);
    float alpha = ease_in;
    float edx = 0.0f, edy = 0.0f;
    if (anim.enter_t < 1.0f)
    {
      float vx = 0.0f, vy = 0.0f;
      switch (anim.kind)
      {
      case GuiFloatKind::kLeft:
        vx = -(float)ov.w * cell_w_;
        break;
      case GuiFloatKind::kRight:
        vx = (float)ov.w * cell_w_;
        break;
      case GuiFloatKind::kCenter:
        vy = cell_h_;
        break;
      case GuiFloatKind::kBottom:
        break;
      }
      edx = vx * (1.0f - ease_in);
      edy = vy * (1.0f - ease_in);
    }

    // Color capture/easing: only standalone floats (toasts) ease colors
    // at frame rate (their Lua fade steps land every 50ms); handler
    // surfaces repaint their content every frame (typing in a picker must
    // not cross-fade), so they adopt the live colors instantly.
    const bool color_ease = ov.surface.empty();
    const bool fresh = (int)fc.cells.size() != n
        || (n > 0 && (int)fc.cells[0].size() != ov.w);
    if (fresh)
    {
      fc.cells.assign((size_t)n, std::vector<UICell>((size_t)ov.w));
      fc.rgb.assign((size_t)n * (size_t)ov.w * 9, 0.0f);
      fc.settled = true;
    }
    // Source of this float's pixels: the per-float capture published by
    // render_floats (exact — later floats painted over the shared grid), or
    // the live grid when no capture is available (terminal-style fallback).
    const bool have_cells = (int)ov.cells.size() == n
        && (n == 0 || (int)ov.cells[0].size() == ov.w);
    bool changed = false;
    float max_diff = 0.0f;
    for (int r = 0; r < n; r++)
    {
      const std::vector<UICell> &grow =
          have_cells ? ov.cells[(size_t)r] : grid[(size_t)(ov.y + r)];
      std::vector<UICell> &crow = fc.cells[(size_t)r];
      float *rbase = fc.rgb.data() + (size_t)r * (size_t)ov.w * 9;
      for (int c = 0; c < ov.w; c++)
      {
        const UICell &live = grow[(size_t)c];
        float cur[9];
        resolve_cell_rgb(live, cur);
        float *p = rbase + (size_t)c * 9;
        if (fresh || !color_ease)
        {
          for (int i = 0; i < 9; i++)
          {
            p[i] = cur[i];
          }
        }
        else if (!fc.settled)
        {
          // Ease the displayed colors toward the live colors.
          for (int i = 0; i < 9; i++)
          {
            p[i] += (cur[i] - p[i]) * col_k;
          }
        }
        for (int i = 0; i < 9; i++)
        {
          max_diff = std::max(max_diff, std::fabs(cur[i] - p[i]));
        }
        if (live != crow[(size_t)c])
        {
          changed = true;
        }
        crow[(size_t)c] = live;
      }
    }
    if (fresh)
    {
      // Adopted as-is; nothing to smooth.
    }
    else if (!fc.settled && max_diff < 0.5f / 255.0f)
    {
      // Converged: snap the stored colors to the live values exactly so
      // static floats stay pixel-identical (no drift) and the pump can
      // stop repainting.
      for (int r = 0; r < n; r++)
      {
        float *rbase = fc.rgb.data() + (size_t)r * (size_t)ov.w * 9;
        const std::vector<UICell> &crow = fc.cells[(size_t)r];
        for (int c = 0; c < ov.w; c++)
        {
          float cur[9];
          resolve_cell_rgb(crow[(size_t)c], cur);
          for (int i = 0; i < 9; i++)
          {
            rbase[(size_t)c * 9 + (size_t)i] = cur[i];
          }
        }
      }
      fc.settled = true;
    }
    else if (!fc.settled || (color_ease && changed && max_diff > 0.5f / 255.0f))
    {
      fc.settled = false;
      float_colors_transitioning_ = true;
    }

    paint_float_cells(ov.x, ov.y, ov.w, n, dx + edx, dy + edy, fc.cells, fc.rgb, alpha);
  };

  // Modal surfaces (match api_float.cpp's modal_surface_open set, plus the
  // settings menu): their own panel draws on top of the scrim; everything
  // else is background and sits under it.
  const auto is_modal_float = [](const std::string &s) -> bool
  {
    return s == "quick_pick" || s == "popup" || s == "tree_sitter_status"
           || s == "lsp_status" || s == "telescope" || s == "settings";
  };
  for (const FloatOverlay &ov : float_overlays)
  {
    if (!is_modal_float(ov.surface))
    {
      paint_one(ov);
    }
  }
  if (dim_alpha_ > 0.003f)
  {
    begin_batch();
    push_quad(0.0f,
              0.0f,
              (float)width * cell_w_,
              (float)height * cell_h_,
              0,
              0,
              1,
              1,
              0.0f,
              0.0f,
              0.0f,
              dim_alpha_);
    flush_tex(white_tex_);
    end_batch();
  }
  for (const FloatOverlay &ov : float_overlays)
  {
    if (is_modal_float(ov.surface))
    {
      paint_one(ov);
    }
  }

  // Exiting floats: paint from the retained capture, fading out while
  // sliding toward the same edge they entered from.
  for (auto &kv : float_anims_)
  {
    GuiFloatAnim &a = kv.second;
    if (!a.exiting)
    {
      continue;
    }
    if (a.exit_t < 1.0f)
    {
      a.exit_t = std::min(1.0f, a.exit_t + dt / kFloatExitSecs);
      float_anims_transitioning_ = true;
    }
    const float e = a.exit_t * a.exit_t * (3.0f - 2.0f * a.exit_t);
    float vx = 0.0f, vy = 0.0f;
    switch (a.kind)
    {
    case GuiFloatKind::kLeft:
      vx = -(float)a.exit_w * cell_w_;
      break;
    case GuiFloatKind::kRight:
      vx = (float)a.exit_w * cell_w_;
      break;
    case GuiFloatKind::kCenter:
      vy = cell_h_;
      break;
    case GuiFloatKind::kBottom:
      break;
    }
    paint_float_cells(a.exit_x, a.exit_y, a.exit_w, a.exit_h, vx * e, vy * e, a.exit_cells,
                      a.exit_rgb, 1.0f - e);
  }

  last_float_keys_ = std::move(cur_keys);
}

// Paints one float from its captured cells + resolved RGB (see
// GuiFloatColors) at the layout position translated by (dx_px, dy_px). The
// captured cells are stored at RELATIVE column index (row c holds the
// grid's column x+c), so column c must land at pixel (x+c)*cell_w_ — the
// rect's column base x*cell_w_ plus the translation. Glyphs/attrs come
// from `cells`; colors come from `rgb` (fg/bg/underline, 9 floats per
// cell), which paint_float_overlays eases toward the live grid's colors.
void UIGui::paint_float_cells(int x, int y, int w, int h, float dx_px, float dy_px,
                              const std::vector<std::vector<UICell>> &cells,
                              const std::vector<float> &rgb,
                              float alpha)
{
  if (x < 0 || y < 0 || w <= 0 || h <= 0)
  {
    return;
  }
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  const int n = std::min(h, std::max(0, height - y));
  if (n <= 0)
  {
    return;
  }
  const float x0 = (float)x * cell_w_ + dx_px;

  // Backgrounds (runs coalesce on the blended bg color).
  size_t quads = 0;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const float py = (float)(y + r) * cell_h_ + dy_px;
    int c = 0;
    while (c < w)
    {
      const float *p = rgb.data() + ((size_t)r * (size_t)w + (size_t)c) * 9;
      int run_end = c + 1;
      while (run_end < w)
      {
        const float *q = rgb.data() + ((size_t)r * (size_t)w + (size_t)run_end) * 9;
        if (q[3] != p[3] || q[4] != p[4] || q[5] != p[5])
        {
          break;
        }
        run_end++;
      }
      push_quad(x0 + (float)c * cell_w_, py, x0 + (float)run_end * cell_w_, py + cell_h_, 0, 0,
                1, 1, p[3], p[4], p[5], alpha);
      quads++;
      c = run_end;
    }
  }
  if (quads)
  {
    flush_tex(white_tex_);
    end_batch();
  }

  // Glyphs.
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row = cells[(size_t)r];
    const float py = (float)(y + r) * cell_h_ + dy_px;
    for (int c = 0; c < w; c++)
    {
      const UICell &cell = row[(size_t)c];
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
      const float *p = rgb.data() + ((size_t)r * (size_t)w + (size_t)c) * 9;
      const float gx = x0 + (float)c * cell_w_ + g.bearing_x;
      const float gy = py + ascent_ - g.bearing_top;
      const float gw = (g.u1 - g.u0) * kAtlasW;
      const float gh = (g.v1 - g.v0) * kAtlasH;
      push_quad(gx, gy, gx + gw, gy + gh, g.u0, g.v0, g.u1, g.v1, p[0], p[1], p[2], alpha);
    }
  }
  flush_tex(atlas_tex_);
  end_batch();

  // Underlines.
  bool any = false;
  begin_batch();
  for (int r = 0; r < n; r++)
  {
    const std::vector<UICell> &row = cells[(size_t)r];
    const float py = (float)(y + r) * cell_h_ + dy_px;
    for (int c = 0; c < w; c++)
    {
      const UICell &cell = row[(size_t)c];
      if (cell.underline == 0)
      {
        continue;
      }
      any = true;
      const float *p = rgb.data() + ((size_t)r * (size_t)w + (size_t)c) * 9;
      const float x0c = x0 + (float)c * cell_w_;
      const float y0 = py + cell_h_ - 2.0f;
      if (cell.underline == 2)
      {
        const float seg = cell_w_ / 3.0f;
        for (int s = 0; s < 3; s++)
        {
          const float sy = y0 - (s % 2 == 0 ? 0.0f : 1.0f);
          push_quad(x0c + s * seg, sy, x0c + (s + 1) * seg, sy + 1.0f, 0, 0, 1, 1, p[6], p[7],
                    p[8], alpha);
        }
      }
      else
      {
        push_quad(x0c, y0, x0c + cell_w_, y0 + 1.0f, 0, 0, 1, 1, p[6], p[7], p[8], alpha);
      }
    }
  }
  if (any)
  {
    flush_tex(white_tex_);
    end_batch();
  }
}

