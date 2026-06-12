#ifndef TERMINAL_H
#define TERMINAL_H

#include <functional>
#include <string>
#include <unistd.h>
#include <vector>

enum EventType { EVENT_KEY, EVENT_MOUSE, EVENT_RESIZE, EVENT_REDRAW };

struct KeyEvent {
  int key;
  bool ctrl;
  bool shift;
  bool alt;
};

struct MouseEvent {
  int x, y;
  int button;
  bool pressed;
  bool released;
  bool ctrl;
  bool shift;
  bool alt;
};

struct ResizeEvent {
  int width, height;
};

struct Event {
  EventType type;
  union {
    KeyEvent key;
    MouseEvent mouse;
    ResizeEvent resize;
  };
};

class Terminal {
private:
  int width, height;
  int poll_timeout_ms;
  bool raw_mode;
  std::string buffer;
  std::string mouse_event_buffer;
  FILE *render_capture_ = nullptr;
  int render_capture_seq_ = 0;
  bool render_capture_raw_ = false;
  int last_flush_bytes_ = 0;
  // Per-frame safety margin: the renderer will not write the
  // rightmost `render_margin_` physical columns of any row, and the
  // cursor is clamped one cell inside that margin, so we never
  // trigger the terminal's pending-wrap state at large widths.
  // The margin is fixed at one cell; larger values would only
  // produce a visibly oversized blank strip on the right edge of
  // the UI without any safety benefit. UI::get_render_width() is
  // defined as `width - render_margin()` and is the width every
  // full-width panel (pane layout, status line, integrated
  // terminal, image viewer, home menu) must use so its right
  // border lands on the last paintable column.
  int render_margin_ = 1;
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

  void enable_raw_mode();
  void disable_raw_mode();
  void setup_terminal();
  void restore_terminal();
  int read_key();
  void parse_mouse_event(int ch, MouseEvent &event);

public:
  Terminal();
  ~Terminal();

  void init();
  void cleanup();

  int get_width() const { return width; }
  int get_height() const { return height; }

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
  void set_color(int fg, int bg);
  void reset_color();
  void set_bold(bool on);
  void set_italic(bool on);
  void set_reverse(bool on);

  void write(const std::string &str);
  void write_char(char c);

  int get_input_fd() const { return STDIN_FILENO; }
  Event read_event();
  Event check_resize_event();

  void enable_mouse();
  void disable_mouse();
  void enable_mouse_hover();
  void disable_mouse_hover();

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
  bool render_capture_enabled() const { return render_capture_ != nullptr; }
  bool render_capture_raw() const { return render_capture_raw_; }
  int render_capture_bytes_since_last_flush() const { return last_flush_bytes_; }
  void render_capture_marker(const std::string &label, int rows_rendered);

  // Number of physical columns on the right edge of every row that the
  // renderer must leave untouched. This is fixed at exactly one cell so
  // full-width borders sit on the last paintable column without creating
  // a wider visual gap.
  int render_margin() const { return 1; }

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
