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
  // Per-frame safety margin (default 1): the renderer will not write the
  // rightmost `render_margin_` physical columns of any row, and the cursor
  // is clamped one cell inside that margin, so we never trigger the
  // terminal's pending-wrap state at large widths. Override with the
  // `JOT_RENDER_MARGIN=<n>` env var. JOT_RENDER_MARGIN=2 has been observed
  // to fix some terminals where margin 1 still shows row/column drift on
  // the right edge.
  int render_margin_ = 1;
  // Per-row chunked-flush threshold. After each row the renderer calls
  // `flush_if_buffer_exceeds()` which does a real flush() if the
  // accumulated output buffer has grown past this many bytes. Default
  // 0 (disabled): mid-frame flushes block the event loop while the
  // kernel drains the PTY buffer, freezing input for hundreds of
  // milliseconds. Set JOT_RENDER_CHUNK_BYTES=<n> to enable chunked
  // flushing for diagnosis (e.g. to confirm a terminal drops bytes
  // from large writes); values like 2048 or 4096 keep each write
  // under most terminals' receive buffer.
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
  // renderer must leave untouched. The renderer caps each painted row at
  // `width - render_margin()` cells and clamps the cursor x to one cell
  // further inside (so the cursor itself is never parked on the right
  // margin either). Default 1; override with `JOT_RENDER_MARGIN=<n>`.
  int render_margin() const { return render_margin_; }

  // Flush the output buffer if its size has crossed the chunk threshold
  // (JOT_RENDER_CHUNK_BYTES, default 0 = disabled). The renderer calls
  // this after each row of the full-row paint pass so the terminal sees
  // a series of small writes rather than one very large one. This is
  // defense-in-depth against terminals that silently drop bytes when the
  // per-write payload exceeds their PTY receive buffer. With
  // `render_chunk_bytes_ == 0` the function is a no-op and the renderer
  // falls back to one big flush at frame end.
  //
  // Note: mid-frame flushes can block the event loop while the kernel
  // drains the PTY buffer, freezing input for hundreds of milliseconds
  // on slow terminals. Keep this disabled unless you have a specific
  // terminal that drops bytes from large writes.
  void flush_if_buffer_exceeds();
};

#endif
