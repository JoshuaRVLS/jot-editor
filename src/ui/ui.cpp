#include "ui.h"
#include <algorithm>

UI::UI(Terminal *t) : term(t), width(80), height(24) {
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++) {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++) {
      grid[y][x] = {" ", 7, 0, false, false};
      last_grid[y][x] = {"", -1, -1, false, false}; // Force redraw initially
    }
  }
}

void UI::resize(int w, int h) {
  width = std::max(1, w);
  height = std::max(1, h);
  grid.resize(height);
  last_grid.resize(height);
  for (int y = 0; y < height; y++) {
    grid[y].resize(width);
    last_grid[y].resize(width);
    for (int x = 0; x < width; x++) {
      grid[y][x] = {" ", 7, 0, false, false};
      last_grid[y][x] = {"", -1, -1, false, false};
    }
  }
  term->clear(); // Clear only on resize
}

void UI::invalidate() {
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      last_grid[y][x] = {"", -1, -1, false, false};
    }
  }
  term->clear();
}

void UI::clear() {
  for (auto &row : grid) {
    for (auto &cell : row) {
      cell = {" ", 7, 0, false, false};
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
  return {" ", 7, 0, false, false};
}

void UI::render() {
  // Remove full clear to prevent blinking
  // term->clear();

  int last_fg = -1, last_bg = -1;
  bool last_bold = false, last_reverse = false;
  int cursor_x = -1, cursor_y = -1;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const auto &cell = grid[y][x];

      // Only draw if changed
      if (cell != last_grid[y][x]) {
        if (cursor_y != y || cursor_x != x) {
          term->move_cursor(x, y);
          cursor_x = x;
          cursor_y = y;
        }

        if (cell.fg != last_fg || cell.bg != last_bg ||
            cell.bold != last_bold || cell.reverse != last_reverse) {
          term->reset_color();
          if (cell.bold)
            term->set_bold(true);
          if (cell.reverse)
            term->set_reverse(true);
          term->set_color(cell.fg, cell.bg);
          last_fg = cell.fg;
          last_bg = cell.bg;
          last_bold = cell.bold;
          last_reverse = cell.reverse;
        }

        term->write(cell.ch);
        cursor_x +=
            1; // Assuming single width char for now, but utf8 might differ

        last_grid[y][x] = cell;
      }
    }
  }

  term->reset_color();
  term->flush();
}

void UI::draw_text(int x, int y, const std::string &text, int fg, int bg,
                   bool bold) {
  int i = 0;
  int cell_offset = 0;
  while (i < (int)text.length() && x + cell_offset < width) {
    unsigned char c = text[i];
    int char_len = 1;
    if ((c & 0x80) == 0)
      char_len = 1;
    else if ((c & 0xE0) == 0xC0)
      char_len = 2;
    else if ((c & 0xF0) == 0xE0)
      char_len = 3;
    else if ((c & 0xF8) == 0xF0)
      char_len = 4;

    if (i + char_len > (int)text.length())
      break;

    UICell cell;
    cell.ch = text.substr(i, char_len);
    cell.fg = fg;
    cell.bg = bg;
    cell.bold = bold;
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
      cell.reverse = false;
      set_cell(x, y, cell);
    }
  }
}

void UI::set_cursor(int x, int y) {
  if (x >= 0 && x < width && y >= 0 && y < height) {
    term->move_cursor(x, y);
    term->show_cursor();
    term->flush(); // Ensure it takes effect
  }
}

void UI::hide_cursor() {
  term->hide_cursor();
  term->flush();
}
