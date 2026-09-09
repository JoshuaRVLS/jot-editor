// GUI frontend (SDL3 + OpenGL 3.3 core + FreeType): paints the same
// immediate-mode cell grid the terminal backend paints, but with a GPU and
// vsync'd to the monitor refresh, so typing is smooth at any refresh rate.
//
// The editor is unchanged: it draws cells through UI::draw_* / fill_rect /
// dim_rect, UIGui::render() repaints the retained grid with glyph quads
// from a FreeType atlas, and poll_event() translates SDL events into the
// exact Event convention the terminal path produces (termkey key codes,
// modifier bits, SGR-style mouse buttons), so all input handlers work
// unchanged.
//
// Implementation is split into focused modules: gui_setup.cpp (lifecycle,
// GL/FreeType setup), gui_glyphs.cpp (atlas), gui_batch.cpp (quad batching),
// gui_colors.cpp (palette), gui_utf8.cpp (decoding), gui_anim.cpp (scroll +
// cursor animation state), gui_render.cpp (frame painting), gui_cursor.cpp
// (cursor/invalidate), and gui_input.cpp (SDL event translation).
#ifndef UI_GUI_H
#define UI_GUI_H

#include "ui.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;
typedef struct FT_LibraryRec_ *FT_Library;
typedef struct FT_FaceRec_ *FT_Face;

// One atlas glyph: UV rect into the atlas texture plus pen metrics
// (bearing/advance in pixels, from FreeType at the active pixel size).
// Decodes one UTF-8 codepoint from `s` starting at `i`; advances `i` past
// the sequence. Returns 0 on invalid input (caller should stop). Shared by
// the glyph painter (gui_render.cpp/gui_cursor.cpp) and the input
// translator (gui_input.cpp).
uint32_t decode_utf8(const char *s, size_t len, size_t &i);

struct GuiGlyph
{
  float u0, v0, u1, v1;
  float bearing_x;
  float bearing_top;
  float advance;
};

class UIGui : public UI
{
public:
  // Creates the SDL3 window sized for `cols` x `rows` cells at the default
  // font size, sets up the GL 3.3 core context (vsync on) and the glyph
  // atlas. Throws std::runtime_error when the display/font is unavailable.
  UIGui(int cols, int rows, int default_fg, int default_bg, int font_px = 16);
  ~UIGui() override;

  void render() override;
  // No terminal to clear: the next render() repaints everything anyway.
  void invalidate() override;
  // Cursor painting happens inside render(); no terminal escapes to emit.
  void flush_cursor() override;

  // Smooth-scroll hook: the editor reports each pane's body region and how
  // many visible rows it scrolled since the last frame. The pane's content
  // is rendered shifted by the animated offset (points) until it settles.
  void notify_pane_scroll(int pane_id, int x, int y, int w, int h, int delta_rows) override;

  // True while a scroll or cursor animation is in flight; the editor pump
  // keeps repainting so the animation advances at the monitor's refresh.
  bool needs_repaint() const;

  // Pops one native SDL event, translated into jot's Event convention
  // (same key codes / modifier bits / SGR mouse buttons as the terminal
  // path). Returns false when the SDL event queue is empty. Multi-
  // codepoint text input is queued internally and returned one Event at
  // a time.
  bool poll_event(Event &out);

  // Set by SDL_EVENT_QUIT; the editor pump stops the loop when set.
  bool quit_requested() const
  {
    return quit_requested_;
  }

  // Font zoom (Ctrl+= / Ctrl+- from the GUI event pump): re-applies the
  // pixel size to every face, drops the atlas (rebuilt lazily), recomputes
  // the cell metrics and re-fits the grid to the current window size.
  // No-op when the new size would leave the [8, 40] px range.
  void apply_font_zoom(int step);

  // Current font pixel size (for persisting the zoom level).
  int font_px() const
  {
    return font_px_;
  }

private:
  enum FontStyle
  {
    kStyleRegular = 0,
    kStyleBold = 1,
    kStyleItalic = 2,
    kStyleBoldItalic = 3,
    kStyleCount = 4,
  };

  bool init_sdl_and_gl();
  bool init_freetype();
  bool compile_shaders();
  bool create_textures();
  // Loads the face for `style`, falling back to the regular face. Paths
  // are probed in order; JOT_GUI_FONT overrides the regular face path.
  bool load_face(int style, const std::vector<std::string> &paths);
  // Renders `codepoint` in `style` into the atlas if not already cached.
  // Returns false when the atlas is full (caller clears and retries once).
  bool ensure_glyph(uint32_t codepoint, int style);
  void clear_atlas();
  // Recomputes cell_w_/cell_h_/ascent_ from the regular face's metrics
  // (shared by init_freetype and apply_font_zoom).
  void refresh_cell_metrics();

  // Batch helpers: accumulate quads into vertex_ and flush with one draw.
  void begin_batch();
  void push_quad(float x0, float y0, float x1, float y1, float u0, float v0,
                 float u1, float v1, float r, float g, float b, float a);
  void end_batch();
  // Binds `tex` to unit 0 and sets the u_tex uniform (shared by all passes).
  static void flush_tex(unsigned int program, unsigned int tex);
  // Scratch vertex buffer size in floats (8 floats/vertex, 6 verts/quad).
  static constexpr int kMaxBatchVertices = 1 << 20;

