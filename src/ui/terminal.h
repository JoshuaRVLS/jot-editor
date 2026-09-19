#ifndef TERMINAL_H
#define TERMINAL_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// Sentinel for "this colour is an xterm-256 palette index, not a 24-bit value".
// Lives here because both the cell model (ui.h) and the terminal's SGR writer
// need it, and ui.h already includes this header.
inline constexpr std::uint32_t kNoRgb = 0xFFFFFFFFu;

// Whether the environment advertises 24-bit colour support: COLORTERM set to
// truecolor/24bit, or a TERM that names a direct-colour variant. Used as the
// "auto" answer for the truecolor config key; the terminal itself never
// assumes it, because a wrong guess paints every truecolour cell wrong.
bool terminal_env_supports_truecolor();

struct TermKey;

enum EventType
{
  EVENT_KEY,
  EVENT_MOUSE,
  EVENT_RESIZE,
  EVENT_REDRAW,
  EVENT_PASTE,
  EVENT_FOCUS_IN,
  EVENT_FOCUS_OUT
};

namespace KeyCode
{
  // Keep function keys tagged separately from text and legacy navigation codes.
  constexpr int FunctionBase = 0x3000;
  constexpr int FunctionMarker = 0x10000000;
  constexpr int function(int number)
  {
    return FunctionMarker | FunctionBase | number;
  }
  constexpr int FunctionFirst = function(1);
  constexpr int FunctionLast = function(24);
} // namespace KeyCode

struct KeyEvent
{
  int key;
  bool ctrl;
  bool shift;
  bool alt;
};

// Normalizes a raw termkey-style key code into the Event convention the
// input layer dispatches on: modifier bits (0x20000 Ctrl / 0x40000 Alt /
// 0x80000 Shift) and the 0x8000 uppercase bit are removed from `key` and
// carried only in the ctrl/shift/alt booleans; control bytes 1-26 (minus
// Tab/Enter) imply ctrl; shifted arrows 2008-2011 unwrap to 1008-1011;
// uppercase letters lose 0x8000 (so shift+s -> key 'S', shift=true).
// Shared by the terminal backend and the GUI frontend so both produce
// identical events.
KeyEvent decode_key_event(int raw_ch);

struct MouseEvent
{
  int x, y;
  int button;
  bool pressed;
  bool released;
  bool ctrl;
  bool shift;
  bool alt;
};

struct ResizeEvent
{
  int width, height;
};

struct PasteEvent
{
  const char *text;
};

struct Event
{
  EventType type;
  union
  {
    KeyEvent key;
    MouseEvent mouse;
    ResizeEvent resize;
    PasteEvent paste;
  };
};

class Terminal
{
private:
  int width, height;
  int poll_timeout_ms;
  bool raw_mode;
  TermKey *termkey_;
  std::string buffer;
  std::string mouse_event_buffer;
  std::string paste_event_buffer;
  FILE *render_capture_ = nullptr;
  int render_capture_seq_ = 0;
  bool render_capture_raw_ = false;
  int last_flush_bytes_ = 0;
  // Columns on the right edge of every row that the renderer leaves unpainted.
  // Zero (the default) uses the full width, which is what the layout wants:
  // any margin shows up as a permanent blank strip down the right edge, and the
  // bottom bar then stops one cell short of the corner it should meet.
  //
  // The wrap hazard the margin used to guard against is already handled: every
  // frame disables autowrap (\x1b[?7l, see disable_autowrap) and each row is
  // addressed with an absolute cursor move, so writing the last column neither
  // wraps nor scrolls. The setting exists only as an escape hatch for a terminal
  // that misbehaves there. UI::get_render_width() is defined as
  // `width - render_margin()` and is the width every full-width panel (pane
  // layout, status line, integrated terminal, image viewer, home menu) must use
  // so its right border lands on the last paintable column.
  int render_margin_ = 0;
  // Per-frame chunking threshold for `flush()`. When > 0 and the
  // output buffer has grown past this many bytes, `flush()` will
  // emit the data in blocking `write()` chunks of this size
  // instead of one big write. This is diagnosis-only; the default
  // is 0 (one ordered flush per frame). Mid-frame `try_drain()` is
  // no longer called from the renderer because mixing
  // non-blocking writes with the blocking final flush was causing
  // byte reordering and large-window cursor teleport. If chunking
  // is needed, set JOT_RENDER_CHUNK_BYTES=<n> to enable chunked
  // writes inside `flush()`.
  size_t render_chunk_bytes_ = 0;
  // Set once at init (detect_truecolor). Defaults to false so the quantised
  // path is what any test that never runs init() exercises.
  bool truecolor_ = false;
  // Cell size in pixels, 0 when unreported. Kitty places images in cells and
  // needs none of this; Sixel output is sized in pixels.
  int cell_px_w_ = 0;
  int cell_px_h_ = 0;

