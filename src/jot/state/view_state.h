#ifndef JOT_STATE_VIEW_STATE_H
#define JOT_STATE_VIEW_STATE_H

#include "features/color_codes.h"       // jot_color::SpanCache
#include "features/color_definitions.h" // jot_color::Definitions
#include "jot/model/theme.h"            // Theme
#include <cstdint>
#include <string>

// How the editor presents itself right now: the layout metrics the renderer
// reads (status line height, indent guides, line numbers), the paint caches
// (inline colour spans and their variable definitions), the blink clock, the
// theme in force, the message line and the dirty flag a frame waits on.
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct ViewState
{
  bool needs_redraw = false;
  bool gui_mode = false; // the SDL3/OpenGL frontend (jot --gui) owns the screen
  int status_height = 0;
  bool show_indent_guides = false;
  bool relative_line_numbers = false;
  bool highlight_cursor_line = false;
  bool auto_indent = false;
  bool smart_paste_indent = false;
  int render_fps = 0;
  int idle_fps = 0;
  int last_cursor_shape = 0;

  Theme theme;
  std::string current_theme_name;

  // Inline colour preview (features/color_codes.cpp): its options are read from
  // config at point of use in render_buffer_content, which is what makes a
  // settings change (or :reload) apply on the next frame with no plumbing. The
  // scan memo is content-hash validated, so it needs no invalidation and is
  // shared across buffers (the key is the line's bytes).
  jot_color::SpanCache colorizer_cache;
  // Colour-preview variable definitions (--name: value / $name: value) for the
  // buffer being rendered, plus a version that is bumped on every rebuild so the
  // line cache above re-resolves references instead of serving a stale colour.
  jot_color::Definitions colorizer_defs;
  std::uint64_t colorizer_defs_version = 0;
  std::string colorizer_defs_path;
  bool colorizer_defs_dirty = true;

  // One software blink clock for the terminal cursor and the extra-caret
  // highlights: anchor in steady-clock ms, a suspension window (input keeps
  // the cursor solid for a moment), and the effective visibility applied to
  // both the cursor and the caret paint.
  long long blink_anchor_ms;
  long long blink_suspend_until_ms;
  bool blink_visible = false;

  // Auto-save: the setting, the interval, and when the last one ran.
  bool auto_save_enabled = false;
  int auto_save_interval_ms = 0;
  long long last_auto_save_ms;

  // The message line. `last_message` is every message ever set, whether or not
  // a Lua status_line handler owns the surface (when one does, `message` is
  // deliberately left alone); the statusline text is a user-visible outcome,
  // so tests assert on it there. A transient message clears on the timer.
  std::string message;
  std::string last_message;
  std::uint64_t transient_message_timer = 0;
  std::uint64_t message_generation = 0;
  std::string clipboard;
};

#endif
