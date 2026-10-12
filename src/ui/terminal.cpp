#include "terminal.h"
#include "events/keybind_catalog.h"
#include "ui/cursor_probe.h"
#include "ui/input_reader.h"
#include "ui/xterm_palette.h"

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <termkey.h>
#include <unistd.h>

static struct termios orig_termios;

static volatile sig_atomic_t g_resize_pending = 0;

static void sigwinch_handler(int)
{
  g_resize_pending = 1;
}

static bool probe_terminal_size_via_ioctl(int fd, struct winsize &ws)
{
  return ioctl(fd, TIOCGWINSZ, &ws) != -1;
}

// A CSI sequence whose head was consumed and then abandoned -- the size probe
// hitting its deadline, a mouse report straddling a timeout -- leaves its tail
// in the tty, where the next read takes it for typing ("9R", ";12;3M"). Every
// read of the input fd goes through this one reader, which owns finishing or
// claiming those tails (see ui/input_reader.h).
static jot_ui::InputReader g_input(STDIN_FILENO);

// cell_px_* are left at 0 when the terminal does not report a
// pixel size (the env fallback cannot know it).
static bool get_terminal_size(int &width, int &height, int *cell_px_w = nullptr,
                              int *cell_px_h = nullptr)
{
  struct winsize ws;

  if (probe_terminal_size_via_ioctl(STDOUT_FILENO, ws)
      || probe_terminal_size_via_ioctl(STDIN_FILENO, ws)
      || probe_terminal_size_via_ioctl(STDERR_FILENO, ws))
  {
    width = std::max(1, (int)ws.ws_col);
    height = std::max(1, (int)ws.ws_row);
    if (cell_px_w)
      *cell_px_w = (int)ws.ws_xpixel;
    if (cell_px_h)
      *cell_px_h = (int)ws.ws_ypixel;
    return true;
  }

  {
    int tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd >= 0)
    {
      bool ok = probe_terminal_size_via_ioctl(tty_fd, ws);
      ::close(tty_fd);
      if (ok)
      {
        width = std::max(1, (int)ws.ws_col);
        height = std::max(1, (int)ws.ws_row);
        if (cell_px_w)
          *cell_px_w = (int)ws.ws_xpixel;
        if (cell_px_h)
          *cell_px_h = (int)ws.ws_ypixel;
        return true;
      }
    }
  }

  const char *env_cols = getenv("COLUMNS");
  const char *env_lines = getenv("LINES");
  if (env_cols)
    width = std::max(1, atoi(env_cols));
  if (env_lines)
    height = std::max(1, atoi(env_lines));
  if (env_lines || env_cols)
    return true;

  return false;
}

// The span of a DSR cursor-position reply, `ESC [ rows ; cols R`, in `buf`.
static bool probe_reply_span(const char *buf, int n, std::size_t &at, std::size_t &end)
{
  for (std::size_t p = 0; p + 3 < (std::size_t)n; p++)
  {
    if (buf[p] != '\x1b' || buf[p + 1] != '[')
      continue;
    std::size_t q = p + 2;
    while (q < (std::size_t)n && ((buf[q] >= '0' && buf[q] <= '9') || buf[q] == ';'))
      q++;
    if (q < (std::size_t)n && buf[q] == 'R' && q > p + 2)
    {
      at = p;
      end = q + 1;
      return true;
    }
  }
  return false;
}

static bool cursor_probe_size(int &width, int &height, int budget_ms = 200,
                              bool cursor_shown = false)
{
  // Park the cursor at (999, 999) and ask for its position with DSR (CSI 6 n):
  // the reply gives the real viewport size even when ioctl and $COLUMNS are
  // stale. Park and ask in one write, and restore with DECRC (homing left the
  // caret visibly in the top-left corner until the next frame).
  // The park is a move to the corner, so the probe hides the cursor for the
  // round trip and shows it again only if it was on screen before
  // (cursor_probe.h): without that, a reader watches the caret jump to the
  // bottom right every couple of seconds, which is how often main_loop's slow
  // DSR fallback runs this.
  // The wait is a strict total deadline spread across the reply's bytes, so a
  // terminal that never answers (piped stdin, CI ptys, some tmux/SSH) costs at
  // most budget_ms instead of blocking startup.
  const std::string probe_park = jot_ui::cursor_probe_park_bytes(999, 999);
  ::write(STDOUT_FILENO, probe_park.data(), probe_park.size());

  const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(budget_ms);
  char buf[32] = {};
  int i = 0;
  while (i < 31)
  {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
    {
      break;
    }
    const int remain = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - now)
                           .count();
    if (!g_input.read(buf[i], std::max(1, remain)))
      break;
    i++;
    std::size_t at = 0;
    std::size_t end = 0;
    if (probe_reply_span(buf, i, at, end))
      break;
  }

  // Whatever stopped the loop -- the deadline, a poll that came back empty, a
  // read that failed -- the probe read bytes it never asked for: a mouse report
  // or a keystroke that raced its window. They are handed back, in order, so the
  // input still reaches the editor. Dropping them is what left a report's tail
  // ("191R", ";124;24M") to be read as typing, and swallowed clicks and keys.
  std::size_t reply_at = 0;
  std::size_t reply_end = 0;
  const bool answered = probe_reply_span(buf, i, reply_at, reply_end);

  char keep[sizeof(buf)];
  int kept = 0;
  for (int k = 0; k < i && kept < (int)sizeof(keep); k++)
  {
    if (answered && (std::size_t)k >= reply_at && (std::size_t)k < reply_end)
      continue;
    keep[kept++] = buf[k];
  }
  if (kept > 0)
    g_input.unread(keep, (std::size_t)kept);

  // DECRC puts the caret back, and the visibility the probe hid goes back with
  // the same write when the cursor was on screen.
  const std::string probe_restore = jot_ui::cursor_probe_restore_bytes(cursor_shown);
  ::write(STDOUT_FILENO, probe_restore.data(), probe_restore.size());

  int rows = 0, cols = 0;
  if (answered && sscanf(buf + reply_at, "\x1b[%d;%d", &rows, &cols) == 2)
  {
    if (rows > 0)
      height = rows;
    if (cols > 0)
      width = cols;
    return true;
  }
  return false;
}

