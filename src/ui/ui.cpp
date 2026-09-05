#include "ui.h"
#include "ui/text.h"
#include <algorithm>

namespace
{
  int rendered_cell_width(const std::string &text)
  {
    if (text.empty())
      return 1;
    return std::max(1, ui_cell_count(text));
  }

  void append_sanitized_cell_text(std::string &out, const std::string &text)
  {
    if (text.empty())
    {
      out.push_back(' ');
      return;
    }
    if (text.size() == 1 && ((unsigned char)text[0] & 0x80) == 0)
    {
      out.push_back(text[0]);
      return;
    }
    out += ui_is_valid_utf8_sequence(text) ? text : "?";
  }

  void write_sanitized_cell_text(Terminal *term, const std::string &text)
  {
    if (text.empty())
    {
      term->write_char(' ');
      return;
    }
    if (text.size() == 1 && ((unsigned char)text[0] & 0x80) == 0)
    {
      term->write_char(text[0]);
      return;
    }
    term->write(ui_is_valid_utf8_sequence(text) ? text : "?");
  }

  void append_cell_for_remaining_width(std::string &out, const std::string &text, int remaining)
  {
    int width = rendered_cell_width(text);
    if (width > remaining)
    {
      out.append((size_t)remaining, ' ');
      return;
    }
    append_sanitized_cell_text(out, text);
  }

  void write_cell_for_remaining_width(Terminal *term, const std::string &text, int remaining)
  {
    int width = rendered_cell_width(text);
    if (width > remaining)
    {
      for (int i = 0; i < remaining; i++)
        term->write_char(' ');
      return;
    }
    write_sanitized_cell_text(term, text);
  }
} // namespace

UI::UI(Terminal *t)
    : term(t), width(80), height(24), cursor_x(-1), cursor_y(-1),
      cursor_shape(UICursorShape::Block), cursor_hidden(true)
{
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++)
  {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++)
    {
      grid[y][x] = {" ", default_fg, default_bg, false, false, false};
      last_grid[y][x] = {" ", default_fg, default_bg, false, false, false};
    }
  }
  mark_all_rows_dirty();
}

void UI::mark_all_rows_dirty()
{
  row_dirty.assign(height, (unsigned char)1);
}

void UI::resize(int w, int h)
{
  int new_w = std::max(1, w);
  int new_h = std::max(1, h);
  bool dim_changed = (new_w != width) || (new_h != height);
  width = new_w;
  height = new_h;
  cursor_x = -1;
  cursor_y = -1;
  cursor_shape = UICursorShape::Block;
  cursor_hidden = true;
  cursor_dirty = true;
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++)
  {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++)
    {
      grid[y][x] = {" ", default_fg, default_bg, false, false, false};
      last_grid[y][x] = {" ", default_fg, default_bg, false, false, false};
    }
  }
  // The grid was just blanked, so the next frame must repaint every row.
  mark_all_rows_dirty();
  // Only invalidate (which calls term->clear()) when the dimensions actually
  // changed, to avoid an extra ESC[2J when the Editor constructor and
  // Editor::run() each call resize() at startup.
  if (dim_changed)
  {
    invalidate();
  }
}

void UI::invalidate()
{
  cursor_x = -1;
  cursor_y = -1;
  cursor_shape = UICursorShape::Block;
  cursor_hidden = true;
  cursor_dirty = true;
  term->clear();
  // The physical screen was cleared; the next frame must repaint every row.
  mark_all_rows_dirty();
}

void UI::clear()
{
  for (auto &row : grid)
  {
    for (auto &cell : row)
    {
      cell = {" ", default_fg, default_bg, false, false, false};
    }
  }
  mark_all_rows_dirty();
}

void UI::set_default_colors(int fg, int bg)
{
  default_fg = fg;
  default_bg = bg;
}

void UI::dim_rect(const UIRect &rect)
{
  const int x0 = std::max(0, rect.x);
  const int y0 = std::max(0, rect.y);
  const int x1 = std::min(width, rect.x + std::max(0, rect.w));
  const int y1 = std::min(height, rect.y + std::max(0, rect.h));
  for (int y = y0; y < y1; y++)
  {
    for (int x = x0; x < x1; x++)
    {
      grid[y][x].dim = true;
    }
    row_dirty[y] = 1;
  }
}

