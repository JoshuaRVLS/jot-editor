#ifndef UI_H
#define UI_H

#include "terminal.h"
#include <functional>
#include <string>
#include <vector>

struct UIRect {
  int x, y, w, h;
};

struct UICell {
  std::string ch;
  int fg;
  int bg;
  bool bold;
  bool italic;
  bool reverse;

  bool operator==(const UICell &other) const {
    return ch == other.ch && fg == other.fg && bg == other.bg &&
           bold == other.bold && italic == other.italic &&
           reverse == other.reverse;
  }
  bool operator!=(const UICell &other) const { return !(*this == other); }
};

class UI {
private:
  Terminal *term;
  std::vector<std::vector<UICell>> grid;
  std::vector<std::vector<UICell>> last_grid;
  int width, height;
  int cursor_x, cursor_y;
  bool cursor_hidden;

  void set_cell(int x, int y, const UICell &cell);
  UICell get_cell(int x, int y) const;

public:
  UI(Terminal *t);
  void resize(int w, int h);
  void invalidate();

  void clear();
  void render();

  void draw_text(int x, int y, const std::string &text, int fg = 7, int bg = 0,
                 bool bold = false, bool italic = false);
  void draw_rect(const UIRect &rect, int fg, int bg);
  void draw_border(const UIRect &rect, int fg, int bg);
  void fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg);

  void set_cursor(int x, int y);
  void hide_cursor();

  int get_width() const { return width; }
  int get_height() const { return height; }
};

#endif