// Reads one DSR cursor-position reply (`ESC [ rows ; cols R`) within a total
// deadline, handing back any other bytes the wait swallowed so a keystroke
// that arrived mid-probe still reaches the editor.
static bool read_cpr_reply(int &row, int &col, int budget_ms)
{
  const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(budget_ms);
  char buf[32] = {};
  int i = 0;
  bool answered = false;
  while (i < 31)
  {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
    {
      break;
    }
    const int remain = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                           .count();
    if (!g_input.read(buf[i], std::max(1, remain)))
    {
      break;
    }
    i++;
    std::size_t at = 0;
    std::size_t end = 0;
    answered = probe_reply_span(buf, i, at, end);
    if (answered)
    {
      if (sscanf(buf + at, "\x1b[%d;%d", &row, &col) != 2)
      {
        answered = false;
      }
      // Everything that was not the reply goes back, in order.
      char keep[sizeof(buf)];
      int kept = 0;
      for (int k = 0; k < i && kept < (int)sizeof(keep); k++)
      {
        if ((std::size_t)k >= at && (std::size_t)k < end)
        {
          continue;
        }
        keep[kept++] = buf[k];
      }
      if (kept > 0)
      {
        g_input.unread(keep, (std::size_t)kept);
      }
      return true;
    }
  }
  if (i > 0 && !answered)
  {
    g_input.unread(buf, (std::size_t)i);
  }
  return false;
}

// The protocol's own test for its scale part: ask for the cursor position,
// draw a two-cell space, ask again, draw a double-sized space, ask once more.
// A terminal without the protocol ignores both and the cursor never moves; one
// that implements only the width part moves for the first space and not the
// second. Both have to move -- the scale alone would not make the grid we draw
// line up -- so the check is strictly four cells of movement, two per frame.
static bool text_scale_probe(bool cursor_shown)
{
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
  {
    return false;
  }
  static const char kWidthFrame[] = "\x1b]66;w=2; \a\x1b[6n";
  static const char kScaleFrame[] = "\x1b]66;s=2; \a\x1b[6n";
  // The probe writes straight to the tty: the frame the editor is composing
  // rides behind it in the same frame, so the two scaled blanks it leaves are
  // painted over before anything is read on screen. Its park is a move to the
  // first cell, so it hides the cursor for the round trip like the size probe
  // does and shows it again only if it was on screen (cursor_probe.h).
  const std::string probe_park = jot_ui::cursor_probe_park_bytes(1, 1);
  ::write(STDOUT_FILENO, probe_park.data(), probe_park.size());

  int row = 0, col = 0;
  bool ok = false;
  if (read_cpr_reply(row, col, 120) && col == 1)
  {
    ::write(STDOUT_FILENO, kWidthFrame, sizeof(kWidthFrame) - 1);
    int width_col = 0;
    if (read_cpr_reply(row, width_col, 120) && width_col == 3)
    {
      ::write(STDOUT_FILENO, kScaleFrame, sizeof(kScaleFrame) - 1);
      int scale_col = 0;
      if (read_cpr_reply(row, scale_col, 120) && scale_col == 5)
      {
        ok = true;
      }
    }
  }

  // Erase the probe's two blocks and put the caret back. Plain spaces do it:
  // writing over a block's top-left cell erases the whole block, row under it
  // included. No erase-to-end and no frame of our own here -- a refused zoom
  // schedules no repaint, so anything destructive would be left on the screen
  // until something else happened to redraw it.
  std::string cleanup = "\x1b[1;1H    ";
  cleanup += jot_ui::cursor_probe_restore_bytes(cursor_shown);
  ::write(STDOUT_FILENO, cleanup.data(), cleanup.size());
  return ok;
}

static int termkey_modifier_flags(int mod)
{
  int flags = 0;
  if (mod & TERMKEY_KEYMOD_SHIFT)
    flags |= 0x80000; // Shift
  if (mod & TERMKEY_KEYMOD_ALT)
    flags |= 0x40000; // Alt
  if (mod & TERMKEY_KEYMOD_CTRL)
    flags |= 0x20000; // Ctrl
  return flags;
}