  struct GuiScrollAnim; // defined with the animation state below
  // Paint: animated panes draw every retained frame translated to its
  // position in the sliding strip (each frame holds identical file rows
  // where they overlap, so this never ghosts and always covers the pane in
  // both scroll directions), then all static content draws on top, then
  // the cursor. `rows` == nullptr paints the live grid (absolute columns);
  // otherwise `rows` are pane-relative (column 0 = grid column a.x1).
  void paint_sprite(const GuiScrollAnim &anim, const std::vector<std::vector<UICell>> *rows,
                    float dy);
  void paint_plain();
  void paint_cursor();
  // Push the bg/glyph/underline quads of one row slice. `src` holds the
  // row's cells (a grid row or a retained pane row); draws columns
  // [col0, col1) of it, source column 0 at pixel x `x0px`, top edge at
  // `y_top` translated by `dy`.
  void paint_row_bg(const std::vector<UICell> &src, int col0, int col1, float x0px, float y_top,
                    float dy, size_t &quads);
  void paint_row_glyphs(const std::vector<UICell> &src, int col0, int col1, float x0px,
                        float y_top, float dy);
  void paint_row_underlines(const std::vector<UICell> &src, int col0, int col1, float x0px,
                            float y_top, float dy, bool &any);

  // Cell colors: xterm 256-index -> linear-ish rgb (0..1).
  static void xterm_rgb(int index, float &r, float &g, float &b);
  // Resolves a cell's effective fg/bg (honoring reverse), dimmed.
  void cell_colors(const UICell &cell, float &fr, float &fg_, float &fb, float &br,
                   float &bg_, float &bb) const;

  SDL_Window *window_ = nullptr;
  // Opaque SDL_GLContext (struct SDL_GLContextState *): kept as void* so
  // this header stays SDL-free for terminal-only builds.
  void *gl_context_ = nullptr;

  unsigned int program_ = 0;
  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  unsigned int white_tex_ = 0;
  unsigned int atlas_tex_ = 0;
  // Scratch vertex buffer (8 floats/vertex, 6 vertices/quad). Grown on
  // demand; sized for the max grid observed so far.
  std::vector<float> vertex_;
  size_t vertex_quads_ = 0;

  FT_Library ft_lib_ = nullptr;
  FT_Face faces_[kStyleCount] = {nullptr, nullptr, nullptr, nullptr};
  // Atlas: 2048x2048 R8. One row of glyphs at a time; cleared wholesale
  // when full (the cache is rebuilt lazily on the next frame).
  std::vector<unsigned char> atlas_pixels_;
  static constexpr int kAtlasW = 2048;
  static constexpr int kAtlasH = 2048;
  int atlas_x_ = 0;
  int atlas_y_ = 0;
  int atlas_row_h_ = 0;
  std::unordered_map<uint64_t, GuiGlyph> glyphs_;

  float cell_w_ = 10.0f;
  float cell_h_ = 20.0f;
  float ascent_ = 0.0f;
  int font_px_ = 16;

  // Window size in points (cell math) and pixels (GL viewport).
  int window_w_ = 0;
  int window_h_ = 0;
  int pixel_w_ = 0;
  int pixel_h_ = 0;

  bool quit_requested_ = false;
  // Queued events from multi-codepoint text input / IME commits.
  std::vector<Event> pending_events_;
  // Storage for the last synthesized paste payload (EVENT_PASTE text).
  std::string pending_paste_text_;

  // --- Smooth-scroll / cursor animation state ---------------------------
  // Per-pane scroll animation (neovide-style). The slide is a strip of
  // buffer content: its display position (rows from the chain start) is
  // s = total_px/cell_h - offset_px/cell_h, easing from 0 to the net
  // scroll. Each frame -- the live grid plus every viewport the chain has
  // visited (captured at the end of each render into `frames`) -- is
  // painted translated by (frame_top - s)*cell_h, so wherever two frames
  // overlap they hold the same file rows and draw the same pixels: no
  // ghosting, and the retained set always covers the displayed strip in
  // both scroll directions (quick reversals can't leave an edge unrendered
  // because the viewports at both extremes are retained). Deltas arriving
  // mid-slide ACCUMULATE into total_px/offset_px so wheel bursts stay
  // continuous; when a slide settles (offset eases to 0) total_px resets
  // and `frames` clears, so the next chain starts from the settled
  // viewport. Jumps bigger than a pane snap (offset/total reset, frames
  // cleared, recaptured next frame).
  // One retained copy of a pane body: the viewport at `top_row` rows from
  // the current chain start. Frames with the same top replace each other,
  // so `frames` holds one copy per distinct viewport the chain visited.
  struct GuiFrame
  {
    int top_row = 0;
    std::vector<std::vector<UICell>> rows; // pane-relative columns
  };
  struct GuiScrollAnim
  {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0; // body region (grid cells, x2/y2 exclusive)
    float offset_px = 0.0f;             // current shift of the new content (eases to 0)
    float total_px = 0.0f;              // net scroll of the chain (delta * cell_h)
    // Retained viewports of this slide chain, oldest first. Empty until at
    // least one frame has been rendered for this pane.
    std::vector<GuiFrame> frames;
  };
  std::unordered_map<int, GuiScrollAnim> scroll_anims_;

  // Retains the current grid as this chain's frame for `top_row` (replaces
  // an older copy of the same viewport). Called at the end of every render,
  // so every viewport the chain visits is available to the slide.
  void capture_pane_rows();
  // Pixel offset for the cursor cell: the new content of an animating pane
  // is shifted by its offset_px. Returns 0 when the cursor is not inside an
  // animating pane body.
  float cursor_pane_offset(int x, int y) const;

  // Cursor glide: pixel position eases toward the cursor cell (plus any
  // pane scroll offset covering it) each frame. -1 = uninitialized (snap).
  float cursor_px_ = -1.0f;
  float cursor_py_ = -1.0f;
  float cursor_target_x_ = -1.0f;
  float cursor_target_y_ = -1.0f;
  uint64_t last_frame_ticks_ = 0;

  float frame_dt();
  void advance_animations(float dt);
  void advance_cursor_glide(float dt);
};

#endif