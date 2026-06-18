#include "terminal.h"
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#include <algorithm>
#include <iostream>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <termkey.h>
#include <unistd.h>

static struct termios orig_termios;

static volatile sig_atomic_t g_resize_pending = 0;

static void sigwinch_handler(int) {
  g_resize_pending = 1;
}

static bool probe_terminal_size_via_ioctl(int fd, struct winsize &ws) {
  return ioctl(fd, TIOCGWINSZ, &ws) != -1;
}

static bool get_terminal_size(int &width, int &height) {
  struct winsize ws;

  if (probe_terminal_size_via_ioctl(STDOUT_FILENO, ws) ||
      probe_terminal_size_via_ioctl(STDIN_FILENO, ws) ||
      probe_terminal_size_via_ioctl(STDERR_FILENO, ws)) {
    width = std::max(1, (int)ws.ws_col);
    height = std::max(1, (int)ws.ws_row);
    return true;
  }

  {
    int tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd >= 0) {
      bool ok = probe_terminal_size_via_ioctl(tty_fd, ws);
      ::close(tty_fd);
      if (ok) {
        width = std::max(1, (int)ws.ws_col);
        height = std::max(1, (int)ws.ws_row);
        return true;
      }
    }
  }

  const char *env_cols = getenv("COLUMNS");
  const char *env_lines = getenv("LINES");
  if (env_cols) width = std::max(1, atoi(env_cols));
  if (env_lines) height = std::max(1, atoi(env_lines));
  if (env_lines || env_cols) return true;

  return false;
}

static bool cursor_probe_size(int &width, int &height) {
  // Move cursor to (999, 999) — most terminals clamp to the bottom-right
  // corner — then ask for the cursor position with DSR (CSI 6 n). The
  // terminal replies `\x1b[<rows>;<cols>R` which gives us the real
  // viewport size even when ioctl and $COLUMNS/$LINES are stale.
  //
  // Use a short poll() timeout so we don't block forever if the terminal
  // doesn't reply (e.g. piped stdin, some embedded consoles).
  ::write(STDOUT_FILENO, "\x1b[999;999H", 10);
  ::write(STDOUT_FILENO, "\x1b[6n", 4);

  char buf[32] = {};
  int i = 0;
  while (i < 31) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = poll(&pfd, 1, 200); // 200ms total budget
    if (ready <= 0 || !(pfd.revents & POLLIN)) {
      break;
    }
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') {
      i++;
      break;
    }
    i++;
  }

  int rows = 0, cols = 0;
  if (sscanf(buf, "\x1b[%d;%d", &rows, &cols) == 2) {
    if (rows > 0) height = rows;
    if (cols > 0) width = cols;
    ::write(STDOUT_FILENO, "\x1b[H", 3);
    return true;
  }
  ::write(STDOUT_FILENO, "\x1b[H", 3);
  return false;
}


static bool read_char_with_timeout(char &out, int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int ready = poll(&pfd, 1, std::max(0, timeout_ms));
  if (ready <= 0 || !(pfd.revents & POLLIN)) {
    return false;
  }

  return read(STDIN_FILENO, &out, 1) == 1;
}

static bool read_paste_until_end(std::string &out) {
  out.clear();
  while (true) {
    char c = 0;
    if (!read_char_with_timeout(c, 1000)) return false;
      if (c == '\x1b') {
        char a = 0, b = 0, c2 = 0, d = 0;
        if (read_char_with_timeout(a, 20) && a == '[' &&
            read_char_with_timeout(b, 20) && b == '2' &&
            read_char_with_timeout(c2, 20) && c2 == '0' &&
            read_char_with_timeout(d, 20) && d == '1'            
        ) {
          char tilde = 0;
          if (read_char_with_timeout(tilde, 20) && tilde == '~') return true;
          out.push_back('\x1b'); out.push_back(a); out.push_back(b); out.push_back(c2); out.push_back(d); out.push_back(tilde);
        } else {
          out.push_back('\x1b'); out.push_back(a); out.push_back(b); out.push_back(c2); out.push_back(d);
        }
      } else {
        out.push_back(c);
      }
  }
}