static int translate_termkey_keysym(TermKeySym sym)
{
  switch (sym)
  {
  case TERMKEY_SYM_BACKSPACE:
    return 127;
  case TERMKEY_SYM_TAB:
    return '\t';
  case TERMKEY_SYM_ENTER:
  case TERMKEY_SYM_KPENTER:
    return 13;
  case TERMKEY_SYM_ESCAPE:
    return 27;
  case TERMKEY_SYM_SPACE:
    return ' ';
  case TERMKEY_SYM_DEL:
    return 127;
  case TERMKEY_SYM_DELETE:
    return 1001;
  case TERMKEY_SYM_UP:
    return 1008;
  case TERMKEY_SYM_DOWN:
    return 1009;
  case TERMKEY_SYM_RIGHT:
    return 1010;
  case TERMKEY_SYM_LEFT:
    return 1011;
  case TERMKEY_SYM_HOME:
  case TERMKEY_SYM_BEGIN:
    return 1012;
  case TERMKEY_SYM_END:
    return 1013;
  case TERMKEY_SYM_PAGEUP:
    return 1015;
  case TERMKEY_SYM_PAGEDOWN:
    return 1016;
  case TERMKEY_SYM_KP0:
    return '0';
  case TERMKEY_SYM_KP1:
    return '1';
  case TERMKEY_SYM_KP2:
    return '2';
  case TERMKEY_SYM_KP3:
    return '3';
  case TERMKEY_SYM_KP4:
    return '4';
  case TERMKEY_SYM_KP5:
    return '5';
  case TERMKEY_SYM_KP6:
    return '6';
  case TERMKEY_SYM_KP7:
    return '7';
  case TERMKEY_SYM_KP8:
    return '8';
  case TERMKEY_SYM_KP9:
    return '9';
  case TERMKEY_SYM_KPPLUS:
    return '+';
  case TERMKEY_SYM_KPMINUS:
    return '-';
  case TERMKEY_SYM_KPMULT:
    return '*';
  case TERMKEY_SYM_KPDIV:
    return '/';
  case TERMKEY_SYM_KPCOMMA:
    return ',';
  case TERMKEY_SYM_KPPERIOD:
    return '.';
  case TERMKEY_SYM_KPEQUALS:
    return '=';
  default:
    return 0;
  }
}

static int translate_termkey_key(const TermKeyKey &key)
{
  int flags = termkey_modifier_flags(key.modifiers);

  switch (key.type)
  {
  case TERMKEY_TYPE_UNICODE:
  {
    int codepoint = static_cast<int>(key.code.codepoint);
    if (codepoint <= 0)
    {
      return -1;
    }
    if (codepoint >= 'A' && codepoint <= 'Z')
    {
      codepoint |= 0x8000;
    }
    return codepoint | flags;
  }
  case TERMKEY_TYPE_KEYSYM:
  {
    int mapped = translate_termkey_keysym(key.code.sym);
    if (mapped == '\t' && (key.modifiers & TERMKEY_KEYMOD_SHIFT))
    {
      mapped = 1017;
      flags &= ~0x80000;
    }
    return mapped ? (mapped | flags) : -1;
  }
  case TERMKEY_TYPE_FUNCTION:
    if (key.code.number > 0)
    {
      return KeyCode::function(key.code.number) | flags;
    }
    return -1;
  default:
    return -1;
  }
}

static void append_mouse_reset(std::string &buffer)
{
  buffer += "\x1b[?1003l"; // any-motion mouse tracking
  buffer += "\x1b[?1002l"; // button-event mouse tracking
  buffer += "\x1b[?1000l"; // basic mouse tracking
  buffer += "\x1b[?1006l"; // SGR mouse protocol
  buffer += "\x1b[?1015l"; // urxvt mouse protocol
  buffer += "\x1b[?1004l"; // focus events
  buffer += "\x1b[?2004l"; // bracketed paste
}

Terminal::Terminal() : width(80), height(24), poll_timeout_ms(8), raw_mode(false), termkey_(nullptr)
{
}

Terminal::~Terminal()
{
  cleanup();
}

void Terminal::enable_raw_mode()
{
  if (raw_mode)
    return;

  tcgetattr(STDIN_FILENO, &orig_termios);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  raw_mode = true;
}

void Terminal::disable_raw_mode()
{
  if (!raw_mode)
    return;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  raw_mode = false;
}

void Terminal::setup_terminal()
{
  truecolor_ = terminal_env_supports_truecolor();
  write("\x1b[?1049h");
  write("\x1b[?25l");
  write("\x1b[2J");
  write("\x1b[H");
  write("\x1b[?2004h");
  // Kitty keyboard protocol push (flags=1, disambiguate ESC): terminals
  // that support it (kitty, wezterm, Windows Terminal, …) then report
  // modified keys such as Ctrl+Enter / Ctrl+Shift+Enter as CSI-u sequences
  // instead of a bare \r, so the editor can tell them apart. Unsupported
  // terminals ignore the sequence and keep sending legacy encodings.
  write("\x1b[>1u");
}

void Terminal::restore_terminal()
{
  append_mouse_reset(buffer);
  write("\x1b[<1u"); // pop the kitty keyboard protocol
  write("\x1b[?25h");
  write("\x1b[1 q"); // restore block cursor for the host shell
  write("\x1b[?7h"); // re-enable autowrap so the host shell is left normal
  write("\x1b[?1049l");
  append_mouse_reset(buffer);
  show_cursor();
  reset_color();
  flush();
}

