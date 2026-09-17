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

// Which sides of a box to ink. Chrome that sits against another region draws
// only the edge facing it (see src/render/pane_edges.h), so a separator between
// two regions is one line rather than two adjacent ones. The default inks all
// four sides, which is what a float over buffer content wants.
struct UIBorderEdges
{
  bool top = true;
  bool right = true;
  bool bottom = true;
  bool left = true;
  // The horizontal line continues past this corner, because a region on that
  // side draws its own bottom edge on the same row. The corner is then a
  // T-junction (┴: up + left + right) rather than an L (└/┘), so one continuous
  // separator runs across the row instead of appearing to break at each box.
  bool join_left = false;
  bool join_right = false;
};

struct UICell;

// One Lua float-window's screen rect (grid cells), as laid out by
// LuaAPI::render_floats() each frame. The GUI frontend reads this list to
// paint floats as a fixed overlay on top of the sliding content instead of
// leaving them in the grid (where they would move with the scroll).
struct FloatOverlay
{
  int handle = 0;
  int x = 0, y = 0, w = 0, h = 0;
  // True when the float is fixed to the window (relative = "editor"): the
  // GUI eases its position at frame rate so Lua's row-stepped drift reads
  // as a smooth glide. Tracking floats (cursor/win-relative) snap instead
  // so they never lag the cursor.
  bool animate = true;
  // The jot.ui.handler surface that opened this float ("sidebar",
  // "quick_pick", ...), or empty for standalone floats (toasts, user
  // floats). The GUI keys its animation state by this name, so a surface that
  // re-emits with a fresh handle every frame still animates as one panel; the
  // home screen instead keeps its handle and its scratch buffer across frames
  // (runtime/lua/features/ui/home.lua). Standalone floats key by handle.
  std::string surface;
  // The float's own cells, captured by render_floats right after painting
  // it into the grid (h * w, row-major). Floats overlap in the shared grid
  // (a modal covers toasts, the telescope covers the sidebar), so reading
  // the final grid would mix other floats' pixels into this float's
  // capture; capturing before the next float paints keeps each float's
  // pixels exact. Empty for the terminal backend, which paints floats
  // directly into the grid and never re-reads them.
  std::vector<std::vector<UICell>> cells;
};

// Sentinel for "this cell has no 24-bit colour, use the palette index".
// Declared in terminal.h (see the note there); repeated here for discoverability.

struct UICell
{
  std::string ch = " ";
  int fg = 7;
  int bg = 0;
  // Optional 24-bit colours. Themes and every existing painter stay on the
  // xterm-256 indices above; features that know an exact colour (the inline
  // colour preview) set these and they win over the index. The terminal
  // backend emits 38;2/48;2 when the terminal supports truecolor and quantises
  // to the nearest palette entry when it does not, so the cell model itself
  // stays exact either way; the GUI always resolves them verbatim.
  std::uint32_t fg_rgb = kNoRgb;
  std::uint32_t bg_rgb = kNoRgb;
  bool bold = false;
  bool italic = false;
  bool reverse = false;
  bool dim = false;
  // Underline style: 0 = none, 1 = straight, 2 = wavy. underline_fg is the
  // underline color (SGR 58, -1 = inherit the text color).
  int underline = 0;
  int underline_fg = -1;

