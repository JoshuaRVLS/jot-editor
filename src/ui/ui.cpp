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

  // Dims an xterm-256 palette index by scaling its RGB toward a fraction of
  // itself (keeping the hue) instead of hard-stepping channels, so the scrim
  // reads as a subtle darker backdrop rather than blacking the theme out.
  // The background darkening is what actually carries the effect: SGR 2 only
  // affects the foreground on most terminals, and Windows Terminal's conpty
  // path drops the faint attribute entirely. Foreground dimming borrows the
  // background scale — with a very dark result the renderer can skip SGR 2.
  // The default background (-1) is mapped to a dark gray, not black, so the
  // theme doesn't collapse.
  constexpr int ui_dim_rgb_scale_pct = 55;

  // xterm-256 index → RGB. Named (not a lambda) so both ui_dim_color and the
  // darkness probe below can use it.
  struct XtermPalette
  {
    static void decode(int i, int rgb[3])
    {
      if (i < 0)
      {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
      }
      if (i < 16)
      {
        static const int base[8][3] = {
            {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
            {0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192}};
        rgb[0] = base[i & 7][0];
        rgb[1] = base[i & 7][1];
        rgb[2] = base[i & 7][2];
        if (i >= 8)
        {
          rgb[0] += 64;
          rgb[1] += 64;
          rgb[2] += 64;
        }
        return;
      }
      if (i >= 232)
      {
        const int g = 8 + (i - 232) * 10;
        rgb[0] = rgb[1] = rgb[2] = g;
        return;
      }
      const int v = i - 16;
      static const int levels[6] = {0, 95, 135, 175, 215, 255};
      rgb[0] = levels[v / 36];
      rgb[1] = levels[(v % 36) / 6];
      rgb[2] = levels[v % 6];
    }
  };

  int ui_dim_color(int idx, bool is_bg)
  {
    auto nearest_index = [](int r, int g, int b)
    {
      int best = 0;
      long long best_d = (long long)1 << 62;
      for (int i = 0; i < 256; i++)
      {
        int rgb[3];
        XtermPalette::decode(i, rgb);
        const long long dr = (long long)r - rgb[0];
        const long long dg = (long long)g - rgb[1];
        const long long db = (long long)b - rgb[2];
        const long long d = dr * dr + dg * dg + db * db;
        if (d < best_d)
        {
          best_d = d;
          best = i;
        }
      }
      return best;
    };
    if (idx < 0)
    {
      if (!is_bg)
      {
        return idx;
      }
      // Default background: a muted dark gray, not black, so the theme frame
      // stays visible behind the scrim.
      return 233;
    }
    int rgb[3];
    XtermPalette::decode(idx, rgb);
    const int r = (int)(((long long)rgb[0] * ui_dim_rgb_scale_pct) / 100);
    const int g = (int)(((long long)rgb[1] * ui_dim_rgb_scale_pct) / 100);
    const int b = (int)(((long long)rgb[2] * ui_dim_rgb_scale_pct) / 100);
    return nearest_index(r, g, b);
  }

  // Whether a dimmed cell looks dimmed from its colors alone, without the
  // SGR 2 faint attribute. Used so the modal scrim still reads on terminals
  // whose conpty path drops faint (Windows Terminal): if the dimmed
  // background is still relatively bright, SGR 2 is needed for the
  // foreground; if the scrim is already very dark, the background carries
  // the effect and SGR 2 can stay off (it is optional in most terminals and
  // ignored in conpty entirely).
  bool ui_dim_reads_dark(int dimmed_bg_idx)
  {
    if (dimmed_bg_idx == 233 || dimmed_bg_idx == 232 || dimmed_bg_idx == 16
        || dimmed_bg_idx == 0)
    {
      return true;
    }
    int rgb[3] = {0, 0, 0};
    XtermPalette::decode(dimmed_bg_idx, rgb);
    const int lum = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000;
    return lum < 40;
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
  // Cell-diffing renderer. The draw layer repaints the whole grid every
  // frame (immediate mode), so render() compares each row against
  // last_grid -- the frame that was actually written to the terminal.
  // Rows whose content is unchanged are skipped entirely; rows that did
  // change are diffed at cell granularity (emit_row_diff) and only the
  // changed runs are written, with a cheap cursor move per run. A
  // typical typing frame then writes a few short SGR runs instead of
  // whole rows, cutting per-frame terminal output by 90-99% on big
  // buffers. That output volume is what makes typing/scrolling feel
  // laggy on I/O-bound terminals: thousands of SGR sequences must be
  // parsed by the terminal emulator for every keystroke.
  //
  // Skipping is safe because a skipped cell is byte-identical to what
  // the terminal already shows: nothing wrote to it since it was
  // painted (it is compared cell-by-cell against the retained copy), and
  // every full paint pads the row to full width and erases its right
  // margin, so no stale content can linger. Capture modes
  // (JOT_RENDER_CAPTURE*) force a full paint of every row so their logs
  // stay unambiguous; capture raw also disables run coalescing so every
  // cell is written one at a time.

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

    // Full-row painters position the cursor themselves at row start;
    // emit_row_diff() moves the cursor per changed run.
    if (capture_raw)
    {
      term->move_cursor(0, y);
      for (int x = 0; x < row_width;)
      {
        const auto &cell = grid[y][x];
        term->reset_color();
        if (cell.bold)
          term->set_bold(true);
        if (cell.italic)
          term->set_italic(true);
        // SGR 2 (faint) is optional in most terminals and dropped by
        // Windows Terminal's conpty path: when the dimmed background alone
        // already reads dark, the scrim carries the effect and faint is not
        // needed for the foreground. Emit it only when the colors alone
        // would not look dimmed.
        if (cell.dim
            && !ui_dim_reads_dark(ui_dim_color(cell.bg, true)))
        {
          term->set_dim(true);
        }
        if (cell.reverse)
          term->set_reverse(true);
        if (cell.underline)
          term->set_underline(cell.underline);
        if (cell.underline_fg != -1)
          term->set_underline_color(cell.underline_fg);
        term->set_color(cell.dim ? ui_dim_color(cell.fg, false) : cell.fg,
                        cell.dim ? ui_dim_color(cell.bg, true) : cell.bg);
        write_cell_for_remaining_width(term, cell.ch, row_width - x);
        x += std::min(rendered_cell_width(cell.ch), row_width - x);
      }
    }
    else if (paint_all)
    {
      // Full repaint (capture mode / periodic self-heal): the terminal
      // state cannot be assumed, so the row is written out completely.
      emit_full_row(y, row_width);
    }
    else
    {
      // Normal frame: the row differs from last_grid but only in a few
      // runs -- emit exactly those instead of the whole row.
      emit_row_diff(y, row_width);
    }

    // Erase any leftover content from the previous frame that might
    // still occupy the margin columns (the `width - margin` columns
    // we deliberately left untouched). \x1b[K (EL 0) erases from the
    // cursor to end of line, so it covers the margin and any
    // characters past the row's last painted cell. With autowrap
    // disabled, the cursor stays at the end of the written text and
    // the erase is bounded to the current row.
    // Move past the painted cells before erasing the untouched margin.
    // Full-row paints only: a diff row leaves the unchanged tail (and
    // the never-painted margin) exactly as the terminal already shows
    // them, so erasing would be redundant work.
    if (capture_raw || paint_all)
    {
      term->move_cursor(row_width, y);
      term->clear_to_end();
    }

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
  }

  term->write(cursor_shape_sequence(cursor_shape));
  term->flush();
  cursor_dirty = false;

  if (capture_on)
  {
    char label[64];
    snprintf(label, sizeof(label), "FRAME w=%d h=%d", width, height);
    term->render_capture_marker(label, height);
  }
}