static int termkey_modifier_flags(int mod) {
  int flags = 0;
  if (mod & TERMKEY_KEYMOD_SHIFT)
    flags |= 0x80000; // Shift
  if (mod & TERMKEY_KEYMOD_ALT)
    flags |= 0x40000; // Alt
  if (mod & TERMKEY_KEYMOD_CTRL)
    flags |= 0x20000; // Ctrl
  return flags;
}

static int translate_termkey_keysym(TermKeySym sym) {
  switch (sym) {
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

static int translate_termkey_key(const TermKeyKey &key) {
  int flags = termkey_modifier_flags(key.modifiers);

  switch (key.type) {
  case TERMKEY_TYPE_UNICODE: {
    int codepoint = static_cast<int>(key.code.codepoint);
    if (codepoint <= 0) {
      return -1;
    }
    if (codepoint >= 'A' && codepoint <= 'Z') {
      codepoint |= 0x8000;
    }
    return codepoint | flags;
  }
  case TERMKEY_TYPE_KEYSYM: {
    int mapped = translate_termkey_keysym(key.code.sym);
    if (mapped == '\t' && (key.modifiers & TERMKEY_KEYMOD_SHIFT)) {
      mapped = 1017;
      flags &= ~0x80000;
    }
    return mapped ? (mapped | flags) : -1;
  }
  case TERMKEY_TYPE_FUNCTION:
    if (key.code.number > 0) {
      return (1000 + key.code.number) | flags;
    }
    return -1;
  default:
    return -1;
  }
}

static void append_mouse_reset(std::string &buffer) {
  buffer += "\x1b[?1003l"; // any-motion mouse tracking
  buffer += "\x1b[?1002l"; // button-event mouse tracking
  buffer += "\x1b[?1000l"; // basic mouse tracking
  buffer += "\x1b[?1006l"; // SGR mouse protocol
  buffer += "\x1b[?1015l"; // urxvt mouse protocol
  buffer += "\x1b[?1004l"; // focus events
  buffer += "\x1b[?2004l"; // bracketed paste
}

Terminal::Terminal()
    : width(80), height(24), poll_timeout_ms(8), raw_mode(false),
      termkey_(nullptr) {}

Terminal::~Terminal() { cleanup(); }

void Terminal::enable_raw_mode() {
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

void Terminal::disable_raw_mode() {
  if (!raw_mode)
    return;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  raw_mode = false;
}

void Terminal::setup_terminal() {
  write("\x1b[?1049h");
  write("\x1b[?25l");
  write("\x1b[2J");
  write("\x1b[H");
  write("\x1b[?2004h");
}

void Terminal::restore_terminal() {
  append_mouse_reset(buffer);
  write("\x1b[?25h");
  write("\x1b[1 q"); // restore block cursor for the host shell
  write("\x1b[?7h"); // re-enable autowrap so the host shell is left normal
  write("\x1b[?1049l");
  append_mouse_reset(buffer);
  show_cursor();
  reset_color();
  flush();
}

void Terminal::init() {
  setup_terminal();
  flush();

  enable_raw_mode();

  // Render capture: if JOT_RENDER_CAPTURE=/path/to/log is set, open a
  // summary capture file. Frame markers include frame number, bytes, WxH,
  // and rows rendered. Set JOT_RENDER_CAPTURE_RAW=1 to also dump the raw
  // terminal bytes to the capture file.
  {
    const char *cp = getenv("JOT_RENDER_CAPTURE");
    if (cp && cp[0] != '\0') {
      render_capture_ = fopen(cp, "wb");
      if (render_capture_) {
        const char *raw = getenv("JOT_RENDER_CAPTURE_RAW");
        render_capture_raw_ = (raw && raw[0] == '1');
        fprintf(render_capture_, "--JOT-RENDER-CAPTURE-START (raw=%d)--\n",
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
  if (!term || term[0] == '\0') {
    term = "xterm-256color";
  }
  termkey_ = termkey_new_abstract(
      term, TERMKEY_FLAG_UTF8 | TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_CONVERTKP);
  if (!termkey_) {
    termkey_ = termkey_new_abstract(
        "xterm-256color",
        TERMKEY_FLAG_UTF8 | TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_CONVERTKP);
  }
  if (termkey_) {
    termkey_set_waittime(termkey_, 10);
  }

  if (width <= 0) width = 80;
  if (height <= 0) height = 24;

  // Renderer safety margin is fixed at one cell. Larger margins create a
  // visible right-edge gap; zero brings back the rightmost-column wrap bug.
  render_margin_ = 1;

  // Per-row chunked-flush threshold. JOT_RENDER_CHUNK_BYTES=<n> overrides
  // the default of 0 (disabled). Set to e.g. 2048 or 4096 to enable
  // chunked flushing for diagnosis on terminals that drop bytes from
  // large writes. Disabled by default because mid-frame flushes block
  // the event loop while the kernel drains the PTY buffer.
  {
    const char *m = getenv("JOT_RENDER_CHUNK_BYTES");
    if (m && m[0] != '\0') {
      int v = atoi(m);
      if (v >= 0) {
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

bool Terminal::refresh_size(bool force_probe) {
  int new_width = width;
  int new_height = height;

  // First source: ioctl and env vars.
  bool got = get_terminal_size(new_width, new_height);

  // If the caller asked us to force-probe, or the ioctl/env path failed,
  // try the ANSI cursor-position probe. The probe moves the cursor to
  // (999, 999), asks for the current position with DSR (CSI 6 n), reads
  // the terminal's reported rows;cols, then restores the cursor to home.
  // This is the only way to recover when ioctl is returning stale
  // dimensions (e.g. after the alternate screen was entered) or when the
  // controlling TTY / foreground process group changed.
  if ((force_probe || !got) && isatty(STDIN_FILENO) &&
      isatty(STDOUT_FILENO)) {
    int probe_w = new_width;
    int probe_h = new_height;
    if (cursor_probe_size(probe_w, probe_h)) {
      if (probe_w > 0)
        new_width = probe_w;
      if (probe_h > 0)
        new_height = probe_h;
      got = true;
    }
  }

  if (!got) {
    return false;
  }

  if (new_width < 1) new_width = 1;
  if (new_height < 1) new_height = 1;
  bool changed = (new_width != width) || (new_height != height);
  width = new_width;
  height = new_height;
  return changed;
}

void Terminal::cleanup() {
  if (termkey_) {
    termkey_destroy(termkey_);
    termkey_ = nullptr;
  }
  disable_mouse();
  disable_raw_mode();
  restore_terminal();
  if (render_capture_) {
    fprintf(render_capture_, "\n--JOT-RENDER-CAPTURE-END--\n");
    fclose(render_capture_);
    render_capture_ = nullptr;
  }
}

int Terminal::read_termkey_result() {
  if (!termkey_) {
    return -1;
  }

  TermKeyKey key;
  while (true) {
    TermKeyResult result = termkey_getkey(termkey_, &key);
    if (result == TERMKEY_RES_KEY) {
      return translate_termkey_key(key);
    }
    if (result == TERMKEY_RES_AGAIN) {
      char next;
      if (read_char_with_timeout(next, termkey_get_waittime(termkey_))) {
        termkey_push_bytes(termkey_, &next, 1);
        continue;
      }
      result = termkey_getkey_force(termkey_, &key);
      if (result == TERMKEY_RES_KEY) {
        return translate_termkey_key(key);
      }
      return -1;
    }
    return -1;
  }
}

int Terminal::read_key() {
  char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return -1;

  std::string bytes;
  bytes.push_back(c);

  if (c == '\x1b') {
    char second;
    if (read_char_with_timeout(second, 5)) {
      bytes.push_back(second);
      if (second == '\x1b') {
        if (!read_char_with_timeout(second, 5)) {
          if (!termkey_) {
            return -1;
          }
          termkey_push_bytes(termkey_, bytes.data(), bytes.size());
          return read_termkey_result();
        }
        bytes.push_back(second);
      }
      if (second == '[') {
        char third;
        if (read_char_with_timeout(third, 5)) {
          bytes.push_back(third);
          if (third == '<') {
            mouse_event_buffer.clear();
            char mouse_seq[32] = {0};
            int mouse_pos = 0;
            while (mouse_pos < 31) {
              if (!read_char_with_timeout(mouse_seq[mouse_pos], 5))
                return '\x1b';
              if (mouse_seq[mouse_pos] == 'M' || mouse_seq[mouse_pos] == 'm') {
                mouse_seq[mouse_pos + 1] = '\0';
                break;
              }
              mouse_pos++;
            }
            mouse_event_buffer = std::string(mouse_seq);
            return 1014;
          }
          
          if (third == '2') {
            char a = 0, b = 0, tilde = 0;
            if (read_char_with_timeout(a, 5) && a == '0' &&
                read_char_with_timeout(b, 5) && b == '0' &&
                read_char_with_timeout(tilde, 5) && tilde == '~'
            ) {
              if (read_paste_until_end(paste_event_buffer)) return 1020;
            }
          }
          
          while (third < 0x40 || third > 0x7e) {
            if (!read_char_with_timeout(third, 5)) {
              break;
            }
            bytes.push_back(third);
          }
        }
      } else if (second == 'O') {
        char third;
        if (read_char_with_timeout(third, 5)) {
          bytes.push_back(third);
        }
      }
    }
  }

  if (!termkey_) {
    return -1;
  }
  termkey_push_bytes(termkey_, bytes.data(), bytes.size());
  return read_termkey_result();
}

void Terminal::parse_mouse_event(int ch, MouseEvent &event) {
  if (mouse_event_buffer.empty()) {
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
  if (!mouse_event_buffer.empty()) {
    terminator = mouse_event_buffer.back();
  }
  if (terminator != 'M' && terminator != 'm') {
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
  if (sscanf(seq, "%d;%d;%d", &button, &x, &y) == 3) {
    if (x <= 0 || y <= 0) {
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
    event.x = x - 1;
    event.y = y - 1;
    event.ctrl = (button & 16) != 0;
    event.shift = (button & 4) != 0;
    event.alt = (button & 8) != 0;

    bool is_motion = (button & 0x20) != 0;
    bool is_wheel = (button >= 64 && button <= 67);
    bool is_release = (terminator == 'm');

    if (is_wheel) {
      event.pressed = true;
      event.released = false;
    } else if (is_motion) {
      event.pressed = false;
      event.released = false;
    } else if (!is_release) {
      event.pressed = true;
      event.released = false;
    } else {
      event.pressed = false;
      event.released = true;
    }
  } else {
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

Event Terminal::poll_event() {
  Event ev{};
  ev.type = EVENT_REDRAW;

  struct pollfd pfd;
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  pfd.revents = 0;
  poll(&pfd, 1, std::max(0, poll_timeout_ms));

  if (pfd.revents & POLLIN) {
    Event input = read_event();
    if (input.type != EVENT_REDRAW)
      return input;
  }

  Event rsz = check_resize_event();
  if (rsz.type != EVENT_REDRAW)
    return rsz;

  return ev;
}

Event Terminal::check_resize_event() {
  Event ev;
  ev.type = EVENT_REDRAW;

  // Always attempt a live size refresh. SIGWINCH is a fast signal that
  // tells us a resize is likely pending, but it is not required: the cached
  // size can be stale (or stuck at the 80x24 constructor fallback) even
  // when no signal has been delivered. Reset g_resize_pending so a single
  // signal doesn't keep forcing resizes after we've already absorbed it.
  g_resize_pending = 0;
  if (refresh_size()) {
    ev.type = EVENT_RESIZE;
    ev.resize.width = width;
    ev.resize.height = height;
    return ev;
  }
  return ev;
}

Event Terminal::read_event() {
  Event ev{};
  ev.type = EVENT_REDRAW;

  int ch = read_key();
  if (ch < 0) {
    ev.type = EVENT_REDRAW;
    return ev;
  }
  
  if (ch == 1020) {
    ev.type = EVENT_PASTE;
    ev.paste.text = paste_event_buffer.c_str();
    return ev;
  }

  if (ch == 1014) {
    parse_mouse_event(ch, ev.mouse);
    if (const char *p = std::getenv("JOT_MOUSE_DEBUG"); p && *p) {
      FILE *f = std::fopen(p, "a");
      if (f) {
        std::fprintf(f, "[proto] SGR buffer='%s' parsed=(%d,%d) btn=%d "
                          "pressed=%d released=%d\n",
                     mouse_event_buffer.c_str(), ev.mouse.x, ev.mouse.y,
                     ev.mouse.button, ev.mouse.pressed ? 1 : 0,
                     ev.mouse.released ? 1 : 0);
        std::fclose(f);
      }
    }
    if (ev.mouse.x < 0 || ev.mouse.y < 0) {
      ev.type = EVENT_REDRAW;
    } else {
      ev.type = EVENT_MOUSE;
    }
    return ev;
  }

  if (ch == 28) {
    ev = check_resize_event();
    if (ev.type != EVENT_REDRAW)
      return ev;
  }

  ev.type = EVENT_KEY;

  bool mod_shift = (ch & 0x80000) != 0;
  bool mod_alt = (ch & 0x40000) != 0;
  bool mod_ctrl = (ch & 0x20000) != 0;

  int base_key = ch & 0xFFFF;

  ev.key.key = base_key;
  ev.key.ctrl = mod_ctrl || (base_key >= 1 && base_key <= 26 &&
                             base_key != 13 && base_key != 9);
  ev.key.shift = mod_shift || (base_key >= 2008 && base_key <= 2011) ||
                 (base_key & 0x8000);
  ev.key.alt = mod_alt;

  if (ev.key.shift && (base_key >= 2008 && base_key <= 2011)) {
    ev.key.key = base_key - 1000;
  } else if (ev.key.shift && (base_key & 0x8000)) {
    ev.key.key = base_key & 0x7FFF;
  }

  return ev;
}

void Terminal::set_poll_timeout_ms(int timeout_ms) {
  poll_timeout_ms = std::clamp(timeout_ms, 1, 250);
}

void Terminal::flush() {
  int n = (int)buffer.length();
  last_flush_bytes_ = n;
  if (n <= 0) {
    return;
  }

  // Write the raw capture file first. fwrite is stdio-buffered and
  // does not touch the PTY, so it cannot interleave with the
  // terminal output and keeps the capture in sync with what we are
  // about to emit.
  if (render_capture_ && render_capture_raw_) {
    fwrite(buffer.c_str(), 1, n, render_capture_);
  }

  // Single ordered flush path: a blocking write() loop on
  // STDOUT_FILENO. We previously used fwrite()/fflush(stdout) here
  // and a non-blocking write() in try_drain() mid-frame. Mixing
  // stdio with low-level writes caused byte reordering on some
  // terminals (notably COSMIC at fullscreen sizes) and the
  // observable symptom was the cursor teleporting away from the
  // click position while typing. Using one low-level write() loop
  // for the whole frame keeps the output strictly ordered and
  // EINTR-safe.
  const char *p = buffer.c_str();
  size_t remaining = (size_t)n;
  while (remaining > 0) {
    ssize_t w = ::write(STDOUT_FILENO, p, remaining);
    if (w > 0) {
      p += w;
      remaining -= (size_t)w;
    } else if (w == -1 && errno == EINTR) {
      // Interrupted by a signal; retry the same bytes.
      continue;
    } else if (w == -1 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // stdout was switched to O_NONBLOCK somewhere; fall back to
      // a short blocking wait by poll()-ing for writability.
      struct pollfd pfd;
      pfd.fd = STDOUT_FILENO;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      if (::poll(&pfd, 1, 1000) <= 0) {
        // Give up after 1s; drop the rest of the frame to avoid a
        // permanent hang.
        break;
      }
      continue;
    } else {
      // EPIPE, EBADF, etc. Drop the rest of the frame.
      break;
    }
  }

  buffer.clear();
}

void Terminal::flush_if_buffer_exceeds() {
  if (render_chunk_bytes_ == 0) {
    return;
  }
  if (buffer.size() >= render_chunk_bytes_) {
    flush();
  }
}

void Terminal::try_drain() {
  if (render_chunk_bytes_ == 0) {
    return;
  }
  if (buffer.size() < render_chunk_bytes_) {
    return;
  }
  if (buffer.empty()) {
    return;
  }

  // Write the capture file first (fwrite is buffered and fast; does
  // not block on the PTY). This keeps the diagnostic capture in
  // sync with what we actually attempt to write to the terminal.
  if (render_capture_ && render_capture_raw_) {
    fwrite(buffer.c_str(), 1, buffer.size(), render_capture_);
  }

  // Set O_NONBLOCK on stdout so write() returns immediately with
  // a short count (or EAGAIN) if the kernel PTY buffer is full,
  // instead of blocking while the terminal drains.
  int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
  if (flags == -1) {
    return;
  }
  if (fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) {
    return;
  }

  size_t total = buffer.size();
  size_t written = 0;
  while (written < total) {
    ssize_t n = ::write(STDOUT_FILENO, buffer.c_str() + written, total - written);
    if (n > 0) {
      written += (size_t)n;
    } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // PTY buffer is full; terminal hasn't drained yet. Keep the
      // remaining data in the buffer for the next try_drain() or
      // the final flush() at frame end.
      break;
    } else {
      // Real error (EPIPE, EBADF, etc). Bail and let the final
      // flush() decide what to do.
      break;
    }
  }

  // Restore the original non-blocking flag state.
  fcntl(STDOUT_FILENO, F_SETFL, flags);

  if (written > 0) {
    if (written == total) {
      buffer.clear();
    } else {
      buffer.erase(0, written);
    }
  }
}

void Terminal::clear() {
  buffer += "\x1b[2J";
  buffer += "\x1b[H";
}

void Terminal::move_cursor(int x, int y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
  buffer += buf;
}

void Terminal::hide_cursor() { buffer += "\x1b[?25l"; }

void Terminal::show_cursor() { buffer += "\x1b[?25h"; }

void Terminal::set_color(int fg, int bg) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[38;5;%dm\x1b[48;5;%dm", fg, bg);
  buffer += buf;
}

void Terminal::reset_color() { buffer += "\x1b[0m"; }

void Terminal::set_bold(bool on) {
  if (on) {
    buffer += "\x1b[1m";
  } else {
    buffer += "\x1b[22m";
  }
}

void Terminal::set_italic(bool on) {
  if (on) {
    buffer += "\x1b[3m";
  } else {
    buffer += "\x1b[23m";
  }
}

void Terminal::set_reverse(bool on) {
  if (on) {
    buffer += "\x1b[7m";
  } else {
    buffer += "\x1b[27m";
  }
}

void Terminal::write(const std::string &str) { buffer += str; }

void Terminal::write_char(char c) { buffer += c; }

void Terminal::enable_mouse() {
  buffer += "\x1b[?1002h";
  buffer += "\x1b[?1006h";
  buffer += "\x1b[?2004h";
  flush();
}

void Terminal::disable_mouse() {
  append_mouse_reset(buffer);
  flush();
}

void Terminal::enable_mouse_hover() {
  buffer += "\x1b[?1003h";
  flush();
}

void Terminal::disable_mouse_hover() {
  buffer += "\x1b[?1003l";
  buffer += "\x1b[?1002h";
  buffer += "\x1b[?1006h";
  flush();
}

void Terminal::save_cursor() { buffer += "\x1b[s"; }

void Terminal::restore_cursor() { buffer += "\x1b[u"; }

void Terminal::clear_line() { buffer += "\x1b[2K"; }

void Terminal::clear_to_end() { buffer += "\x1b[K"; }

void Terminal::disable_autowrap() { buffer += "\x1b[?7l"; }

void Terminal::enable_autowrap() { buffer += "\x1b[?7h"; }

void Terminal::render_capture_marker(const std::string &label,
                                     int rows_rendered) {
  if (!render_capture_)
    return;
  render_capture_seq_++;
  int bytes = last_flush_bytes_;
  fprintf(render_capture_,
          "\n--MARKER %d: %s bytes=%d w=%d h=%d rows=%d--\n",
          render_capture_seq_, label.c_str(),
          bytes, width, height, rows_rendered);
  fflush(render_capture_);
}
