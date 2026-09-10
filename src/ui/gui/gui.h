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
// Mesa's gl.h only declares core 2.0+ entry points under this macro.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// RAII guard for glyph-atlas uploads. Glyph rows live in a 2048-wide CPU
// buffer, so GL must be told rows are kAtlasW bytes apart (UNPACK_ROW_LENGTH)
// with 1-byte alignment. That pixel-store state is context-global, so it must
// be restored afterwards or every later GL texture upload reads rows with the
// wrong stride and can crash the driver.
class AtlasPixelStoreGuard
{
public:
  AtlasPixelStoreGuard(int row_length)
  {
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align_);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prev_row_len_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
  }
  ~AtlasPixelStoreGuard()
  {
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align_);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prev_row_len_);
  }
  AtlasPixelStoreGuard(const AtlasPixelStoreGuard &) = delete;
  AtlasPixelStoreGuard &operator=(const AtlasPixelStoreGuard &) = delete;

private:
  GLint prev_align_ = 0;
  GLint prev_row_len_ = 0;
};

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
  bool wants_float_cells() const override
  {
    return true;
  }

  void invalidate() override;
  // Cursor painting happens inside render(); no terminal escapes to emit.
  void flush_cursor() override;

  // Applies an absolute font size (px, clamped to [8, 40]): re-sizes every
  // style face, drops the atlas and re-fits the grid to the window.
  void apply_font_size(int px);

  // Smooth-scroll hook: the editor reports each pane's body region and how
  // many visible rows it scrolled since the last frame. The pane's content
  // is rendered shifted by the animated offset (points) until it settles.
  void notify_pane_scroll(int pane_id, int x, int y, int w, int h, int delta_rows) override;

  // Snapshot the float-free grid so floats render as a fixed overlay that
  // never moves with the scroll; see paint_float_overlays.
  void before_float_render(bool has_visible) override;

  // Modal scrim: records the dim (the grid paint is skipped in GUI mode)
  // so render() can draw a smoothly-fading scrim overlay instead.
  void dim_rect(const UIRect &rect) override;

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

  // Live window size in points (SDL_GetWindowSize): the same source the
  // resize-event handler grids from, used by the pump's defensive re-sync.
  void window_size(int &w, int &h) const;
  // Live drawable size in pixels (SDL_GL_GetDrawableSize).
  void drawable_size(int &w, int &h) const;
  // Grid cell size in window points (also the quad unit).
  float cell_w() const
  {
    return cell_w_;
  }
  float cell_h() const
  {
    return cell_h_;
  }
  // Cached drawable size (pixels) from the last resize event / sync.
  int pixel_w() const
  {
    return pixel_w_;
  }
  int pixel_h() const
  {
    return pixel_h_;
  }
  // Re-cache the drawable size; returns true when it actually changed.
  bool note_drawable_size(int w, int h);

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
  // Fixed-overlay pass: paints every Lua float (toasts, hover, user
  // popups) from the live grid at its absolute position, eased to the
  // pixel level so Lua's row-stepped animations read as smooth glides.
  // Floats are not part of the sliding content (see before_float_render),
  // so they never move with the scroll. Colors are eased the same way:
  // each float keeps a resolved-RGB copy of its cells that eases toward
  // the live grid's colors at frame rate, so Lua's 50ms fade steps render
  // as a continuous dissolve. While any float is mid-transition
  // needs_repaint() keeps the pump rendering at the monitor's refresh.
  void paint_float_overlays(float dt);
  // Paints one float from its captured cells + resolved RGB (see
  // GuiFloatColors) at the layout position translated by (dx_px, dy_px),
  // with quad alpha (enter/exit fades).
  void paint_float_cells(int x, int y, int w, int h, float dx_px, float dy_px,
                         const std::vector<std::vector<UICell>> &cells,
                         const std::vector<float> &rgb,
                         float alpha = 1.0f);
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
  // Same as cell_colors, but also resolves the underline color, writing
  // all three into out[0..8]: fg.r,g,b, bg.r,g,b, underline.r,g,b.
  static void resolve_cell_rgb(const UICell &cell, float *out);

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

  // --- Float overlay state -------------------------------------------------
  // Snapshot of the grid taken before Lua floats paint (see
  // before_float_render). The slide sprites and the plain pass read from
  // here whenever floats are visible, so float cells never appear inside
  // the sliding content; paint_float_overlays draws them on top instead.
  // Empty = no floats visible = paint from the live grid as usual.
  std::vector<std::vector<UICell>> pre_float_grid_;
  // Key for a float's animation state: the handler surface name when the
  // float was opened by a UI handler (those re-emit every frame with a
  // fresh handle, so the handle is useless as a key), or "h:<handle>" for
  // standalone floats (toasts, user floats) whose handle is stable.
  // Per-surface/per-float animation state: eased pixel position (Lua moves
  // floats in whole-cell steps -- toast drift, restack, fade -- and this
  // glides between them at frame rate), plus the open/close transition.
  // Surfaces (sidebar, quick pick, modals, ...) animate in when they
  // appear (slide from the window edge / fade + rise) and out when they
  // close (fade/slide toward the same edge, painted from a retained
  // capture because the live grid no longer holds them). Snaps on
  // teleports (window resize, far jumps).
  enum class GuiFloatKind
  {
    kCenter, // fade in + rise; modals, pickers, menus
    kLeft,   // slide in from the left edge (sidebar)
    kRight,  // slide in from the right edge (side panel)
    kBottom, // fade only (status-line strips)
  };
  // Key for a float's animation state: the handler surface name when the
  // float was opened by a UI handler (those re-emit every frame with a
  // fresh handle, so the handle is useless as a key), or "h:<handle>" for
  // standalone floats (toasts, user floats) whose handle is stable.
  static std::string float_key(const std::string &surface, int handle);
  // Slide/fade style for a float: surfaces choose by name (sidebar slides
  // from the left, side panel from the right, status-line strips fade),
  // standalone floats by screen position (against an edge = slide from it,
  // else center fade + rise).
  static GuiFloatKind float_kind_for(const std::string &surface, int x, int w, int width);

  struct GuiFloatAnim
  {
    float x_px = 0.0f, y_px = 0.0f;
    bool seen = false;
    // Entrance progress 0..1 (1 = fully in) and exit progress 0..1
    // (0 = just closed, 1 = done, erased). Exiting floats are painted
    // from exit_cells/exit_rgb with alpha (1 - ease(exit_t)) sliding
    // toward their edge.
    float enter_t = 1.0f;
    float exit_t = 0.0f;
    bool exiting = false;
    GuiFloatKind kind = GuiFloatKind::kCenter;
    int exit_x = 0, exit_y = 0, exit_w = 0, exit_h = 0;
    std::vector<std::vector<UICell>> exit_cells;
    std::vector<float> exit_rgb;
  };
  std::unordered_map<std::string, GuiFloatAnim> float_anims_;

  // Per-float smoothed color state. `cells` is the float's captured
  // content (glyphs/attrs come from here); `rgb` holds the resolved
  // RGB of each cell -- 9 floats: fg.r,g,b then bg.r,g,b then
  // underline.r,g,b -- eased toward the live grid's colors each frame
  // (the fade's color steps land every 50ms from Lua, and this spreads
  // each step over ~2 ticks at frame rate). Snaps when the content
  // itself changes or a float appears/teleports. Only toasts (surface
  // empty) color-ease; handler surfaces re-paint their content every
  // frame (typing in a picker must not cross-fade) so they snap.
  struct GuiFloatColors
  {
    std::vector<std::vector<UICell>> cells;
    std::vector<float> rgb;
    bool settled = true;
  };
  std::unordered_map<std::string, GuiFloatColors> float_colors_;
  // Keys present in last frame's overlay list; used to detect surface
  // open (new key) and close (vanished key) transitions.
  std::unordered_set<std::string> last_float_keys_;
  // Set by paint_float_overlays each frame: true while any float's eased
  // position/colors or an enter/exit transition is still running.
  // needs_repaint() reads these so the pump keeps rendering until
  // everything settles.
  bool float_anims_transitioning_ = false;
  bool float_colors_transitioning_ = false;
  // Color easing time constant: with ~50ms between Lua fade steps, ~30ms
  // leaves the previous transition ~80% converged when the next step
  // lands, so consecutive steps overlap into one continuous glide.
  static constexpr float kFloatColorTau = 0.030f;
  // Open/close transition durations.
  static constexpr float kFloatEnterSecs = 0.16f;
  static constexpr float kFloatExitSecs = 0.14f;

  // Modal scrim: dim_rect() records the dim (GUI mode) instead of baking
  // it into the grid, and render() draws a full-window black quad whose
  // alpha eases toward kDimAlpha while any modal is up. Matches the
  // terminal backend's dim factor (cell colors multiplied by 0.55).
  bool dim_active_ = false;
  float dim_alpha_ = 0.0f;
  bool scrim_transitioning_ = false;
  static constexpr float kDimAlpha = 0.45f;

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
  // The grid the current frame's content passes paint from: the pre-float
  // snapshot when floats are visible, the live grid otherwise. Set at the
  // top of render(); never null inside render().
  const std::vector<std::vector<UICell>> *content_grid_ = nullptr;

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