void UI::set_cell(int x, int y, const UICell &cell)
{
  if (x >= 0 && x < width && y >= 0 && y < height)
  {
    grid[y][x] = cell;
    row_dirty[y] = 1;
  }
}

const UICell *UI::cell_at(int x, int y) const
{
  if (x < 0 || x >= width || y < 0 || y >= height)
  {
    return nullptr;
  }
  return &grid[y][x];
}

void UI::render()
{
  // Row-diffing full-paint renderer. The draw layer repaints the whole
  // grid every frame (immediate mode), so render() compares each row
  // against last_grid -- the frame that was actually written to the
  // terminal -- and only emits rows whose content genuinely changed.
  // A typical typing frame then writes the 1-3 edited rows instead of
  // the whole screen, cutting per-frame terminal output by ~90% on big
  // buffers. That output volume is what makes typing/scrolling feel laggy
  // on I/O-bound terminals: thousands of SGR sequences must be parsed by
  // the terminal emulator for every keystroke.
  //
  // Skipping is safe because a skipped row is byte-identical to what the
  // terminal already shows: nothing wrote to it since it was painted (it
  // is compared cell-by-cell against the retained copy), and every paint
  // pads the row to full width and erases its right margin, so no stale
  // content can linger. Capture modes (JOT_RENDER_CAPTURE*) force a full
  // paint of every row so their logs stay unambiguous; capture raw also
  // disables run coalescing so every cell is written one at a time.

  bool capture_on = term->render_capture_enabled();
  bool capture_raw = term->render_capture_raw();

  // Capture logs must stay unambiguous frame-to-frame, so capture mode
  // always paints every row (identical to the legacy always-full
  // renderer).
  const bool force_full = capture_on || capture_raw;

  // Terminals can occasionally drop or garble a row's bytes mid-frame.
  // With row-skipping a corrupted-but-unchanged row would otherwise stay
  // corrupted forever, so every kSelfHealFrames rendered frames is a full
  // repaint. At 60fps that is ~1.5s. Frame counting (not wall time) keeps
  // the behaviour deterministic and only "active" frames count: idle
  // frames rarely reach render() at all.
  constexpr int kSelfHealFrames = 90;
  const bool self_heal = renders_since_full_paint_ >= kSelfHealFrames;

  // Keep intermediate row cursor moves invisible. Only the final cursor
  // state below should reach the terminal as visible state.
  term->hide_cursor();
  term->disable_autowrap();

  // Per-frame right-edge safety margin. The renderer must never write the
  // rightmost physical column of any row.
  // On many terminals, writing the rightmost cell of a row leaves the
  // cursor in a "pending wrap" state; at large widths the next cursor
  // move can be misinterpreted as a wrap, scroll the viewport, and show
  // stale or overlapping text from a previous frame. The one-cell margin
  // is fixed so full-width borders do not leave an oversized right gap.
  const int margin = term->render_margin();
  const int row_width = std::max(0, width - margin);
  const bool paint_all = force_full || self_heal;
  if (paint_all)
  {
    renders_since_full_paint_ = 0;
  }
  else
  {
    renders_since_full_paint_++;
  }
  for (int y = 0; y < height; y++)
  {
    if (row_width <= 0)
    {
      // Still emit \x1b[K on the previous row's position so any stale
      // content in the (unpaintable) right margin is cleared. This is
      // belt-and-suspenders for terminals that ignore the move_cursor
      // and try to continue from the previous cursor position.
      if (y > 0)
      {
        term->clear_to_end();
      }
      row_dirty[y] = 0;
      continue;
    }

    if (!paint_all)
    {
      if (!row_dirty[y])
      {
        // Nothing in this row was even drawn to this frame.
        continue;
      }
      // Redrawn this frame, but cell-for-cell identical to the frame the
      // terminal already shows: no output needed. The per-row dirty flag
      // alone cannot decide this -- immediate-mode drawing rewrites every
      // row every frame -- so compare against the retained last_grid.
      const auto &cur = grid[y];
      const auto &prev = last_grid[y];
      bool same = true;
      for (int x = 0; x < row_width; x++)
      {
        if (cur[x] != prev[x])
        {
          same = false;
          break;
        }
      }
      if (same)
      {
        row_dirty[y] = 0;
        continue;
      }
    }
    row_dirty[y] = 0;

    term->move_cursor(0, y);

    if (capture_raw)
    {
      for (int x = 0; x < row_width;)
      {
        const auto &cell = grid[y][x];
        term->reset_color();
        if (cell.bold)
          term->set_bold(true);
        if (cell.italic)
          term->set_italic(true);
        if (cell.dim)
          term->set_dim(true);
        if (cell.reverse)
          term->set_reverse(true);
        term->set_color(cell.fg, cell.bg);
        write_cell_for_remaining_width(term, cell.ch, row_width - x);
        x += std::min(rendered_cell_width(cell.ch), row_width - x);
      }
    }
    else
    {
      int run_fg = -1;
      int run_bg = -1;
      bool run_bold = false;
      bool run_italic = false;
      bool run_dim = false;
      bool run_reverse = false;
      int written = 0;

      std::string body;
      body.reserve((size_t)row_width);

      for (int x = 0; x < row_width;)
      {
        const auto &cell = grid[y][x];

        if (x == 0 || cell.fg != run_fg || cell.bg != run_bg || cell.bold != run_bold
            || cell.italic != run_italic || cell.dim != run_dim || cell.reverse != run_reverse)
        {
          // Optimization: skip ESC[0m (full reset) when only the
          // fg/bg have changed and the bold/italic/reverse bits are
          // still correct. A full reset costs ~5 bytes and reverts
          // background to terminal default, which can flash on
          // terminals with delayed SGR processing. SGR 38;5; and
          // 48;5; are independent of bold/italic/reverse so we can
          // set them in place.
          const bool attrs_unchanged =
              (x != 0) && cell.bold == run_bold && cell.italic == run_italic && cell.dim == run_dim
              && cell.reverse == run_reverse && (run_fg != -1 || run_bg != -1);
          if (!attrs_unchanged)
          {
            term->reset_color();
            if (cell.bold)
              term->set_bold(true);
            if (cell.italic)
              term->set_italic(true);
            if (cell.dim)
              term->set_dim(true);
            if (cell.reverse)
              term->set_reverse(true);
          }
          else
          {
            // Only fg/bg changed within the same attribute set.
            // reset_color() is still needed only when transitioning
            // *out* of bold/italic/reverse; otherwise just emit the
            // new fg/bg in place.
            if (cell.bold != run_bold)
              term->set_bold(cell.bold);
            if (cell.italic != run_italic)
              term->set_italic(cell.italic);
            if (cell.dim != run_dim)
              term->set_dim(cell.dim);
            if (cell.reverse != run_reverse)
              term->set_reverse(cell.reverse);
          }
          term->set_color(cell.fg, cell.bg);
          run_fg = cell.fg;
          run_bg = cell.bg;
          run_bold = cell.bold;
          run_italic = cell.italic;
          run_dim = cell.dim;
          run_reverse = cell.reverse;
        }

        body.clear();

        int run_start = x;
        while (x < row_width && grid[y][x].fg == run_fg && grid[y][x].bg == run_bg
               && grid[y][x].bold == run_bold && grid[y][x].italic == run_italic
               && grid[y][x].dim == run_dim && grid[y][x].reverse == run_reverse)
        {
          int cell_w = std::min(rendered_cell_width(grid[y][x].ch), row_width - x);
          append_cell_for_remaining_width(body, grid[y][x].ch, row_width - x);
          x += cell_w;
        }

        term->write(body);
        written += (x - run_start);
      }

      while (written < row_width)
      {
        term->write(" ");
        written++;
      }
    }

    // Erase any leftover content from the previous frame that might
    // still occupy the margin columns (the `width - margin` columns
    // we deliberately left untouched). \x1b[K (EL 0) erases from the
    // cursor to end of line, so it covers the margin and any
    // characters past the row's last painted cell. With autowrap
    // disabled, the cursor stays at the end of the written text and
    // the erase is bounded to the current row.
    // Move past the painted cells before erasing the untouched margin.
    term->move_cursor(row_width, y);
    term->clear_to_end();

    // Retain this row as the new baseline for next frame's diff. Rows that
    // were skipped above keep their previous (still-accurate) baseline.
    last_grid[y] = grid[y];
  }

  term->reset_color();

  if (cursor_hidden)
  {
    term->hide_cursor();
  }
  else
  {
    int cx = (cursor_x < 0) ? 0 : cursor_x;
    int cy = (cursor_y < 0) ? 0 : cursor_y;
    // Clamp x one cell inside the render margin so the cursor itself is
    // never parked on a right-edge cell. This applies to all widths
    // greater than 1; on a single-column terminal we obviously cannot
    // move the cursor further left.
    const int cursor_max_x = (width > 1) ? (width - margin - 1) : (width - 1);
    if (cx > cursor_max_x)
      cx = cursor_max_x;
    if (cx < 0)
      cx = 0;
    if (cy >= height)
      cy = height - 1;
    if (cy < 0)
      cy = 0;
    term->move_cursor(cx, cy);
    term->show_cursor();
  }

  term->write(cursor_shape == UICursorShape::Bar ? "\033[5 q" : "\033[1 q");
  term->flush();
  cursor_dirty = false;

  if (capture_on)
  {
    char label[64];
    snprintf(label, sizeof(label), "FRAME w=%d h=%d", width, height);
    term->render_capture_marker(label, height);
  }
}

