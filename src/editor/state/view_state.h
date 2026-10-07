#ifndef JOT_STATE_VIEW_STATE_H
#define JOT_STATE_VIEW_STATE_H

#include "editor/model/theme.h"       // Theme
#include "extras/coding_time/probe.h" // coding_time::Counter, Totals
#include "extras/wakatime.h"          // jot_wakatime::Options (api/coding_time.h)
#include "syntax/color_codes.h"       // jot_color::SpanCache
#include "syntax/color_definitions.h" // jot_color::Definitions
#include <cstdint>
#include <string>

// How the editor presents itself right now: the layout metrics the renderer
// reads (status line height, indent guides, line numbers), the paint caches
// (inline colour spans and their variable definitions), the blink clock, the
// theme in force, the message line and the dirty flag a frame waits on.
//
// Split out of editor_state.h (which is now the umbrella over src/editor/state/);
// the members and their comments moved verbatim.
struct ViewState
{
  bool needs_redraw = false;
  bool gui_mode = false; // the SDL3/OpenGL frontend (jot --gui) owns the screen
  int status_height = 0;
  bool show_indent_guides = false;
  bool rainbow_brackets = true;
  bool relative_line_numbers = false;
  bool highlight_cursor_line = false;
  bool auto_indent = false;
  bool smart_paste_indent = false;
  int render_fps = 0;
  int idle_fps = 0;
  int last_cursor_shape = 0;

  Theme theme;
  std::string current_theme_name;

  // Inline colour preview (syntax/color_codes.cpp): its options are read from
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
  // highlights: anchor in steady-clock ms, the end of the input pause that
  // keeps the cursor solid (the cycle runs from there, so the half after it is
  // whole), and the effective visibility applied to both. See ui/cursor_blink.h
  // for the phase itself.
  long long blink_anchor_ms;
  long long blink_suspend_until_ms;
  bool blink_visible = false;

  // The two time labels the last frame asked to paint: the local wall clock and
  // the coding-time chip. The frame loop compares freshly formatted labels
  // against those two strings, so the instant a wall-clock minute or a second of
  // credited coding time rolls over is what buys the next repaint -- no timer of
  // its own, and no repaint while the text would come out identical.
  // `status_time_checked_ms` is the reading those labels were last computed at:
  // neither can move twice within one second, so the comparison is only worth
  // doing once per second (the frame loop runs at render_fps, not once a
  // second).
  long long status_time_checked_ms = 0;
  std::string status_clock_label;
  // (`status_coding_text` rather than `..._label` so it cannot be mistaken for
  // the Editor::status_coding_label() that formats the label it holds.)
  std::string status_coding_text;

  // The local coding-time store (extras/coding_time/probe.h): every workspace and
  // day it knows about, the running counter for the workspace in front of the
  // user, and the key that counter belongs to (which changes at midnight, and
  // when the workspace does). `coding_time_dirty` is set when the total has
  // moved but is not on disk yet.
  coding_time::Totals coding_time_totals;
  coding_time::Counter coding_time;
  std::string coding_time_key;
  bool coding_time_dirty = false;
  long long coding_time_flushed_ms = 0;

  // WakaTime coding time (extras/wakatime.h): the setting in force, the cli's
  // answer for today's total and when it was last asked for one, the heartbeat
  // rule's two memories (the file it last sent and when), and the one-time
  // checks that keep the editor from spawning a cli it does not have. An empty
  // `wakatime_today_text` is what makes the chip fall back to the local total,
  // which is also all it shows until the first answer arrives.
  bool wakatime_enabled = false;
  bool wakatime_probed = false;    // the PATH / install / api-key check has run
  bool wakatime_cli_ready = false; // it passed, so spawning is worth it
  // The binary that check settled on: empty when PATH has a wakatime-cli of the
  // user's own, otherwise the absolute path of the one this editor installed
  // into the WakaTime home (extras/wakatime.h), which is what every spawn and
  // every poll below is then run from.
  std::string wakatime_cli_path;
  // The wakatime_api_key the check above ran against, so typing a key in
  // :settings re-runs it instead of waiting for a restart.
  std::string wakatime_probe_key;
  bool wakatime_today_running = false;
  long long wakatime_today_checked_ms = 0;
  std::string wakatime_today_text;
  long long wakatime_last_sent_ms = 0;
  std::string wakatime_last_entity;

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
