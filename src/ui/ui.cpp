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
      grid[y][x] = {" ", 7, 0, false, false, false};
      last_grid[y][x] = {"", -1, -1, false, false, false}; // Force redraw initially
    }
  }
}

void UI::resize(int w, int h) {
  width = std::max(1, w);
  height = std::max(1, h);
  cursor_x = -1;
  cursor_y = -1;
  cursor_hidden = true;
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++) {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++) {
      grid[y][x] = {" ", 7, 0, false, false, false};
      last_grid[y][x] = {"", -1, -1, false, false, false};
    }
  }
  term->clear(); // Clear only on resize
}

void UI::invalidate() {
  cursor_x = -1;
  cursor_y = -1;
  cursor_hidden = true;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      last_grid[y][x] = {"", -1, -1, false, false, false};
    }
  }
  term->clear();
}

void UI::clear() {
  for (auto &row : grid) {
    for (auto &cell : row) {
      cell = {" ", 7, 0, false, false, false};
    }
  }
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
  // Remove full clear to prevent blinking
  // term->clear();

  int last_fg = -1, last_bg = -1;
  bool last_bold = false, last_italic = false, last_reverse = false;
  int draw_cursor_x = -1, draw_cursor_y = -1;

  for (int y = 0; y < height; y++) {
    int x = 0;
    while (x < width) {
      const auto &cell = grid[y][x];
      if (cell == last_grid[y][x]) {
        x++;
        continue;
      }

      if (draw_cursor_y != y || draw_cursor_x != x) {
        term->move_cursor(x, y);
        draw_cursor_x = x;
        draw_cursor_y = y;
      }

      if (cell.fg != last_fg || cell.bg != last_bg || cell.bold != last_bold ||
          cell.italic != last_italic || cell.reverse != last_reverse) {
        term->reset_color();
        if (cell.bold)
          term->set_bold(true);
        if (cell.italic)
          term->set_italic(true);
        if (cell.reverse)
          term->set_reverse(true);
        term->set_color(cell.fg, cell.bg);
        last_fg = cell.fg;
        last_bg = cell.bg;
        last_bold = cell.bold;
        last_italic = cell.italic;
        last_reverse = cell.reverse;
      }

      std::string run;
      int run_start = x;
      int run_end = x;
      for (; run_end < width; run_end++) {
        const auto &rc = grid[y][run_end];
        if (rc == last_grid[y][run_end])
          break;
        if (rc.fg != cell.fg || rc.bg != cell.bg || rc.bold != cell.bold ||
            rc.italic != cell.italic || rc.reverse != cell.reverse) {
          break;
        }
        run += sanitized_cell_text(rc.ch);
      }

      if (run.empty()) {
        run = sanitized_cell_text(cell.ch);
        run_end = x + 1;
      }

      term->write(run);
      draw_cursor_x += (run_end - run_start);

      for (int i = run_start; i < run_end; i++) {
        last_grid[y][i] = grid[y][i];
      }
      x = run_end;
    }
  }

  term->reset_color();
  term->flush();
}

void UI::draw_text(int x, int y, const std::string &text, int fg, int bg,
                   bool bold, bool italic) {
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

void UI::set_cursor(int x, int y) {
  if (x >= 0 && x < width && y >= 0 && y < height) {
    // Always reposition cursor after a frame. Rendering moves terminal cursor
    // while diff-drawing cells, so cached coordinates may be stale even when
    // logical cursor position is unchanged.
    term->move_cursor(x, y);
    term->show_cursor();
    cursor_x = x;
    cursor_y = y;
    cursor_hidden = false;
  }
}

void UI::hide_cursor() {
  if (cursor_hidden) {
    return;
  }
  term->hide_cursor();
  cursor_hidden = true;
}