// Store cursor state without emitting terminal writes. render() (or
// flush_cursor() when render() is not called) is responsible for
// materialising the cursor on the terminal.
void UI::set_cursor(int x, int y, UICursorShape shape)
{
  cursor_x = std::clamp(x, 0, std::max(0, width - 1));
  cursor_y = std::clamp(y, 0, std::max(0, height - 1));
  cursor_shape = shape;
  cursor_hidden = false;
  cursor_dirty = true;
}

void UI::hide_cursor()
{
  cursor_hidden = true;
  cursor_dirty = true;
}

void UI::reset_cursor_state()
{
  cursor_x = -1;
  cursor_y = -1;
  cursor_shape = UICursorShape::Block;
  cursor_hidden = true;
  cursor_dirty = true;
}

// Emit only the current cursor state to the terminal buffer and flush.
// Used by the !needs_redraw path in Editor::render() when the frame's
// grid is unchanged and the only thing that needs updating is the
// blinking cursor / selection caret.
void UI::flush_cursor()
{
  term->disable_autowrap();
  const int margin = term->render_margin();
  if (cursor_hidden)
  {
    term->hide_cursor();
  }
  else
  {
    int cx = (cursor_x < 0) ? 0 : cursor_x;
    int cy = (cursor_y < 0) ? 0 : cursor_y;
    // Same right-edge clamp as render(): never park the cursor on the
    // rightmost cell the renderer leaves untouched.
    const int cursor_max_x = (width > 1) ? (width - margin - 1) : (width - 1);
    if (cx > cursor_max_x)
      cx = cursor_max_x;
    if (cx < 0)
      cx = 0;
    if (cy >= height)
      cy = height - 1;
    if (cy < 0)
      cy = 0;
    term->move_cursor(cx, cy);
    term->show_cursor();
  }
  term->write(cursor_shape == UICursorShape::Bar ? "\033[5 q" : "\033[1 q");
  term->flush();
  cursor_dirty = false;

  if (term->render_capture_enabled())
  {
    term->render_capture_marker("CURSOR-ONLY", 0);
  }
}

