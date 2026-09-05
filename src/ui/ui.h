#ifndef UI_H
#define UI_H

#include "terminal.h"
#include <functional>
#include <string>
#include <vector>

struct UIRect
{
  int x, y, w, h;
};

struct UICell
{
  std::string ch = " ";
  int fg = 7;
  int bg = 0;
  bool bold = false;
  bool italic = false;
  bool reverse = false;
  bool dim = false;

  bool operator==(const UICell &other) const
  {
    return ch == other.ch && fg == other.fg && bg == other.bg && bold == other.bold
           && italic == other.italic && reverse == other.reverse && dim == other.dim;
  }
  bool operator!=(const UICell &other) const
  {
    return !(*this == other);
  }
};

enum class UICursorShape
{
  Block,
  Bar,
};

class UI
{
private:
  Terminal *term;
  std::vector<std::vector<UICell>> grid;
  // Per-row dirty flags: set whenever a cell in the row is modified between
  // frames, cleared when render() processes the row. A dirty row is only a
  // candidate -- render() still compares it against last_grid (below)
  // before emitting anything. All grid writes go through
  // set_cell()/dim_rect(), which maintain this vector; bulk operations
  // (constructor, resize, clear, invalidate) mark every row dirty so the
  // next frame is a full repaint.
  std::vector<unsigned char> row_dirty;
  // Retained copy of the last frame actually written to the terminal.
  // The draw layer rewrites the whole grid every frame (immediate mode),
  // so per-row dirty flags alone would mark every row dirty. render()
  // instead compares each dirty row against last_grid and skips the row's
  // terminal write when the content is identical -- typing/scrolling then
  // emits only the rows that genuinely changed instead of the whole
  // screen.
  std::vector<std::vector<UICell>> last_grid;
  // Renders since the last full-screen paint. Terminals can occasionally
  // drop or garble a row's bytes mid-frame; since an unchanged row is now
  // left untouched, a periodic full repaint bounds any such artifact's
  // lifetime. Initialised to the threshold so the very first frame is a
  // full paint (the terminal may show stale content from whatever ran
  // before the editor).
  int renders_since_full_paint_ = 90;
  int width, height;
  int cursor_x, cursor_y;
  UICursorShape cursor_shape;
  bool cursor_hidden;
  bool cursor_dirty = true;
  int default_fg = 7;
  int default_bg = 0;

  void set_cell(int x, int y, const UICell &cell);
  void mark_all_rows_dirty();

public:
  UI(Terminal *t);
  void resize(int w, int h);
  void invalidate();

  void clear();
  void render();
  void emit_raw_after_frame(const std::string &bytes);

  void set_default_colors(int fg, int bg);

  void draw_text(int x,
                 int y,
                 const std::string &text,
                 int fg = 7,
                 int bg = -1,
                 bool bold = false,
                 bool italic = false);
  void draw_rect(const UIRect &rect, int fg, int bg);
  void draw_border(const UIRect &rect, int fg, int bg);
  void fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg);
  void dim_rect(const UIRect &rect);

  // Store cursor position/visibility, no terminal writes. render() emits
  // the cursor at frame-end; flush_cursor() emits it for idle frames.
  void set_cursor(int x, int y, UICursorShape shape = UICursorShape::Block);
  void hide_cursor();
  void reset_cursor_state();
  void flush_cursor();
  bool cursor_needs_flush() const
  {
    return cursor_dirty;
  }

  int get_width() const
  {
    return width;
  }
  int get_height() const
  {
    return height;
  }
  const UICell *cell_at(int x, int y) const;

  // Paintable width: the physical terminal width minus the fixed one-cell
  // right-edge safety margin. The renderer intentionally leaves the
  // rightmost physical column untouched to avoid the terminal's
  // pending-wrap state at large widths. Layout code
  // that places visible UI edges (pane borders, status line,
  // integrated terminal panel, image viewer) must use this width
  // instead of `get_width()` so the right border lands inside the
  // paintable area. The margin is invisible to mouse clicks too:
  // the cursor clamp in render()/flush_cursor() already parks the
  // cursor one cell inside, and mouse click handlers ignore
  // positions beyond the rightmost paintable column.
  int get_render_width() const
  {
    int m = term ? term->render_margin() : 0;
    if (m < 0)
      m = 0;
    int w = width - m;
    return w < 1 ? 1 : w;
  }
};

#endif