  bool operator==(const UICell &other) const
  {
    return ch == other.ch && fg == other.fg && bg == other.bg && fg_rgb == other.fg_rgb
           && bg_rgb == other.bg_rgb && bold == other.bold && italic == other.italic
           && reverse == other.reverse && dim == other.dim && underline == other.underline
           && underline_fg == other.underline_fg;
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
protected:
  // Protected so alternate backends (the GUI frontend) can paint the same
  // immediate-mode cell grid their own way.
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
  // terminal write when the content is identical; rows that did change
  // are diffed at cell granularity (emit_row_diff) so only the changed
  // runs reach the terminal. A typical typing frame then writes a few
  // short cursor moves + SGR runs instead of whole rows or the whole
  // screen.
  std::vector<std::vector<UICell>> last_grid;
  // Renders since the last full-screen paint. Terminals can occasionally
  // drop or garble a row's bytes mid-frame; since an unchanged row is now
  // left untouched, a periodic full repaint bounds any such artifact's
  // lifetime. Initialised to the threshold so the very first frame is a
  // full paint (the terminal may show stale content from whatever ran
  // before the editor).
  int renders_since_full_paint_ = 90;
  // Set by resize(): the next render() paints every cell once. Newly exposed
  // cells are default-constructed in both the live and the retained grid, so
  // the cell diff would consider them identical and skip them even though the
  // terminal may show restored content there.
  bool full_repaint_pending_ = false;
  int width, height;
  int cursor_x, cursor_y;
  UICursorShape cursor_shape;
  bool cursor_hidden;
  bool cursor_dirty = true;
  // Software blink visibility for the terminal cursor (set by the editor's
  // unified blink clock each frame): visible emits the show sequence with the
  // steady DECSCUSR shape, hidden emits the hide sequence.
  bool cursor_blink_visible = true;
  // The theme's cursor pair (fg_cursor/bg_cursor). Backends that paint the caret
  // themselves (the GUI) pick between the two by contrast against the cell; the
  // terminal leaves the hardware cursor's colour to the emulator, which owns it.
  int cursor_fg = 0;
  int cursor_bg = 7;
  // Builds the DECSCUSR sequence for `shape`. Steady shapes only: the blink is
  // jot's own clock (cursor_blink_ms), and a blinking shape would blink at the
  // terminal's rate on top of it.
  std::string cursor_shape_sequence(UICursorShape shape) const;
  int default_fg = 7;
  int default_bg = 0;

  void set_cell(int x, int y, const UICell &cell);
  // A blank cell in the UI's default colors. Use this rather than a brace
  // initializer for UICell: the optional 24-bit colors sit between `bg` and
  // `bold`, so a positional initializer silently sets them to 0 (truecolor
  // black) instead of leaving them unset.
  UICell blank_cell() const;
  // Writes a cell's colour pair as SGR, applying the dim adjustment and
  // preferring a 24-bit colour when the cell carries one. Shared by the full
  // row painter and both diff paths so they cannot drift apart.
  void emit_cell_colors(const UICell &cell);
  void mark_all_rows_dirty();
  // Paints one row in full: cursor to (0, y), then style-coalesced runs
  // covering the whole paintable width, padded to the right margin. Used
  // for full repaints (capture mode, periodic self-heal) where the
  // terminal state cannot be assumed.
  void emit_full_row(int y, int row_width);
  // Cell-level diff emitter for a row already known to differ from
  // last_grid: writes only the changed runs (merging tiny gaps), moving
  // the cursor per run, and leaves unchanged cells untouched. Unchanged
  // cells are byte-identical to what the terminal shows, so skipping
  // them is safe and cuts per-frame output to a few short writes on a
  // typical typing frame.
  void emit_row_diff(int y, int row_width);

public:
  UI(Terminal *t);
  virtual ~UI();
  void resize(int w, int h);
  // Virtual so GUI backends can repaint without clearing a terminal.
  virtual void invalidate();
  // Forget what was last written to the terminal: the next render() must
  // repaint every row from scratch instead of diffing against last_grid.
  // Used when the terminal surface may have changed without us (window
  // refocus under a compositor, VT redraw after suspend) — the model is
  // still correct, only the physical screen is stale, so unlike
  // invalidate() this emits no clear and touches no cursor state.
  void forget_last_frame();

  void clear();
  // Paint the retained grid. The base implementation emits the terminal
  // diff; GUI backends override this to draw the same cells with a GPU.
  virtual void render();
  void emit_raw_after_frame(const std::string &bytes);

  void set_default_colors(int fg, int bg);

  // The theme's cursor colours; see the fields above.
  void set_cursor_colors(int fg, int bg);

  void draw_text(int x,
                 int y,
                 const std::string &text,
                 int fg = 7,
                 int bg = -1,
                 bool bold = false,
                 bool italic = false,
                 int underline = 0,
                 int underline_fg = -1,
                 std::uint32_t fg_rgb = kNoRgb,
                 std::uint32_t bg_rgb = kNoRgb,
                 bool dim = false);
  void draw_rect(const UIRect &rect, int fg, int bg);
  // Draws a box, optionally inking only some of its sides. A corner glyph is
  // used only where both of its sides are on; a lone side runs its line glyph
  // through to the endpoint, so a separator never ends in a stray corner.
  //
  // `bottom_bg` overrides the background of the bottom row (-1 = use `bg`). A
  // bar sitting directly on top of another region takes that region's
  // background: with the editor's own background it read as leftover editor
  // space with a line drawn in it rather than as the top edge of the block below.
  void draw_border(const UIRect &rect,
                   int fg,
                   int bg,
                   const UIBorderEdges &edges = UIBorderEdges{},
                   int bottom_bg = -1);
  void fill_rect(const UIRect &rect, const std::string &ch, int fg, int bg);
  // Dims a region of the grid (modal scrim). GUI backends override this to
  // skip the grid paint and draw their own eased scrim overlay instead, so
  // the dim fades in/out smoothly.
  virtual void dim_rect(const UIRect &rect);

  // GUI smooth-scroll hook: the editor reports each pane's body region
  // (grid cells, border columns excluded) and how many visible rows the
  // pane scrolled since the previous frame (positive = content moved up).
  // `jumped_x` says the pane's horizontal window moved too (no slide
  // animation covers that axis). GUI backends animate the shift; the
  // terminal backend ignores it.
  virtual void notify_pane_scroll(int pane_id,
                                  int x,
                                  int y,
                                  int w,
                                  int h,
                                  int delta_rows,
                                  bool jumped_x)
  {
    (void)pane_id;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)delta_rows;
    (void)jumped_x;
  }