void UI::emit_raw_after_frame(const std::string &bytes)
{
  if (bytes.empty())
    return;
  term->write(bytes);
  term->flush();
  cursor_dirty = true;
}

void UI::draw_text(int x, int y, const std::string &text, int fg, int bg, bool bold, bool italic)
{
  // Guard against invisible normal text: if the caller used the default-bg
  // path (bg < 0) and the requested foreground would match the background,
  // substitute the default foreground so editor text is always readable.
  // This protects default/editor text paths from theme misconfiguration
  // (fg_default == bg_default) without overriding intentional styling
  // where both fg and bg are explicitly set (e.g., selection highlights).
  bool used_default_bg = (bg < 0);
  if (used_default_bg)
    bg = default_bg;
  if (used_default_bg && fg == default_bg)
  {
    fg = default_fg;
  }
  int i = 0;
  int cell_offset = 0;
  while (i < (int)text.length() && x + cell_offset < width)
  {
    int cluster_end = ui_next_grapheme_boundary(text, i);
    if (cluster_end <= i)
    {
      UICell bad;
      bad.ch = "?";
      bad.fg = fg;
      bad.bg = bg;
      bad.bold = bold;
      bad.italic = italic;
      bad.reverse = false;
      set_cell(x + cell_offset, y, bad);
      i += 1;
      cell_offset++;
      continue;
    }
    if (cluster_end > (int)text.length())
    {
      break;
    }

    UICell cell;
    cell.ch = ui_sanitized_cell_text(text.substr(i, cluster_end - i));
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = bold;
    cell.italic = italic;
    cell.reverse = false;
    set_cell(x + cell_offset, y, cell);

    int cell_width = rendered_cell_width(cell.ch);
    for (int fill = 1; fill < cell_width && x + cell_offset + fill < width; fill++)
    {
      UICell continuation = cell;
      continuation.ch = "";
      set_cell(x + cell_offset + fill, y, continuation);
    }

    i = cluster_end;
    cell_offset += cell_width;
  }
}

