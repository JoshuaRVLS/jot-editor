#include "ui.h"
#include <algorithm>

namespace {
bool is_valid_utf8_sequence(const std::string &s) {
  if (s.empty())
    return false;
  const unsigned char *p = (const unsigned char *)s.data();
  int n = (int)s.size();

  if (n == 1) {
    return (p[0] & 0x80) == 0;
  }
  if (n == 2) {
    if ((p[0] & 0xE0) != 0xC0)
      return false;
    return (p[1] & 0xC0) == 0x80;
  }
  if (n == 3) {
    if ((p[0] & 0xF0) != 0xE0)
      return false;
    return (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80;
  }
  if (n == 4) {
    if ((p[0] & 0xF8) != 0xF0)
      return false;
    return (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
           (p[3] & 0xC0) == 0x80;
  }
  return false;
}

int utf8_char_len(const std::string &text, int i) {
  if (i < 0 || i >= (int)text.size())
    return 0;
  const unsigned char c = (unsigned char)text[i];
  if ((c & 0x80) == 0)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 0;
}

std::string sanitized_cell_text(const std::string &ch) {
  if (ch.empty())
    return " ";
  if (is_valid_utf8_sequence(ch))
    return ch;
  return "?";
}
} // namespace

UI::UI(Terminal *t)
    : term(t), width(80), height(24), cursor_x(-1), cursor_y(-1),
      cursor_hidden(true) {
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++) {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++) {
      grid[y][x] = {" ", default_fg, default_bg, false, false, false};
      last_grid[y][x] = {"", -1, -1, false, false, false}; // Force redraw initially
    }
  }
}

void UI::resize(int w, int h) {
  int new_w = std::max(1, w);
  int new_h = std::max(1, h);
  bool dim_changed = (new_w != width) || (new_h != height);
  width = new_w;
  height = new_h;
  cursor_x = -1;
  cursor_y = -1;
  cursor_hidden = true;
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++) {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++) {
      grid[y][x] = {" ", default_fg, default_bg, false, false, false};
      last_grid[y][x] = {"", -1, -1, false, false, false};
    }
  }
  // Only invalidate (which calls term->clear()) when the dimensions actually
  // changed. The constructor's UI(Terminal*) already fills last_grid with
  // the force-redraw sentinel, so the first resize() right after
  // construction is a no-op from the diff renderer's point of view; the
  // only side effect of calling invalidate() here is one extra ESC[2J
  // when the Editor constructor and Editor::run() each call resize() at
  // startup. Skipping it on dimension-stable resizes removes the duplicate
  // clear and the duplicate full-redraw pass.
  if (dim_changed) {
    invalidate();
  }
}

void UI::invalidate() {
  cursor_x = -1;
  cursor_y = -1;
  cursor_hidden = true;
  term->clear();
}

void UI::clear() {
  for (auto &row : grid) {
    for (auto &cell : row) {
      cell = {" ", default_fg, default_bg, false, false, false};
    }
  }
}

void UI::set_default_colors(int fg, int bg) {
  default_fg = fg;
  default_bg = bg;
}

void UI::set_cell(int x, int y, const UICell &cell) {
  if (x >= 0 && x < width && y >= 0 && y < height) {
    grid[y][x] = cell;
  }
}

UICell UI::get_cell(int x, int y) const {
  if (x >= 0 && x < width && y >= 0 && y < height) {
    return grid[y][x];
  }
  return {" ", 7, 0, false, false, false};
}