void Terminal::init()
{
  setup_terminal();
  flush();

  enable_raw_mode();

  // Render capture: if JOT_RENDER_CAPTURE=/path/to/log is set, open a
  // summary capture file. Frame markers include frame number, bytes, WxH,
  // and rows rendered. Set JOT_RENDER_CAPTURE_RAW=1 to also dump the raw
  // terminal bytes to the capture file.
  {
    const char *cp = getenv("JOT_RENDER_CAPTURE");
    if (cp && cp[0] != '\0')
    {
      render_capture_ = fopen(cp, "wb");
      if (render_capture_)
      {
        const char *raw = getenv("JOT_RENDER_CAPTURE_RAW");
        render_capture_raw_ = (raw && raw[0] == '1');
        fprintf(render_capture_,
                "--JOT-RENDER-CAPTURE-START (raw=%d)--\n",
                render_capture_raw_ ? 1 : 0);
        fflush(render_capture_);
      }
    }
  }

  // Force a size probe after entering alternate screen and raw mode. The
  // ioctl path that ran before raw mode was enabled can return stale
  // dimensions (e.g. the controlling TTY's notion of size was set when the
  // foreground process group changed). The cursor probe is the only
  // reliable way to learn the real rows/cols at this point.
  refresh_size(/*force_probe=*/true);

  TERMKEY_CHECK_VERSION;
  const char *term = getenv("TERM");
  if (!term || term[0] == '\0')
  {
    term = "xterm-256color";
  }
  termkey_ =
      termkey_new_abstract(term, TERMKEY_FLAG_UTF8 | TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_CONVERTKP);
  if (!termkey_)
  {
    termkey_ = termkey_new_abstract(
        "xterm-256color", TERMKEY_FLAG_UTF8 | TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_CONVERTKP);
  }
  if (termkey_)
  {
    termkey_set_waittime(termkey_, 10);
  }

  if (width <= 0)
    width = 80;
  if (height <= 0)
    height = 24;

  // The right-edge margin keeps its default (zero: paint the full width) and is
  // owned by the `render_margin` setting, applied in Editor::apply_config_live().

  // Per-row chunked-flush threshold. JOT_RENDER_CHUNK_BYTES=<n> overrides
  // the default of 0 (disabled). Set to e.g. 2048 or 4096 to enable
  // chunked flushing for diagnosis on terminals that drop bytes from
  // large writes. Disabled by default because mid-frame flushes block
  // the event loop while the kernel drains the PTY buffer.
  {
    const char *m = getenv("JOT_RENDER_CHUNK_BYTES");
    if (m && m[0] != '\0')
    {
      int v = atoi(m);
      if (v >= 0)
      {
        render_chunk_bytes_ = (size_t)v;
      }
    }
  }

  struct sigaction sa = {};
  sa.sa_handler = sigwinch_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa, nullptr);

  enable_mouse();
}

bool Terminal::refresh_size(bool force_probe)
{
  int new_width = width;
  int new_height = height;

  // First source: ioctl and env vars.
  bool got = get_terminal_size(new_width, new_height, &cell_px_w_, &cell_px_h_);

  // On a forced probe, or when the ioctl/env path failed, fall back to the ANSI
  // cursor-position probe: it is the only recovery from stale dimensions (after
  // the alternate screen was entered, or when the controlling TTY changed).
  // It parks and restores the cursor, so it stays reserved for those cases;
  // probing on every event was pure overhead.
  if ((force_probe || !size_known_) && (force_probe || !got) && isatty(STDIN_FILENO)
      && isatty(STDOUT_FILENO))
  {
    int probe_w = new_width;
    int probe_h = new_height;
    // When ioctl/env already produced a usable size this probe only
    // confirms or corrects it, so a terminal that never answers DSR must
    // not stall startup for the full budget -- terminals that do answer
    // reply within a few milliseconds. Only when the probe is the sole
    // source of a size (ioctl/env failed) keep the generous budget.
    const int probe_budget = got ? 30 : 200;
    if (cursor_probe_size(probe_w, probe_h, probe_budget, cursor_visible()))
    {
      if (probe_w > 0)
        new_width = probe_w;
      if (probe_h > 0)
        new_height = probe_h;
      got = true;
    }
  }

  if (!got)
  {
    return false;
  }

  if (new_width < 1)
    new_width = 1;
  if (new_height < 1)
    new_height = 1;
  bool changed = (new_width != width) || (new_height != height);
  width = new_width;
  height = new_height;
  size_known_ = true;
  return changed;
}

void Terminal::cleanup()
{
  if (termkey_)
  {
    termkey_destroy(termkey_);
    termkey_ = nullptr;
  }
  disable_mouse();
  disable_raw_mode();
  restore_terminal();
  if (render_capture_)
  {
    fprintf(render_capture_, "\n--JOT-RENDER-CAPTURE-END--\n");
    fclose(render_capture_);
    render_capture_ = nullptr;
  }
}

bool Terminal::probe_text_scale()
{
  // Diagnostic escape hatch, in the spirit of JOT_RENDER_CHUNK_BYTES: asking the
  // terminal only works on a real one, so a pty-driven test (or someone behind a
  // multiplexer that strips OSC 66) can force the answer here.
  const char *force = getenv("JOT_TEXT_SCALE_FORCE");
  if (force && force[0] != '\0' && force[0] != '0')
  {
    return true;
  }
  return text_scale_probe(cursor_visible());
}

int Terminal::read_termkey_result()
{
  if (!termkey_)
  {
    return -1;
  }

  TermKeyKey key;
  while (true)
  {
    TermKeyResult result = termkey_getkey(termkey_, &key);
    if (result == TERMKEY_RES_KEY)
    {
      return translate_termkey_key(key);
    }
    if (result == TERMKEY_RES_AGAIN)
    {
      char next;
      if (g_input.read(next, termkey_get_waittime(termkey_)))
      {
        termkey_push_bytes(termkey_, &next, 1);
        continue;
      }
      result = termkey_getkey_force(termkey_, &key);
      if (result == TERMKEY_RES_KEY)
      {
        return translate_termkey_key(key);
      }
      return -1;
    }
    return -1;
  }
}

