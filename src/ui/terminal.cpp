#include "terminal.h"
#include <csignal>
#include <cstring>
#include <fcntl.h>

#include <algorithm>
#include <iostream>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
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

Terminal::Terminal() : width(80), height(24), poll_timeout_ms(8), raw_mode(false) {}

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
}

void Terminal::restore_terminal() {
  write("\x1b[?25h");
  write("\x1b[?7h"); // re-enable autowrap so the host shell is left normal
  write("\x1b[?1049l");
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

  if (width <= 0) width = 80;
  if (height <= 0) height = 24;

  // Renderer safety margin. JOT_RENDER_MARGIN=<n> overrides the default
  // of 1 column. Margin 0 is rejected — the original rightmost-column
  // bug returns at large widths without at least 1 column of safety.
  {
    const char *m = getenv("JOT_RENDER_MARGIN");
    if (m && m[0] != '\0') {
      int v = atoi(m);
      if (v >= 1) {
        render_margin_ = v;
      }
    }
  }

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
  disable_mouse();
  disable_raw_mode();
  restore_terminal();
  if (render_capture_) {
    fprintf(render_capture_, "\n--JOT-RENDER-CAPTURE-END--\n");
    fclose(render_capture_);
    render_capture_ = nullptr;
  }
}

// ... inside read_key
int Terminal::read_key() {
  char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return -1;

  // Debug logging
  // std::ofstream log("key_debug.log", std::ios::app);
  // if (log.is_open())
  //   log << "Read char: " << (int)c << " (" << (c >= 32 ? c : '.') << ")"
  //       << std::endl;

  if (c == '\x1b') {
    char seq[3];
    if (!read_char_with_timeout(seq[0], 5))
      return '\x1b';

    // Treat ESC + regular character as Alt+key. This is the most common way
    // terminals encode Alt-modified letters and is much safer than trying to
    // force Ctrl+Alt+Arrow support through terminal escape parsing.
    if (seq[0] != '[' && seq[0] != 'O') {
      return ((unsigned char)seq[0]) | 0x40000;
    }

    if (!read_char_with_timeout(seq[1], 5))
      return '\x1b';

    // if (log.is_open())
    //   log << "Seq header: " << seq[0] << seq[1] << std::endl;

    if (seq[0] == '[') {
      // Parse params: [param1;param2;...terminator
      std::vector<int> params;
      std::string current_param;
      if (seq[1] >= '0' && seq[1] <= '9') {
        current_param += seq[1];
        char next;
        while (read_char_with_timeout(next, 5)) {
          // log << "Param char: " << (int)next << std::endl;
          if (next >= '0' && next <= '9') {
            current_param += next;
          } else if (next == ';') {
            if (!current_param.empty())
              params.push_back(std::stoi(current_param));
            current_param.clear();
          } else {
            // Terminator found
            if (!current_param.empty())
              params.push_back(std::stoi(current_param));

            // Logic based on terminator
            if (next == '~') {
              if (params.size() >= 1) {
                int key = params[0];
                int mod = (params.size() >= 2) ? params[1] : 1;

                // if (log.is_open())
                //   log << "Sequence parsed: key=" << key << " mod=" << mod
                //       << std::endl;

                // Handle modifyOtherKeys: 27;mod;key~
                if (key == 27 && params.size() >= 3) {
                  mod = params[1];
                  key = params[2];
                }

                int flags = 0;
                if (mod == 2)
                  flags |= 0x80000; // Shift
                else if (mod == 3)
                  flags |= 0x40000; // Alt
                else if (mod == 4)
                  flags |= 0x80000 | 0x40000;
                else if (mod == 5)
                  flags |= 0x20000; // Ctrl
                else if (mod == 6)
                  flags |= 0x80000 | 0x20000; // Ctrl+Shift
                else if (mod == 7)
                  flags |= 0x40000 | 0x20000;
                else if (mod == 8)
                  flags |= 0x80000 | 0x40000 | 0x20000;

                // Map special keys
                int mapped_key = 0;
                switch (key) {
                case 3:
                  mapped_key = 1001;
                  break; // Delete (3~), possibly with modifiers
                case 15:
                  mapped_key = 1005;
                  break; // F5
                case 17:
                  mapped_key = 1006;
                  break; // F6
                case 18:
                  mapped_key = 1007;
                  break; // F7
                case 19:
                  mapped_key = 1008;
                  break; // F8? No, F8 is 19 usually
                         // Add arrow mappings if they come as numbers
                         // But usually they are 1;modA or similar?
                  // Standard 1;5A is parsed differently (see below logic if we
                  // preserve it) Actually, generic keys: 13 -> Enter
                }

                if (mapped_key)
                  return mapped_key | flags;
                return key | flags;
              }
            } else if (next == 'u') { // Kitty protocol: key;modu
              if (params.size() >= 2) {
                int key = params[0];
                int mod = params[1];
                int flags = 0;
                // Kitty mods: 1=None, 2=Shift, 3=Alt, 4=Ctrl+Shift?
                // Wait, Kitty uses 1-based bitmask?
                // 1=Shift, 2=Alt, 4=Ctrl?
                // Actually usually standard masks:
                // 1: Shift=1, Alt=2, Ctrl=4, Super=8 -> +1 offset?
                // Standard xterm: 2=Shift, 3=Alt, 5=Ctrl

                // Assuming standard XTerm modifiers for now as 'u' often
                // follows that
                if (mod == 2)
                  flags |= 0x80000;
                else if (mod == 3)
                  flags |= 0x40000;
                else if (mod == 4)
                  flags |= 0x80000 | 0x40000;
                else if (mod == 5)
                  flags |= 0x20000;
                else if (mod == 6)
                  flags |= 0x80000 | 0x20000;
                else if (mod == 7)
                  flags |= 0x40000 | 0x20000;
                else if (mod == 8)
                  flags |= 0x80000 | 0x40000 | 0x20000;

                return key | flags;
              }
            } else if (next == 'A' || next == 'B' || next == 'C' ||
                       next == 'D' || next == 'F' || next == 'H') {
              // 1;5A style
              int mod = params.empty() ? 1 : params[0];
              // If format is [1;5A, params[0] is 1, params[1] is 5
              if (params.size() == 2 && params[0] == 1)
                mod = params[1];

              int flags = 0;
              if (mod == 2)
                flags |= 0x80000;
              else if (mod == 3)
                flags |= 0x40000;
              else if (mod == 4)
                flags |= 0x80000 | 0x40000;
              else if (mod == 5)
                flags |= 0x20000;
              else if (mod == 6)
                flags |= 0x80000 | 0x20000;
              else if (mod == 7)
                flags |= 0x40000 | 0x20000;
              else if (mod == 8)
                flags |= 0x80000 | 0x40000 | 0x20000;
              // ...

              int code = 0;
              if (next == 'A')
                code = 1008;
              if (next == 'B')
                code = 1009;
              if (next == 'C')
                code = 1010;
              if (next == 'D')
                code = 1011;
              if (next == 'F')
                code = 1013;
              if (next == 'H')
                code = 1012;

              return code | flags;
            }

            return '\x1b';
          }
        }
      } else if (seq[1] == '<') {
        // Mouse
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
      } else {
        // Handle [A, [B directly (no numbers)
        switch (seq[1]) {
        case 'A':
          return 1008;
        case 'B':
          return 1009;
        case 'C':
          return 1010;
        case 'D':
          return 1011;
        case 'H':
          return 1012;
        case 'F':
          return 1013;
        case 'M':
          return 1014; // Mouse X10
        case 'Z':
          return 1017; // Shift+Tab
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return 1012;
      case 'F':
        return 1013;
      case 'M':
        return 13; // Keypad Enter on some terminals
      }
    }
    return '\x1b';
  }

  if (c >= 'A' && c <= 'Z') {
    return c | 0x8000;
  }

  return c;
}

void Terminal::parse_mouse_event(int ch, MouseEvent &event) {
  if (mouse_event_buffer.empty()) {
    event.x = 0;
    event.y = 0;
    event.pressed = false;
    event.released = false;
    return;
  }

  const char *seq = mouse_event_buffer.c_str();

  int button, x, y;
  if (sscanf(seq, "%d;%d;%d", &button, &x, &y) == 3) {
    event.button = button;
    event.x = x - 1;
    event.y = y - 1;

    // int button_code = button & 0x03;
    bool is_motion = (button & 0x20) != 0;
    bool is_wheel = (button >= 64 && button <= 67);

    char terminator = mouse_event_buffer.back();
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
    event.x = 0;
    event.y = 0;
    event.pressed = false;
    event.released = false;
  }
}

Event Terminal::poll_event() {
  Event ev;
  ev.type = EVENT_REDRAW;

  struct pollfd pfd;
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  pfd.revents = 0;
  poll(&pfd, 1, std::max(0, poll_timeout_ms));

  ev = check_resize_event();
  if (ev.type != EVENT_REDRAW)
    return ev;

  if (pfd.revents & POLLIN)
    return read_event();

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
  Event ev;

  int ch = read_key();
  if (ch < 0) {
    ev.type = EVENT_REDRAW;
    return ev;
  }

  if (ch == 1014) {
    ev.type = EVENT_MOUSE;
    parse_mouse_event(ch, ev.mouse);
    if (ev.mouse.x < 0 || ev.mouse.y < 0) {
      ev.type = EVENT_REDRAW;
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
  if (render_capture_ && render_capture_raw_ && n > 0) {
    fwrite(buffer.c_str(), 1, n, render_capture_);
  }
  fwrite(buffer.c_str(), 1, n, stdout);
  fflush(stdout);
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
  buffer += "\x1b[?1000h";
  buffer += "\x1b[?1002h";
  buffer += "\x1b[?1015h";
  buffer += "\x1b[?1006h";
  flush();
}

void Terminal::disable_mouse() {
  buffer += "\x1b[?1006l";
  buffer += "\x1b[?1015l";
  buffer += "\x1b[?1002l";
  buffer += "\x1b[?1000l";
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