void UI::draw_rect(const UIRect &rect, int fg, int bg)
{
  for (int y = rect.y; y < rect.y + rect.h && y < height; y++)
  {
    for (int x = rect.x; x < rect.x + rect.w && x < width; x++)
    {
      UICell cell;
      cell.ch = " ";
      cell.fg = fg;
      cell.bg = bg;
      cell.bold = false;
      cell.italic = false;
      cell.reverse = false;
      set_cell(x, y, cell);
    }
  }
}

void UI::draw_border(const UIRect &rect, int fg, int bg)
{
  // Defensive clamp: if the caller asked for a right edge that lands
  // in the right-edge render margin (the column the renderer will
  // not paint), pull the border one cell inside so the right side
  // of the border is actually visible. This is defense in depth
  // for any full-width panel that does its own layout.
  UIRect clamped = rect;
  int paint_w = get_render_width();
  if (clamped.x + clamped.w > paint_w)
  {
    clamped.w = paint_w - clamped.x;
    if (clamped.w < 1)
      clamped.w = 1;
  }

  // Top and Bottom
  for (int x = clamped.x; x < clamped.x + clamped.w && x < width; x++)
  {
    UICell cell;
    cell.ch = "─"; // U+2500
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    cell.italic = false;
    cell.reverse = false;

    if (x == clamped.x)
      cell.ch = "┌"; // U+250C
    else if (x == clamped.x + clamped.w - 1)
      cell.ch = "┐"; // U+2510 (Top Right)

    // Draw top
    if (clamped.y >= 0 && clamped.y < height)
      set_cell(x, clamped.y, cell);

    // Prepare bottom corners
    if (x == clamped.x)
      cell.ch = "└"; // U+2514
    else if (x == clamped.x + clamped.w - 1)
      cell.ch = "┘"; // U+2518
    else
      cell.ch = "─";

    // Draw bottom
    if (clamped.y + clamped.h - 1 < height && clamped.y + clamped.h - 1 >= 0)
      set_cell(x, clamped.y + clamped.h - 1, cell);
  }

  // Left and Right (excluding corners which are already drawn)
  for (int y = clamped.y + 1; y < clamped.y + clamped.h - 1 && y < height; y++)
  {
    UICell cell;
    cell.ch = "│"; // U+2502
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    cell.italic = false;
    cell.reverse = false;

    if (clamped.x >= 0 && clamped.x < width)
      set_cell(clamped.x, y, cell);

    if (clamped.x + clamped.w - 1 < width && clamped.x + clamped.w - 1 >= 0)
      set_cell(clamped.x + clamped.w - 1, y, cell);
  }
}

void UI::fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg)
{
  for (int y = rect.y; y < rect.y + rect.h && y < height; y++)
  {
    for (int x = rect.x; x < rect.x + rect.w && x < width; x++)
    {
      UICell cell;
      cell.ch = ch;
      cell.fg = fg;
      cell.bg = bg;
      cell.bold = false;
      cell.italic = false;
      cell.reverse = false;
      set_cell(x, y, cell);
    }
  }
}