  void enable_raw_mode();
  void disable_raw_mode();
  void setup_terminal();
  void restore_terminal();
  int read_key();
  int read_termkey_result();
  void parse_mouse_event(int ch, MouseEvent &event);

public:
  Terminal();
  ~Terminal();

  void init();
  void cleanup();

  int get_width() const
  {
    return width;
  }
  int get_height() const
  {
    return height;
  }

  // Probe the current terminal size from the OS and update width/height if
  // it differs from the cached values. Returns true if either dimension
  // changed. Safe to call any time after construction; this does NOT depend
  // on SIGWINCH having been delivered. Use this before constructing or
  // resizing the UI to ensure the first frame uses the real terminal size
  // and not a stale or fallback value (e.g. the 80x24 constructor default).
  //
  // `force_probe = true` runs the ANSI cursor-position probe in addition to
  // ioctl/$COLUMNS/$LINES. Use this once after entering alternate-screen
  // raw mode and before the first UI::resize(...): the ioctl that was
  // attempted in the normal flow can return stale dimensions if the
  // foreground process group or controlling TTY changed when the alternate
  // screen was switched in, and the cursor probe is the only reliable way
  // to get the real rows/cols in that window. The probe is gated on
  // isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) so piped stdin/stdout
  // still return promptly.
  bool refresh_size(bool force_probe = false);

  Event poll_event();
  void set_poll_timeout_ms(int timeout_ms);
  void flush();

  void clear();
  void move_cursor(int x, int y);
  void hide_cursor();
  void show_cursor();
  // True once any source has produced a size, so the DSR probe (which parks and
  // restores the cursor) only runs when there is nothing to go on.
  bool size_known_ = false;
  // Writes fg/bg as SGR. Indices are always xterm-256 palette entries; when a
  // 24-bit colour is supplied (kNoRgb means "none") it is used verbatim if the
  // terminal understands 38;2/48;2, and quantised to the nearest palette entry
  // otherwise, so callers never have to branch on the terminal's capability.
  void set_color(int fg, int bg, std::uint32_t fg_rgb = kNoRgb, std::uint32_t bg_rgb = kNoRgb);
  // Whether the terminal supports 24-bit colour, decided once at init from
  // COLORTERM/TERM (and the `truecolor` config override).
  bool supports_truecolor() const
  {
    return truecolor_;
  }
  void set_truecolor_supported(bool on)
  {
    truecolor_ = on;
  }

  int cell_pixel_width() const
  {
    return cell_px_w_;
  }
  int cell_pixel_height() const
  {
    return cell_px_h_;
  }
  void set_cell_pixel_size(int w, int h)
  {
    cell_px_w_ = w;
    cell_px_h_ = h;
  }
  void reset_color();
  void set_bold(bool on);
  void set_italic(bool on);
  void set_dim(bool on);
  void set_reverse(bool on);
  // Underline style: 0 = off, 1 = straight, 2 = wavy (CSI 4:3, ECMA-48
  // colon subparameter; supported by kitty/wezterm/Windows Terminal/etc.,
  // older terminals fall back to no underline or straight).
  void set_underline(int style);
  // Underline color via SGR 58. A 24-bit value (kNoRgb means "none") goes out in
  // the colon form (58:2::r:g:b) when the terminal understands 24-bit color and
  // is folded to the nearest palette entry otherwise, exactly like set_color.
  // -1 with no 24-bit value resets the underline to the text color.
  void set_underline_color(int fg, std::uint32_t rgb = kNoRgb);