int Terminal::read_key()
{
  // Finishing a sequence whose tail had not arrived when its head was read and
  // dropped (see g_input). Only that shape is eaten: a keystroke that gets
  // there first is handed back and delivered as usual.
  g_input.settle();

  char c;
  if (!g_input.read(c, -1))
    return -1;

  std::string bytes;
  bytes.push_back(c);

  if (c == '\x1b')
  {
    char second;
    if (g_input.read(second, 5))
    {
      bytes.push_back(second);
      if (second == '\x1b')
      {
        if (!g_input.read(second, 5))
        {
          if (!termkey_)
          {
            return -1;
          }
          termkey_push_bytes(termkey_, bytes.data(), bytes.size());
          return read_termkey_result();
        }
        bytes.push_back(second);
      }
      if (second == '[')
      {
        char third;
        if (g_input.read(third, 5))
        {
          bytes.push_back(third);
          // Focus reporting (DECSET 1004): CSI I = focus-in, CSI O =
          // focus-out. Single-char finals, no parameters.
          if (third == 'I')
          {
            return 1021;
          }
          if (third == 'O')
          {
            return 1022;
          }
          if (third == '<')
          {
            mouse_event_buffer.clear();
            char mouse_seq[32] = {0};
            int mouse_pos = 0;
            while (mouse_pos < 31)
            {
              if (!g_input.read(mouse_seq[mouse_pos], 5))
              {
                // The report's head is already consumed, so its tail is ours
                // too; and a truncated report is not an Escape press.
                g_input.abandon(jot_ui::OwedTail::MouseReport);
                return -1;
              }
              if (mouse_seq[mouse_pos] == 'M' || mouse_seq[mouse_pos] == 'm')
              {
                mouse_seq[mouse_pos + 1] = '\0';
                break;
              }
              mouse_pos++;
            }
            mouse_event_buffer = std::string(mouse_seq);
            return 1014;
          }

          while (third < 0x40 || third > 0x7e)
          {
            if (!g_input.read(third, 5))
            {
              // A CSI is provably in progress (a bare `ESC [` would not have
              // entered this loop), so its tail is ours to eat, not to hand on
              // to libtermkey as a fragment whose remainder types itself.
              g_input.abandon(jot_ui::OwedTail::CsiBody);
              return -1;
            }
            bytes.push_back(third);
          }

          // Bracketed paste start: ESC [ 200 ~. Detect it on the assembled
          // sequence rather than with a destructive lookahead: the old
          // "third == '2', read a/b/tilde" peek consumed the '7' of a CSI-u
          // report such as ESC [ 27 u - how a kitty-protocol terminal
          // (Alacritty, WezTerm, …) encodes the Escape key - turning a
          // plain Esc press into control code 2 (Ctrl+B), which the command
          // palette typed as "b" and normal mode used to toggle the sidebar.
          if (bytes.size() >= 6 && bytes.compare(0, 6, "\x1b[200~") == 0)
          {
            if (g_input.read_paste(paste_event_buffer))
            {
              return 1020;
            }
          }
          // Kitty keyboard protocol (CSI u): decode here so modified keys
          // such as Ctrl+Enter stay distinguishable (libtermkey 0.22 does
          // not understand CSI-u and mangles them into Ctrl+letter).
          const int csi_u_key = jot::keybind_detail::decode_csi_u_key(bytes);
          if (csi_u_key >= 0)
          {
            return csi_u_key;
          }
        }
      }
      else if (second == 'O')
      {
        char third;
        if (g_input.read(third, 5))
        {
          bytes.push_back(third);
        }
        else
        {
          // An SS3 sequence is `ESC O <final>`: the byte still owed is its
          // final one, and without this it would be read back as typing.
          g_input.abandon(jot_ui::OwedTail::FinalByte);
        }
      }
    }
  }

  if (!termkey_)
  {
    return -1;
  }
  termkey_push_bytes(termkey_, bytes.data(), bytes.size());
  return read_termkey_result();
}

void Terminal::parse_mouse_event(int ch, MouseEvent &event)
{
  if (mouse_event_buffer.empty())
  {
    event.x = -1;
    event.y = -1;
    event.button = 0;
    event.pressed = false;
    event.released = false;
    event.ctrl = false;
    event.shift = false;
    event.alt = false;
    return;
  }

  const char *seq = mouse_event_buffer.c_str();

  char terminator = '\0';
  if (!mouse_event_buffer.empty())
  {
    terminator = mouse_event_buffer.back();
  }
  if (terminator != 'M' && terminator != 'm')
  {
    event.x = -1;
    event.y = -1;
    event.button = 0;
    event.pressed = false;
    event.released = false;
    event.ctrl = false;
    event.shift = false;
    event.alt = false;
    return;
  }

  int button, x, y;
  if (sscanf(seq, "%d;%d;%d", &button, &x, &y) == 3)
  {
    if (x <= 0 || y <= 0)
    {
      event.x = -1;
      event.y = -1;
      event.button = button;
      event.pressed = false;
      event.released = false;
      event.ctrl = (button & 16) != 0;
      event.shift = (button & 4) != 0;
      event.alt = (button & 8) != 0;
      return;
    }
    event.button = button;
    // Terminal cell -> grid cell: the report names the cell under the pointer,
    // and at a scale above 1 the grid cell it is part of is scale cells wide.
    event.x = (x - 1) / cell_scale_;
    event.y = (y - 1) / cell_scale_;
    event.ctrl = (button & 16) != 0;
    event.shift = (button & 4) != 0;
    event.alt = (button & 8) != 0;

    bool is_motion = (button & 0x20) != 0;
    bool is_wheel = (button >= 64 && button <= 67);
    bool is_release = (terminator == 'm');

    if (is_wheel)
    {
      event.pressed = true;
      event.released = false;
    }
    else if (is_motion)
    {
      event.pressed = false;
      event.released = false;
    }
    else if (!is_release)
    {
      event.pressed = true;
      event.released = false;
    }
    else
    {
      event.pressed = false;
      event.released = true;
    }
  }
  else
  {
    event.x = -1;
    event.y = -1;
    event.button = 0;
    event.pressed = false;
    event.released = false;
    event.ctrl = false;
    event.shift = false;
    event.alt = false;
  }
}

