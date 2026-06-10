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
  int default_fg = 7;
  int default_bg = 0;

  void set_cell(int x, int y, const UICell &cell);
  UICell get_cell(int x, int y) const;

public:
  UI(Terminal *t);
  void resize(int w, int h);
  void invalidate();

  void clear();
  void render();

  void set_default_colors(int fg, int bg);

  void draw_text(int x, int y, const std::string &text, int fg = 7, int bg = -1,
                 bool bold = false, bool italic = false);
  void draw_rect(const UIRect &rect, int fg, int bg);
  void draw_border(const UIRect &rect, int fg, int bg);
  void fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg);

  // Store cursor position/visibility, no terminal writes. render() emits
  // the cursor at frame-end; flush_cursor() emits it for idle frames.
  void set_cursor(int x, int y);
  void hide_cursor();
  void flush_cursor();

  int get_width() const { return width; }
  int get_height() const { return height; }

  // Paintable width: the physical terminal width minus the right-edge
  // safety margin. The renderer intentionally leaves the rightmost
  // `render_margin()` columns untouched (default 1) to avoid the
  // terminal's pending-wrap state at large widths. Layout code
  // that places visible UI edges (pane borders, status line,
  // integrated terminal panel, image viewer) must use this width
  // instead of `get_width()` so the right border lands inside the
  // paintable area. The margin is invisible to mouse clicks too:
  // the cursor clamp in render()/flush_cursor() already parks the
  // cursor one cell inside, and mouse click handlers ignore
  // positions beyond the rightmost paintable column.
  int get_render_width() const {
    int m = term ? term->render_margin() : 0;
    if (m < 0) m = 0;
    int w = width - m;
    return w < 1 ? 1 : w;
  }
};

#endif
