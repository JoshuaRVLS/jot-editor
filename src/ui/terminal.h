#ifndef TERMINAL_H
#define TERMINAL_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
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

// Normalizes a raw termkey-style key code into the Event convention the input
// layer dispatches on: the modifier bits (0x20000 Ctrl, 0x40000 Alt, 0x80000
// Shift) and the 0x8000 uppercase bit move into the ctrl/shift/alt booleans,
// control bytes 1-26 (minus Tab/Enter) imply ctrl, and shifted arrows 2008-2011
// unwrap to 1008-1011. Shared by both frontends, so their events are identical.
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
  // Test seam: the next flush() reports a stalled terminal (see
  // stall_next_flush_for_test). One-shot.
  bool flush_stall_for_test_ = false;
  // Set when a chunked flush dropped bytes part-way through a frame, so that
  // frame's final flush() still reports it (see flush()). Cleared by the next
  // flush().
  bool frame_bytes_dropped_ = false;
  int last_flush_bytes_ = 0;
  // Columns on the right edge of every row the renderer leaves unpainted; zero
  // (the default) uses the full width, since any margin is a blank strip down the
  // right edge. It is an escape hatch only: autowrap is off and rows are addressed
  // absolutely, so writing the last column neither wraps nor scrolls. Every
  // full-width panel uses UI::get_render_width() = width - render_margin().
  int render_margin_ = 0;
  // Per-frame chunking threshold for `flush()`: past this many bytes it writes in
  // blocking chunks of this size instead of one write. Diagnosis-only, default 0
  // (one ordered flush), set with JOT_RENDER_CHUNK_BYTES=<n>.
  size_t render_chunk_bytes_ = 0;
  // Set once at init (detect_truecolor). Defaults to false so the quantised
  // path is what any test that never runs init() exercises.
  bool truecolor_ = false;
  // Text scale of the cell grid: 1 is the classic one character per cell, and
  // above it every grid cell is drawn as a `cell_scale_` x `cell_scale_` block
  // through kitty's text sizing protocol (see write_scaled_text). Cursor moves
  // and mouse reports are translated between grid cells and terminal cells by
  // it, so everything above this class keeps working in grid cells.
  int cell_scale_ = 1;
  // Whether the terminal implements the scale part of that protocol, and
  // whether it has been asked: the probe runs at most once, the first time a
  // scale above 1 is requested, so a terminal that never answers costs its
  // budget on that request instead of on startup.
  bool cell_scale_probed_ = false;
  bool cell_scale_supported_ = false;
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
  // Asks the terminal whether it draws OSC 66 text at the requested scale. A
  // backend whose console cannot (the Windows console) answers false.
  bool probe_text_scale();

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

  // Probes the terminal size from the OS and updates width/height, returning
  // whether either changed; safe any time after construction and independent of
  // SIGWINCH, so the first frame avoids the 80x24 constructor default.
  // `force_probe` adds the ANSI cursor-position probe (gated on both fds being a
  // tty), which is the only reliable read right after the alternate screen is
  // entered and ioctl would report a stale size.
  bool refresh_size(bool force_probe = false);

  Event poll_event();
  void set_poll_timeout_ms(int timeout_ms);
  // Writes the buffered frame and reports whether the terminal took all of it.
  // False when the write gave up (a pty full past the timeout): the bytes that
  // never went out are dropped, so the caller must treat the screen as unknown
  // and force a repaint -- the diff baseline cannot notice on its own.
  bool flush();

  void clear();
  // Parks the cursor at grid cell (x, y), converting to the terminal cell the
  // block that cell is drawn in starts at when a scale is in effect.
  void move_cursor(int x, int y);
  void hide_cursor();
  void show_cursor();
  // Whether the cursor is on screen right now (DECTCEM). Kept by hide_cursor() /
  // show_cursor() and by the UI's own cursor emission, because the DSR size
  // probe parks the cursor in the bottom-right corner to read the viewport and
  // has to put back the visibility it found -- a probe that showed a cursor the
  // editor had hidden (or hid one it had shown) is a caret that jumps or blinks
  // when nothing asked it to.
  bool cursor_visible() const
  {
    return cursor_visible_;
  }
  // Records the visibility of the bytes the UI just emitted for the caret
  // (UI::cursor_sequence), which the probe's restore writes back.
  void note_cursor_visible(bool visible)
  {
    cursor_visible_ = visible;
  }
  bool cursor_visible_ = true;
  // True once any source has produced a size, so the DSR probe (which parks and
  // restores the cursor) only runs when there is nothing to go on.
  bool size_known_ = false;

public:
  // The grid's text scale (1 = one character per terminal cell).
  int cell_scale() const
  {
    return cell_scale_;
  }
  // Sets it. Callers keep working in grid cells: cursor moves and mouse
  // reports translate between the two.
  void set_cell_scale(int scale)
  {
    cell_scale_ = scale > 0 ? scale : 1;
  }
  // Whether the terminal implements the scale part of OSC 66, probing once on
  // the first call (the probe is the protocol's own cursor-position test, and
  // a terminal that does not answer -- a pipe, a CI pty, an emulator without
  // the protocol -- reads as unsupported).
  bool text_scale_supported()
  {
    if (!cell_scale_probed_)
    {
      cell_scale_probed_ = true;
      cell_scale_supported_ = probe_text_scale();
    }    return cell_scale_supported_;
  }

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

  // Queues one chunk of cell text. At scale 1 this is a plain write; above it
  // the chunk is framed in OSC 66, which is the only way a program running in
  // a terminal can make its own text bigger: the emulator owns the glyphs, so
  // a cell-grid program asks it to draw the text in a scale x scale block
  // instead. The frame's text may not exceed 4096 bytes (the protocol's cap),
  // so a long run is split at a UTF-8 boundary, and control bytes are folded
  // to spaces because a BEL would end the frame early.
  void write_scaled_text(const std::string &text)
  {
    if (cell_scale_ <= 1 || text.empty())
    {
      buffer += text;
      return;
    }
    constexpr std::size_t kMaxFrameText = 4096;
    std::size_t at = 0;
    while (at < text.size())
    {
      std::size_t end = std::min(text.size(), at + kMaxFrameText);
      while (end > at && end < text.size() && ((unsigned char)text[end] & 0xC0) == 0x80)
      {
        end--; // never split a UTF-8 sequence across two frames
      }
      char head[24];
      snprintf(head, sizeof(head), "\x1b]66;s=%d;", cell_scale_);
      buffer += head;
      for (std::size_t i = at; i < end; i++)
      {
        const unsigned char byte = (unsigned char)text[i];
        buffer += (byte < 0x20 || byte == 0x7F) ? ' ' : (char)byte;
      }
      buffer += '\a';
      at = end;
    }
  }

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

  // Test seam: make the next flush() report a stalled terminal without needing
  // a pty nobody is reading, so the incomplete-frame recovery can be exercised
  // off a real terminal. One-shot: the frame after it flushes normally.
  void stall_next_flush_for_test()
  {
    flush_stall_for_test_ = true;
  }

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

  // Parses one SGR mouse report the way poll_event() does, so a case can pin
  // the cell translation without a live pointer.
  bool parse_mouse_report_for_test(const std::string &report, MouseEvent &event)
  {
    mouse_event_buffer = report;
    parse_mouse_event(0, event);
    return event.x >= 0;
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