Event Terminal::poll_event()
{
  Event ev{};
  ev.type = EVENT_REDRAW;

  // Bytes a reader handed back (see InputReader::unread) are input waiting to
  // be delivered, so they are read without waiting for the fd to speak again.
  const bool queued = g_input.pending();
  struct pollfd pfd;
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (!queued)
    poll(&pfd, 1, std::max(0, poll_timeout_ms));

  if (queued || (pfd.revents & POLLIN))
  {
    Event input = read_event();
    if (input.type != EVENT_REDRAW)
      return input;
  }

  Event rsz = check_resize_event();
  if (rsz.type != EVENT_REDRAW)
    return rsz;

  return ev;
}

Event Terminal::check_resize_event()
{
  Event ev;
  ev.type = EVENT_REDRAW;

  // Always attempt a live size refresh. SIGWINCH is a fast signal that
  // tells us a resize is likely pending, but it is not required: the cached
  // size can be stale (or stuck at the 80x24 constructor fallback) even
  // when no signal has been delivered. Reset g_resize_pending so a single
  // signal doesn't keep forcing resizes after we've already absorbed it.
  const bool resize_pending = g_resize_pending != 0;
  g_resize_pending = 0;
  if (refresh_size(/*force_probe=*/resize_pending))
  {
    ev.type = EVENT_RESIZE;
    ev.resize.width = width;
    ev.resize.height = height;
    return ev;
  }
  return ev;
}

Event Terminal::read_event()
{
  Event ev{};
  ev.type = EVENT_REDRAW;

  int ch = read_key();
  if (ch < 0)
  {
    ev.type = EVENT_REDRAW;
    return ev;
  }

  if (ch == 1020)
  {
    ev.type = EVENT_PASTE;
    ev.paste.text = paste_event_buffer.c_str();
    return ev;
  }

  if (ch == 1021)
  {
    ev.type = EVENT_FOCUS_IN;
    return ev;
  }

  if (ch == 1022)
  {
    ev.type = EVENT_FOCUS_OUT;
    return ev;
  }

  if (ch == 1014)
  {
    parse_mouse_event(ch, ev.mouse);
    if (const char *p = std::getenv("JOT_MOUSE_DEBUG"); p && *p)
    {
      FILE *f = std::fopen(p, "a");
      if (f)
      {
        std::fprintf(f,
                     "[proto] SGR buffer='%s' parsed=(%d,%d) btn=%d "
                     "pressed=%d released=%d\n",
                     mouse_event_buffer.c_str(),
                     ev.mouse.x,
                     ev.mouse.y,
                     ev.mouse.button,
                     ev.mouse.pressed ? 1 : 0,
                     ev.mouse.released ? 1 : 0);
        std::fclose(f);
      }
    }
    if (ev.mouse.x < 0 || ev.mouse.y < 0)
    {
      ev.type = EVENT_REDRAW;
    }
    else
    {
      ev.type = EVENT_MOUSE;
    }
    return ev;
  }

  if (ch == 28)
  {
    ev = check_resize_event();
    if (ev.type != EVENT_REDRAW)
      return ev;
  }

  ev.type = EVENT_KEY;
  ev.key = decode_key_event(ch);
  return ev;
}

KeyEvent decode_key_event(int raw_ch)
{
  KeyEvent out;

  const bool mod_shift = (raw_ch & 0x80000) != 0;
  const bool mod_alt = (raw_ch & 0x40000) != 0;
  const bool mod_ctrl = (raw_ch & 0x20000) != 0;

  const bool function_key = (raw_ch & KeyCode::FunctionMarker) != 0;
  int base_key = raw_ch & 0xFFFF;

  // The 0x8000 bit marks shifted letters (termkey's 'S' vs 's' encoding).
  // Only treat it as such for the A-Z range: high codepoints (CJK in
  // U+8000-U+9FFF, nerd icons in U+E000-U+F8FF) naturally have that bit
  // set and must pass through unchanged.
  const bool upper_bit = (base_key & 0x8000) != 0 && (base_key & 0x7FFF) >= 'A'
                         && (base_key & 0x7FFF) <= 'Z';

  out.key = function_key ? (raw_ch & (KeyCode::FunctionMarker | 0xFFFF)) : base_key;
  out.ctrl = mod_ctrl || (base_key >= 1 && base_key <= 26 && base_key != 13 && base_key != 9);
  out.shift = mod_shift || (base_key >= 2008 && base_key <= 2011) || upper_bit;
  out.alt = mod_alt;

  if (out.shift && (base_key >= 2008 && base_key <= 2011))
  {
    out.key = base_key - 1000;
  }
  else if (upper_bit)
  {
    out.key = base_key & 0x7FFF;
  }

  return out;
}

void Terminal::set_poll_timeout_ms(int timeout_ms)
{
  poll_timeout_ms = std::clamp(timeout_ms, 1, 250);
}