void UI::emit_full_row(int y, int row_width)
{
  term->move_cursor(0, y);

  int run_fg = -1;
  int run_bg = -1;
  bool run_bold = false;
  bool run_italic = false;
  bool run_dim = false;
  bool run_reverse = false;
  int run_underline = 0;
  int run_underline_fg = -1;
  int written = 0;

  std::string body;
  body.reserve((size_t)row_width);

  for (int x = 0; x < row_width;)
  {
    const auto &cell = grid[y][x];

    if (x == 0 || cell.fg != run_fg || cell.bg != run_bg || cell.bold != run_bold
        || cell.italic != run_italic || cell.dim != run_dim || cell.reverse != run_reverse
        || cell.underline != run_underline || cell.underline_fg != run_underline_fg)
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
          && cell.reverse == run_reverse && cell.underline == run_underline
          && cell.underline_fg == run_underline_fg && (run_fg != -1 || run_bg != -1);
      if (!attrs_unchanged)
      {
        term->reset_color();
        if (cell.bold)
          term->set_bold(true);
        if (cell.italic)
          term->set_italic(true);
        // As above: skip SGR 2 when the dimmed background alone already
        // reads dark (conpty drops faint anyway).
        if (cell.dim
            && !ui_dim_reads_dark(ui_dim_color(cell.bg, true)))
        {
          term->set_dim(true);
        }
        if (cell.reverse)
          term->set_reverse(true);
        if (cell.underline)
          term->set_underline(cell.underline);
        if (cell.underline_fg != -1)
          term->set_underline_color(cell.underline_fg);
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
        {
          if (cell.dim && !ui_dim_reads_dark(ui_dim_color(cell.bg, true)))
          {
            term->set_dim(true);
          }
          else
          {
            term->set_dim(cell.dim);
          }
        }
        if (cell.reverse != run_reverse)
          term->set_reverse(cell.reverse);
        if (cell.underline != run_underline)
          term->set_underline(cell.underline);
        if (cell.underline_fg != run_underline_fg)
          term->set_underline_color(cell.underline_fg);
      }
      // `cell.*` hold the values that just changed; `run_*` still hold the
      // previous run's colors here, so emit from the cell.
      term->set_color(cell.dim ? ui_dim_color(cell.fg, false) : cell.fg,
                      cell.dim ? ui_dim_color(cell.bg, true) : cell.bg);
      run_fg = cell.fg;
      run_bg = cell.bg;
      run_bold = cell.bold;
      run_italic = cell.italic;
      run_dim = cell.dim;
      run_reverse = cell.reverse;
      run_underline = cell.underline;
      run_underline_fg = cell.underline_fg;
    }

    body.clear();

    int run_start = x;
    while (x < row_width && grid[y][x].fg == run_fg && grid[y][x].bg == run_bg
           && grid[y][x].bold == run_bold && grid[y][x].italic == run_italic
           && grid[y][x].dim == run_dim && grid[y][x].reverse == run_reverse
           && grid[y][x].underline == run_underline
           && grid[y][x].underline_fg == run_underline_fg)
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

void UI::emit_row_diff(int y, int row_width)
{
  const auto &cur = grid[y];
  const auto &prev = last_grid[y];

  // Collect the columns whose cells differ from the frame the terminal
  // already shows.
  std::vector<int> changed;
  changed.reserve(32);
  for (int x = 0; x < row_width; x++)
  {
    if (cur[x] != prev[x])
      changed.push_back(x);
  }
  if (changed.empty())
    return;

  // Merge changed columns into runs: unchanged gaps of up to kMergeGap
  // cells are absorbed into the surrounding run -- cheaper to rewrite a
  // few identical cells than to pay a cursor move + SGR for a separate
  // run.
  constexpr int kMergeGap = 4;
  struct Run
  {
    int start;
    int end;
  };
  std::vector<Run> runs;
  runs.reserve(changed.size());
  int run_start = changed[0];
  int run_end = changed[0] + 1;
  for (size_t i = 1; i < changed.size(); i++)
  {
    const int c = changed[i];
    if (c <= run_end + kMergeGap)
      run_end = c + 1;
    else
    {
      runs.push_back({run_start, run_end});
      run_start = c;
      run_end = c + 1;
    }
  }
  runs.push_back({run_start, run_end});

  // Wide-char fixups: never start a run on a continuation cell (include
  // its lead), never end a run right before a continuation cell (lead and
  // continuation are written as one unit), then collapse overlaps between
  // adjacent runs after the fixups.
  for (auto &r : runs)
  {
    while (r.start > 0 && cur[r.start].ch.empty())
      r.start--;
    while (r.end < row_width && cur[r.end].ch.empty())
      r.end++;
  }
  std::vector<Run> merged;
  merged.reserve(runs.size());
  for (const auto &r : runs)
  {
    if (!merged.empty() && r.start <= merged.back().end)
      merged.back().end = std::max(merged.back().end, r.end);
    else
      merged.push_back(r);
  }

  // Style of the last emitted group in this row. The first group of the
  // row always issues a full reset -- after the previous row the
  // terminal's SGR state is unknown -- and later groups skip the reset
  // when only the colors changed, mirroring emit_full_row().
  int run_fg = -1;
  int run_bg = -1;
  bool run_bold = false;
  bool run_italic = false;
  bool run_dim = false;
  bool run_reverse = false;
  int run_underline = 0;
  int run_underline_fg = -1;
  // Terminal column the cursor sits at after the previous emit in this
  // row; used to skip or shorten cursor moves between runs.
  int last_x = 0;
  bool first_run = true;

  for (const auto &r : merged)
  {
    // Position the cursor. The first run of every row needs an absolute
    // move; later runs are reached with a cheap relative move when the
    // gap is small.
    if (first_run)
    {
      term->move_cursor(r.start, y);
    }
    else if (r.start == last_x)
    {
      // The previous write already left the cursor here.
    }
    else if (r.start > last_x && r.start - last_x <= 64)
    {
      term->write("\x1b[" + std::to_string(r.start - last_x) + "C");
    }
    else if (r.start < last_x && last_x - r.start <= 64)
    {
      term->write("\x1b[" + std::to_string(last_x - r.start) + "D");
    }
    else
    {
      term->move_cursor(r.start, y);
    }
    // `first_run` is cleared below by the first style group's SGR emit;
    // positioning only consults it to force an absolute move for the
    // row's first run.

    // The merged run may span several styles (adjacent changed cells
    // with different colors); break it into style-homogeneous groups,
    // emitting SGR only at group boundaries.
    int x = r.start;
    while (x < r.end)
    {
      const auto &cell = cur[x];

      if (first_run)
      {
        // First group of the row: the terminal's SGR state after the
        // previous row is unknown, so always start from a clean slate.
        term->reset_color();
        if (cell.bold)
          term->set_bold(true);
        if (cell.italic)
          term->set_italic(true);
        // As above: skip SGR 2 when the dimmed background alone already
        // reads dark (conpty drops faint anyway).
        if (cell.dim
            && !ui_dim_reads_dark(ui_dim_color(cell.bg, true)))
        {
          term->set_dim(true);
        }
        if (cell.reverse)
          term->set_reverse(true);
        if (cell.underline)
          term->set_underline(cell.underline);
        if (cell.underline_fg != -1)
          term->set_underline_color(cell.underline_fg);
        term->set_color(cell.dim ? ui_dim_color(cell.fg, false) : cell.fg,
                        cell.dim ? ui_dim_color(cell.bg, true) : cell.bg);
        first_run = false;
      }
      else
      {
        const bool attrs_same = cell.bold == run_bold && cell.italic == run_italic
                                && cell.dim == run_dim && cell.reverse == run_reverse
                                && cell.underline == run_underline
                                && cell.underline_fg == run_underline_fg;
        if (attrs_same && cell.fg == run_fg && cell.bg == run_bg)
        {
          // Same style as the previous group: no SGR needed, the
          // terminal state already matches.
        }
        else if (attrs_same && (run_fg != -1 || run_bg != -1))
        {
          // Only fg/bg changed: set them in place without a full reset
          // (a reset costs bytes and can flash the background).
          term->set_color(cell.dim ? ui_dim_color(cell.fg, false) : cell.fg,
                          cell.dim ? ui_dim_color(cell.bg, true) : cell.bg);
        }
        else
        {
          term->reset_color();
          if (cell.bold)
            term->set_bold(true);
          if (cell.italic)
            term->set_italic(true);
          // As above: skip SGR 2 when the dimmed background alone already
          // reads dark (conpty drops faint anyway).
          if (cell.dim
              && !ui_dim_reads_dark(ui_dim_color(cell.bg, true)))
          {
            term->set_dim(true);
          }
          if (cell.reverse)
            term->set_reverse(true);
          if (cell.underline)
            term->set_underline(cell.underline);
          if (cell.underline_fg != -1)
            term->set_underline_color(cell.underline_fg);
          term->set_color(cell.dim ? ui_dim_color(cell.fg, false) : cell.fg,
                          cell.dim ? ui_dim_color(cell.bg, true) : cell.bg);
        }
      }
      run_fg = cell.fg;
      run_bg = cell.bg;
      run_bold = cell.bold;
      run_italic = cell.italic;
      run_dim = cell.dim;
      run_reverse = cell.reverse;
      run_underline = cell.underline;
      run_underline_fg = cell.underline_fg;

      std::string body;
      body.reserve((size_t)(r.end - x));
      while (x < r.end)
      {
        const auto &c = cur[x];
        if (c.fg != cell.fg || c.bg != cell.bg || c.bold != cell.bold
            || c.italic != cell.italic || c.dim != cell.dim || c.reverse != cell.reverse
            || c.underline != cell.underline || c.underline_fg != cell.underline_fg)
        {
          break;
        }
        const std::string &ch = c.ch;
        if (ch.empty())
        {
          // Defensive: an isolated continuation cell (its lead was not
          // part of this run) is rewritten as a blank column so the
          // cursor position stays exact.
          body.push_back(' ');
          x += 1;
          continue;
        }
        const int cell_w = std::min(rendered_cell_width(ch), r.end - x);
        append_cell_for_remaining_width(body, ch, r.end - x);
        x += cell_w;
      }
      term->write(body);
    }
    last_x = x;
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

// Applies the current software-blink visibility to the terminal cursor.
// The editor drives one blink clock for the terminal cursor and the
// extra-caret highlights, so everything blinks in phase instead of the
// terminal's own unsynchronized blink timer.
void UI::set_cursor_blink_visible(bool visible)
{
  if (visible != cursor_blink_visible)
  {
    cursor_blink_visible = visible;
    cursor_dirty = true;
  }
}

std::string UI::cursor_shape_sequence(UICursorShape shape)
{
  if (!cursor_blink_visible)
  {
    return "\033[?25l";
  }
  // Always the steady DECSCUSR: blinking is done in software (DECTCEM
  // show/hide on the blink clock), so the terminal's own blink phase can
  // never drift out of sync with the extra-caret highlights.
  if (shape == UICursorShape::Bar)
  {
    return "\033[?25h\033[3 q";
  }
  return "\033[?25h\033[2 q";
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
  }
  term->write(cursor_shape_sequence(cursor_shape));
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

void UI::draw_text(int x,
                   int y,
                   const std::string &text,
                   int fg,
                   int bg,
                   bool bold,
                   bool italic,
                   int underline,
                   int underline_fg)
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
      bad.underline = underline;
      bad.underline_fg = underline_fg;
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
    cell.underline = underline;
    cell.underline_fg = underline_fg;
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