void UI::render() {
  // Full-row paint renderer. Every row is written from column 0 with
  // explicit colors and padded so stale terminal content is always
  // overwritten. No diff against last_grid. Autowrap is disabled for the
  // entire frame; one final flush at the end.
  //
  // When JOT_RENDER_CAPTURE_RAW=1, run coalescing is disabled and every
  // cell is written one at a time so the capture log is unambiguous.

  bool capture_on = term->render_capture_enabled();
  bool capture_raw = term->render_capture_raw();

  term->disable_autowrap();

  // Per-frame right-edge safety margin. The renderer must never write the
  // rightmost `margin` physical columns of any row (margin is at least 1).
  // On many terminals, writing the rightmost cell of a row leaves the
  // cursor in a "pending wrap" state; at large widths the next cursor
  // move can be misinterpreted as a wrap, scroll the viewport, and show
  // stale or overlapping text from a previous frame. The last row already
  // has margin 1; we extend that to every row and make the amount
  // configurable via JOT_RENDER_MARGIN=<n>.
  const int margin = term->render_margin();
  const int row_width = std::max(0, width - margin);
  for (int y = 0; y < height; y++) {
    if (row_width <= 0) {
      // Still emit \x1b[K on the previous row's position so any stale
      // content in the (unpaintable) right margin is cleared. This is
      // belt-and-suspenders for terminals that ignore the move_cursor
      // and try to continue from the previous cursor position.
      if (y > 0) {
        term->clear_to_end();
      }
      continue;
    }

    term->move_cursor(0, y);

    if (capture_raw) {
      for (int x = 0; x < row_width; x++) {
        const auto &cell = grid[y][x];
        term->reset_color();
        if (cell.bold)   term->set_bold(true);
        if (cell.italic)  term->set_italic(true);
        if (cell.reverse) term->set_reverse(true);
        term->set_color(cell.fg, cell.bg);
        term->write(sanitized_cell_text(cell.ch));
      }
    } else {
      int run_fg = -1;
      int run_bg = -1;
      bool run_bold = false;
      bool run_italic = false;
      bool run_reverse = false;
      int written = 0;

      for (int x = 0; x < row_width; ) {
        const auto &cell = grid[y][x];

        if (x == 0 ||
            cell.fg != run_fg || cell.bg != run_bg ||
            cell.bold != run_bold || cell.italic != run_italic ||
            cell.reverse != run_reverse) {
          // Optimization: skip ESC[0m (full reset) when only the
          // fg/bg have changed and the bold/italic/reverse bits are
          // still correct. A full reset costs ~5 bytes and reverts
          // background to terminal default, which can flash on
          // terminals with delayed SGR processing. SGR 38;5; and
          // 48;5; are independent of bold/italic/reverse so we can
          // set them in place.
          const bool attrs_unchanged =
              (x != 0) &&
              cell.bold == run_bold && cell.italic == run_italic &&
              cell.reverse == run_reverse &&
              (run_fg != -1 || run_bg != -1);
          if (!attrs_unchanged) {
            term->reset_color();
            if (cell.bold)
              term->set_bold(true);
            if (cell.italic)
              term->set_italic(true);
            if (cell.reverse)
              term->set_reverse(true);
          } else {
            // Only fg/bg changed within the same attribute set.
            // reset_color() is still needed only when transitioning
            // *out* of bold/italic/reverse; otherwise just emit the
            // new fg/bg in place.
            if (cell.bold != run_bold) term->set_bold(cell.bold);
            if (cell.italic != run_italic) term->set_italic(cell.italic);
            if (cell.reverse != run_reverse) term->set_reverse(cell.reverse);
          }
          term->set_color(cell.fg, cell.bg);
          run_fg = cell.fg;
          run_bg = cell.bg;
          run_bold = cell.bold;
          run_italic = cell.italic;
          run_reverse = cell.reverse;
        }

        std::string body;
        int run_start = x;
        while (x < row_width &&
               grid[y][x].fg == run_fg &&
               grid[y][x].bg == run_bg &&
               grid[y][x].bold == run_bold &&
               grid[y][x].italic == run_italic &&
               grid[y][x].reverse == run_reverse) {
          body += sanitized_cell_text(grid[y][x].ch);
          x++;
        }

        term->write(body);
        written += (x - run_start);
      }

      while (written < row_width) {
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
    term->clear_to_end();
  }

  term->reset_color();

  if (cursor_hidden) {
    term->hide_cursor();
  } else {
    int cx = (cursor_x < 0) ? 0 : cursor_x;
    int cy = (cursor_y < 0) ? 0 : cursor_y;
    // Clamp x one cell inside the render margin so the cursor itself is
    // never parked on a right-edge cell. This applies to all widths
    // greater than 1; on a single-column terminal we obviously cannot
    // move the cursor further left.
    const int cursor_max_x = (width > 1) ? (width - margin - 1) : (width - 1);
    if (cx > cursor_max_x) cx = cursor_max_x;
    if (cx < 0) cx = 0;
    if (cy >= height) cy = height - 1;
    if (cy < 0) cy = 0;
    term->move_cursor(cx, cy);
    term->show_cursor();
  }

  term->write("\033[1 q");
  term->flush();

  if (capture_on) {
    char label[64];
    snprintf(label, sizeof(label), "FRAME w=%d h=%d", width, height);
    term->render_capture_marker(label, height);
  }
}

// Store cursor state without emitting terminal writes. render() (or
// flush_cursor() when render() is not called) is responsible for
// materialising the cursor on the terminal.
void UI::set_cursor(int x, int y) {
  if (x >= 0 && x < width && y >= 0 && y < height) {
    cursor_x = x;
    cursor_y = y;
    cursor_hidden = false;
  }
}

void UI::hide_cursor() {
  cursor_hidden = true;
}

// Emit only the current cursor state to the terminal buffer and flush.
// Used by the !needs_redraw path in Editor::render() when the frame's
// grid is unchanged and the only thing that needs updating is the
// blinking cursor / selection caret.
void UI::flush_cursor() {
  term->disable_autowrap();
  const int margin = term->render_margin();
  if (cursor_hidden) {
    term->hide_cursor();
  } else {
    int cx = (cursor_x < 0) ? 0 : cursor_x;
    int cy = (cursor_y < 0) ? 0 : cursor_y;
    // Same right-edge clamp as render(): never park the cursor on the
    // rightmost cell the renderer leaves untouched.
    const int cursor_max_x = (width > 1) ? (width - margin - 1) : (width - 1);
    if (cx > cursor_max_x) cx = cursor_max_x;
    if (cx < 0) cx = 0;
    if (cy >= height) cy = height - 1;
    if (cy < 0) cy = 0;
    term->move_cursor(cx, cy);
    term->show_cursor();
  }
  term->write("\033[1 q");
  term->flush();

  if (term->render_capture_enabled()) {
    term->render_capture_marker("CURSOR-ONLY", 0);
  }
}

void UI::draw_text(int x, int y, const std::string &text, int fg, int bg,
                   bool bold, bool italic) {
  // Guard against invisible normal text: if the caller used the default-bg
  // path (bg < 0) and the requested foreground would match the background,
  // substitute the default foreground so editor text is always readable.
  // This protects default/editor text paths from theme misconfiguration
  // (fg_default == bg_default) without overriding intentional styling
  // where both fg and bg are explicitly set (e.g., selection highlights).
  bool used_default_bg = (bg < 0);
  if (used_default_bg) bg = default_bg;
  if (used_default_bg && fg == default_bg) {
    fg = default_fg;
  }
  int i = 0;
  int cell_offset = 0;
  while (i < (int)text.length() && x + cell_offset < width) {
    int char_len = utf8_char_len(text, i);
    if (char_len <= 0) {
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
    if (i + char_len > (int)text.length()) {
      break;
    }

    UICell cell;
    cell.ch = sanitized_cell_text(text.substr(i, char_len));
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = bold;
    cell.italic = italic;
    cell.reverse = false;
    set_cell(x + cell_offset, y, cell);

    i += char_len;
    cell_offset++;
  }
}

void UI::draw_rect(const UIRect &rect, int fg, int bg) {
  for (int y = rect.y; y < rect.y + rect.h && y < height; y++) {
    for (int x = rect.x; x < rect.x + rect.w && x < width; x++) {
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

void UI::draw_border(const UIRect &rect, int fg, int bg) {
  // Top and Bottom
  for (int x = rect.x; x < rect.x + rect.w && x < width; x++) {
    UICell cell;
    cell.ch = "─"; // U+2500
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    cell.italic = false;
    cell.reverse = false;

    if (x == rect.x)
      cell.ch = "┌"; // U+250C
    else if (x == rect.x + rect.w - 1)
      cell.ch = "┐"; // U+2510 (Top Right)

    // Draw top
    if (rect.y >= 0 && rect.y < height)
      set_cell(x, rect.y, cell);

    // Prepare bottom corners
    if (x == rect.x)
      cell.ch = "└"; // U+2514
    else if (x == rect.x + rect.w - 1)
      cell.ch = "┘"; // U+2518
    else
      cell.ch = "─";

    // Draw bottom
    if (rect.y + rect.h - 1 < height && rect.y + rect.h - 1 >= 0)
      set_cell(x, rect.y + rect.h - 1, cell);
  }

  // Left and Right (excluding corners which are already drawn)
  for (int y = rect.y + 1; y < rect.y + rect.h - 1 && y < height; y++) {
    UICell cell;
    cell.ch = "│"; // U+2502
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = false;
    cell.italic = false;
    cell.reverse = false;

    if (rect.x >= 0 && rect.x < width)
      set_cell(rect.x, y, cell);

    if (rect.x + rect.w - 1 < width && rect.x + rect.w - 1 >= 0)
      set_cell(rect.x + rect.w - 1, y, cell);
  }
}

void UI::fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg) {
  for (int y = rect.y; y < rect.y + rect.h && y < height; y++) {
    for (int x = rect.x; x < rect.x + rect.w && x < width; x++) {
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