bool Terminal::flush()
{
  // Bytes dropped by a chunked flush earlier in this frame (see
  // flush_if_buffer_exceeds): part of what the frame queued never reached the
  // terminal, so the frame as a whole cannot be trusted -- even if this write
  // lands in full. Read once, so a single drop is reported by exactly one
  // frame and the next one can be judged on its own.
  const bool dropped_earlier = frame_bytes_dropped_;
  frame_bytes_dropped_ = false;

  int n = (int)buffer.length();
  last_flush_bytes_ = n;
  if (n <= 0)
  {
    return !dropped_earlier;
  }
  if (flush_stall_for_test_)
  {
    // Model a terminal that accepted nothing from this frame: the bytes are
    // dropped, exactly as they are when the write below gives up.
    flush_stall_for_test_ = false;
    buffer.clear();
    return false;
  }

  // Write the raw capture file first. fwrite is stdio-buffered and
  // does not touch the PTY, so it cannot interleave with the
  // terminal output and keeps the capture in sync with what we are
  // about to emit.
  if (render_capture_ && render_capture_raw_)
  {
    fwrite(buffer.c_str(), 1, n, render_capture_);
  }

  // Present the frame atomically (DECSET 2026, synchronized output). A frame is
  // a long stream of cursor moves, row diffs and cursor show/hide toggles, and
  // without this the terminal is free to repaint any intermediate state -- so a
  // moment where the cursor sits where a row write left it shows the caret away
  // from the text for one frame. Terminals that do not know the mode ignore it.
  // Both markers wrap the payload inside this one write, so the mode can never
  // be left open by an early return.
  std::string framed;
  framed.reserve((size_t)n + 16);
  framed += "\x1b[?2026h";
  framed.append(buffer, 0, (size_t)n);
  framed += "\x1b[?2026l";
  // One low-level write() loop for the whole frame: mixing stdio with these
  // writes reordered bytes on some terminals, and the symptom was the cursor
  // landing away from the click position while typing.
  const char *p = framed.c_str();
  size_t remaining = framed.size();
  while (remaining > 0)
  {
    const ssize_t w = ::write(STDOUT_FILENO, p, remaining);
    if (w > 0)
    {
      p += w;
      remaining -= (size_t)w;
    }
    else if (w == -1 && errno == EINTR)
    {
      continue; // interrupted by a signal; retry the same bytes
    }
    else if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      // stdout was switched to O_NONBLOCK somewhere; wait briefly for it.
      struct pollfd pfd;
      pfd.fd = STDOUT_FILENO;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (::poll(&pfd, 1, 1000) <= 0)
      {
        break; // give up after 1s rather than hang forever
      }
    }
    else
    {
      break;
    }
  }
  buffer.clear();
  // False tells the caller the screen cannot be trusted for the cells this
  // frame queued: it repaints every row next frame instead of diffing against
  // a baseline that assumes they landed.
  return remaining == 0 && !dropped_earlier;
}

void Terminal::flush_if_buffer_exceeds()
{
  if (render_chunk_bytes_ == 0)
  {
    return;
  }
  if (buffer.size() >= render_chunk_bytes_)
  {
    // This is a mid-frame flush: its caller is still queueing the frame and
    // has nowhere to report a failure, so remember it here and let the frame's
    // final flush() report it.
    if (!flush())
    {
      frame_bytes_dropped_ = true;
    }
  }
}

void Terminal::try_drain()
{
  if (render_chunk_bytes_ == 0)
  {
    return;
  }
  if (buffer.size() < render_chunk_bytes_)
  {
    return;
  }
  if (buffer.empty())
  {
    return;
  }

  // Write the capture file first (fwrite is buffered and fast; does
  // not block on the PTY). This keeps the diagnostic capture in
  // sync with what we actually attempt to write to the terminal.
  if (render_capture_ && render_capture_raw_)
  {
    fwrite(buffer.c_str(), 1, buffer.size(), render_capture_);
  }

  // Set O_NONBLOCK on stdout so write() returns immediately with
  // a short count (or EAGAIN) if the kernel PTY buffer is full,
  // instead of blocking while the terminal drains.
  int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
  if (flags == -1)
  {
    return;
  }
  if (fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    return;
  }

  size_t total = buffer.size();
  size_t written = 0;
  while (written < total)
  {
    ssize_t n = ::write(STDOUT_FILENO, buffer.c_str() + written, total - written);
    if (n > 0)
    {
      written += (size_t)n;
    }
    else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      // PTY buffer is full; terminal hasn't drained yet. Keep the
      // remaining data in the buffer for the next try_drain() or
      // the final flush() at frame end.
      break;
    }
    else
    {
      // Real error (EPIPE, EBADF, etc). Bail and let the final
      // flush() decide what to do.
      break;
    }
  }

  // Restore the original non-blocking flag state.
  fcntl(STDOUT_FILENO, F_SETFL, flags);

  if (written > 0)
  {
    if (written == total)
    {
      buffer.clear();
    }
    else
    {
      buffer.erase(0, written);
    }
  }
}

void Terminal::clear()
{
  buffer += "\x1b[2J";
  buffer += "\x1b[H";
}

void Terminal::move_cursor(int x, int y)
{
  // Grid cell -> terminal cell: at a scale above 1 a cell is drawn as a scale
  // x scale block, so its top-left corner is `scale` cells further along.
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y * cell_scale_ + 1, x * cell_scale_ + 1);
  buffer += buf;
}

void Terminal::hide_cursor()
{
  cursor_visible_ = false;
  buffer += "\x1b[?25l";
}

void Terminal::show_cursor()
{
  cursor_visible_ = true;
  buffer += "\x1b[?25h";
}

void Terminal::set_color(int fg, int bg, std::uint32_t fg_rgb, std::uint32_t bg_rgb)
{
  // A 24-bit value is only emitted when the terminal understands it; otherwise
  // it is folded down to the closest palette entry here, so the caller can
  // always hand over the exact colour it meant.
  char buf[48];
  if (fg_rgb != kNoRgb)
  {
    if (truecolor_)
    {
      snprintf(buf,
               sizeof(buf),
               "\x1b[38;2;%u;%u;%um",
               (unsigned)((fg_rgb >> 16) & 0xFF),
               (unsigned)((fg_rgb >> 8) & 0xFF),
               (unsigned)(fg_rgb & 0xFF));
    }
    else
    {
      fg = jot_ui::palette_nearest_index(
          (unsigned char)((fg_rgb >> 16) & 0xFF),
          (unsigned char)((fg_rgb >> 8) & 0xFF),
          (unsigned char)(fg_rgb & 0xFF));
      snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", fg);
    }
    buffer += buf;
  }
  else
  {
    snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", fg);
    buffer += buf;
  }

  if (bg_rgb != kNoRgb)
  {
    if (truecolor_)
    {
      snprintf(buf,
               sizeof(buf),
               "\x1b[48;2;%u;%u;%um",
               (unsigned)((bg_rgb >> 16) & 0xFF),
               (unsigned)((bg_rgb >> 8) & 0xFF),
               (unsigned)(bg_rgb & 0xFF));
    }
    else
    {
      bg = jot_ui::palette_nearest_index(
          (unsigned char)((bg_rgb >> 16) & 0xFF),
          (unsigned char)((bg_rgb >> 8) & 0xFF),
          (unsigned char)(bg_rgb & 0xFF));
      snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", bg);
    }
    buffer += buf;
  }
  else
  {
    snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", bg);
    buffer += buf;
  }
}