  // Called right before Lua floats paint into the grid. GUI backends
  // snapshot the float-free grid here so they can render floats as a fixed
  // overlay (see float_overlays) instead of letting them slide with the
  // content during scroll animations; the terminal backend ignores it.
  // `has_visible` says whether any float is about to be painted.
  virtual void before_float_render(bool /*has_visible*/)
  {
  }

  // Whether the backend needs render_floats to publish each float's own
  // cells (see FloatOverlay::cells). The terminal paints floats into the
  // grid directly; the GUI overlay pass repaints them from the captures.
  virtual bool wants_float_cells() const
  {
    return false;
  }

  // Screen rects (grid cells) of every visible float, newest layout from
  // LuaAPI::render_floats(), ordered back-to-front (zindex). Filled every
  // frame; empty when no floats are visible.
  std::vector<FloatOverlay> float_overlays;

  // Store cursor position/visibility, no terminal writes. render() emits
  // the cursor at frame-end; flush_cursor() emits it for idle frames.
  void set_cursor(int x, int y, UICursorShape shape = UICursorShape::Block);
  void hide_cursor();
  void reset_cursor_state();
  // Applies the current software-blink visibility to the terminal cursor.
  // Only marks the cursor dirty when the state actually changed, so idle
  // frames never re-emit cursor bytes.
  void set_cursor_blink_visible(bool visible);
  // The caret's terminal bytes for the current state: hide, or show plus the
  // steady shape. Composed in one place so a hidden cursor can never be
  // re-shown by the shape write that follows it.
  std::string cursor_sequence() const;
  // Emits the pending cursor to the physical screen. The base
  // implementation writes terminal escapes; GUI backends override this to
  // a no-op (their render() paints the cursor from the same state).
  virtual void flush_cursor();
  bool cursor_needs_flush() const
  {
    return cursor_dirty;
  }
  // Whether the last frame left the terminal cursor hidden (palette open,
  // popup covering the editor, etc.) — used to skip blink repaints when
  // nothing visible would blink anyway.
  bool cursor_is_hidden() const
  {
    return cursor_hidden;
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

  // Whether the next render() will paint every cell because the grid was just
  // re-dimensioned (see full_repaint_pending_).
  bool full_repaint_pending() const
  {
    return full_repaint_pending_;
  }
};

#endif