  void write(const std::string &str);
  void write_char(char c);

  int get_input_fd() const
  {
#ifdef _WIN32
    return -1;
#else
    return STDIN_FILENO;
#endif
  }
  Event read_event();
  Event check_resize_event();

  void enable_mouse();
  void disable_mouse();
  void enable_mouse_hover();
  void disable_mouse_hover();
  // Focus reporting (DECSET 1004): the terminal sends CSI I on window
  // focus-in and CSI O on focus-out. Used to force a full repaint when
  // the window regains focus (compositors may repaint the surface from a
  // stale buffer while the app was unfocused).
  void enable_focus_reporting();
  void disable_focus_reporting();

  void save_cursor();
  void restore_cursor();
  void clear_line();
  // Erase from the cursor to the end of the current line (EL 0, \x1b[K).
  // Used by the renderer to clear any leftover content in the right-edge
  // margin columns and any stale characters past the last painted cell.
  void clear_to_end();

  // Disable the terminal's automatic line-wrap mode (DECAWM off, \x1b[?7l).
  // While disabled, writing past the rightmost column will NOT cause the
  // cursor to wrap to the next line; the terminal will instead keep the
  // cursor on the last column and overwrite it. This is what makes the
  // diff renderer scroll-safe: any overrun on the bottom-right cell is
  // // clamped by the terminal itself instead of causing a viewport scroll.
  // `enable_autowrap()` (\x1b[?7h) restores the default wrap behaviour.
  // `restore_terminal()` always re-enables autowrap on exit so the host
  // shell is left in its normal state.
  void disable_autowrap();
  void enable_autowrap();

  // When `JOT_RENDER_CAPTURE=/path/to/log` is set at startup, every
  // Terminal::flush() appends summary metadata to that file path.
  // Set `JOT_RENDER_CAPTURE_RAW=1` to also capture the full raw bytes.
  // Frame/cursor markers are written by UI::render() / UI::flush_cursor().
  bool render_capture_enabled() const
  {
    return render_capture_ != nullptr;
  }
  bool render_capture_raw() const
  {
    return render_capture_raw_;
  }
  int render_capture_bytes_since_last_flush() const
  {
    return last_flush_bytes_;
  }
  void render_capture_marker(const std::string &label, int rows_rendered);

  // Number of physical columns on the right edge of every row that the
  // renderer leaves untouched (see `render_margin_`).
  int render_margin() const
  {
    return render_margin_;
  }

  // Sets that margin; negative values are treated as zero.
  void set_render_margin(int margin)
  {
    render_margin_ = margin > 0 ? margin : 0;
  }

  // The bytes queued for the next flush, without flushing them. Used by tests
  // to assert on the exact escape sequences the renderer composes.
  const std::string &pending_output_for_test() const
  {
    return buffer;
  }
  void clear_pending_output_for_test()
  {
    buffer.clear();
  }

  // Drain the output buffer to the kernel PTY without blocking. If
  // `render_chunk_bytes_` is 0 (default), this is a no-op. When
  // chunking is enabled, sets O_NONBLOCK on stdout and calls
  // `write()` to push as much of the buffer as the kernel PTY can
  // accept right now. Any data the kernel cannot accept stays in
  // the buffer and is retried by the next `try_drain()` or the
  // final `flush()` at frame end.
  //
  // The renderer no longer calls this from the normal `render()`
  // row loop. Mid-frame drains mixed non-blocking writes with the
  // blocking final flush and was causing byte reordering that
  // manifested as large-window cursor teleport. This method is
  // kept for diagnosis and for a future chunked-flush rebuild that
  // would only emit ordered chunks from inside `flush()` itself.
  void try_drain();

  // Legacy blocking chunked flush. Calls `flush()` if the output
  // buffer has grown past `render_chunk_bytes_`. WARNING: blocks
  // the event loop while the kernel drains the PTY buffer, which
  // freezes input for hundreds of milliseconds on slow terminals.
  // Kept for diagnosis only; the renderer calls `try_drain()`
  // instead.
  void flush_if_buffer_exceeds();
};

#endif