void Terminal::reset_color()
{
  buffer += "\x1b[0m";
}

void Terminal::set_bold(bool on)
{
  if (on)
  {
    buffer += "\x1b[1m";
  }
  else
  {
    buffer += "\x1b[22m";
  }
}

void Terminal::set_italic(bool on)
{
  if (on)
  {
    buffer += "\x1b[3m";
  }
  else
  {
    buffer += "\x1b[23m";
  }
}

void Terminal::set_dim(bool on)
{
  buffer += on ? "\x1b[2m" : "\x1b[22m";
}

void Terminal::set_reverse(bool on)
{
  if (on)
  {
    buffer += "\x1b[7m";
  }
  else
  {
    buffer += "\x1b[27m";
  }
}

void Terminal::set_underline(int style)
{
  if (style == 1)
  {
    buffer += "\x1b[4m";
  }
  else if (style == 2)
  {
    buffer += "\x1b[4:3m";
  }
  else
  {
    buffer += "\x1b[24m";
  }
}

void Terminal::set_underline_color(int fg, std::uint32_t rgb)
{
  // Same contract as set_color(): a 24-bit value is used verbatim when the
  // terminal understands it and quantised here when it does not, so a caller can
  // always hand over the exact colour it meant. The colon subparameter form is
  // the one colour-aware terminals parse for SGR 58 (58:2::r:g:b) and matches
  // the wavy underline's 4:3; the indexed form is what everything else reads.
  if (fg < 0 && rgb == kNoRgb)
  {
    buffer += "\x1b[59m";
    return;
  }
  char buf[32];
  if (rgb != kNoRgb && truecolor_)
  {
    snprintf(buf,
             sizeof(buf),
             "\x1b[58:2::%u:%u:%um",
             (unsigned)((rgb >> 16) & 0xFF),
             (unsigned)((rgb >> 8) & 0xFF),
             (unsigned)(rgb & 0xFF));
  }
  else
  {
    if (rgb != kNoRgb)
    {
      fg = jot_ui::palette_nearest_index(
          (unsigned char)((rgb >> 16) & 0xFF),
          (unsigned char)((rgb >> 8) & 0xFF),
          (unsigned char)(rgb & 0xFF));
    }
    snprintf(buf, sizeof(buf), "\x1b[58;5;%dm", fg);
  }
  buffer += buf;
}

void Terminal::write(const std::string &str)
{
  buffer += str;
}

void Terminal::write_char(char c)
{
  buffer += c;
}

void Terminal::enable_mouse()
{
  buffer += "\x1b[?1002h";
  buffer += "\x1b[?1006h";
  buffer += "\x1b[?2004h";
  flush();
}

void Terminal::disable_mouse()
{
  append_mouse_reset(buffer);
  flush();
}

void Terminal::enable_mouse_hover()
{
  buffer += "\x1b[?1003h";
  flush();
}

void Terminal::disable_mouse_hover()
{
  buffer += "\x1b[?1003l";
  buffer += "\x1b[?1002h";
  buffer += "\x1b[?1006h";
  flush();
}

void Terminal::enable_focus_reporting()
{
  buffer += "\x1b[?1004h";
  flush();
}

void Terminal::disable_focus_reporting()
{
  // NOTE: ?1004l is deliberately NOT emitted. On kitty and foot, disabling
  // focus reporting while unfocused races the compositor's surface teardown and
  // can discard the queued repaint, blanking the window until the next input.
  // The mode is per-application and dies with the PTY anyway.
}

void Terminal::save_cursor()
{
  buffer += "\x1b[s";
}

void Terminal::restore_cursor()
{
  buffer += "\x1b[u";
}

void Terminal::clear_line()
{
  buffer += "\x1b[2K";
}

void Terminal::clear_to_end()
{
  buffer += "\x1b[K";
}

void Terminal::disable_autowrap()
{
  buffer += "\x1b[?7l";
}

void Terminal::enable_autowrap()
{
  buffer += "\x1b[?7h";
}

void Terminal::render_capture_marker(const std::string &label, int rows_rendered)
{
  if (!render_capture_)
    return;
  render_capture_seq_++;
  int bytes = last_flush_bytes_;
  const char *ms_env = getenv("JOT_RENDER_CAPTURE_MS");
  if (ms_env && ms_env[0] == '1')
  {
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
    fprintf(render_capture_,
            "\n--MARKER %d: %s bytes=%d w=%d h=%d rows=%d ms=%lld--\n",
            render_capture_seq_,
            label.c_str(),
            bytes,
            width,
            height,
            rows_rendered,
            now_ms);
  }
  else
  {
    fprintf(render_capture_,
            "\n--MARKER %d: %s bytes=%d w=%d h=%d rows=%d--\n",
            render_capture_seq_,
            label.c_str(),
            bytes,
            width,
            height,
            rows_rendered);
  }
  fflush(render_capture_);
}
