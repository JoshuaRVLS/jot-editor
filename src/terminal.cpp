#include "terminal.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;

Terminal::Terminal() : width(80), height(24), raw_mode(false) {}

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
  raw.c_cc[VMIN] = 1;
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
  write("\x1b[?1049l");
  show_cursor();
  reset_color();
  flush();
}

void Terminal::init() {
  enable_raw_mode();
  setup_terminal();

  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
    width = ws.ws_col;
    height = ws.ws_row;
  }

  enable_mouse();
}

void Terminal::cleanup() {
  disable_mouse();
  disable_raw_mode();
  restore_terminal();
}

int Terminal::read_key() {
  char c;
  if (read(STDIN_FILENO, &c, 1) != 1)
    return 0;

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        char next;
        if (read(STDIN_FILENO, &next, 1) != 1)
          return '\x1b';
        if (next == ';') {
          char mod;
          if (read(STDIN_FILENO, &mod, 1) != 1)
            return '\x1b';
          char final;
          if (read(STDIN_FILENO, &final, 1) != 1)
            return '\x1b';

          int key_code = 0;
          switch (final) {
          case 'A':
            key_code = 1008;
            break; // Up
          case 'B':
            key_code = 1009;
            break; // Down
          case 'C':
            key_code = 1010;
            break; // Right
          case 'D':
            key_code = 1011;
            break; // Left
          case 'F':
            key_code = 1013;
            break; // End
          case 'H':
            key_code = 1012;
            break; // Home
          }

          if (key_code != 0) {
            int flags = 0;
            // 2=Shift, 3=Alt, 5=Ctrl, 4=Shift+Alt, 6=Shift+Ctrl, 7=Alt+Ctrl,
            // 8=Shift+Alt+Ctrl
            if (mod == '2')
              flags |= 0x80000; // Shift
            else if (mod == '3')
              flags |= 0x40000; // Alt
            else if (mod == '4')
              flags |= 0x80000 | 0x40000; // Shift+Alt
            else if (mod == '5')
              flags |= 0x20000; // Ctrl
            else if (mod == '6')
              flags |= 0x80000 | 0x20000; // Shift+Ctrl
            else if (mod == '7')
              flags |= 0x40000 | 0x20000; // Alt+Ctrl
            else if (mod == '8')
              flags |= 0x80000 | 0x40000 | 0x20000; // Shift+Alt+Ctrl

            return key_code | flags;
          }
          return '\x1b';
        } else if (next == '~') {
          switch (seq[1]) {
          case '1':
            return 1000;
          case '3':
            return 1001;
          case '4':
            return 1002;
          case '5':
            return 1003;
          case '6':
            return 1004;
          case '7':
            return 1005;
          case '8':
            return 1006;
          case '9':
            return 1007;
          }
        }
      } else if (seq[1] == '<') {
        mouse_event_buffer.clear();
        char mouse_seq[32] = {0};
        int mouse_pos = 0;
        while (mouse_pos < 31) {
          if (read(STDIN_FILENO, &mouse_seq[mouse_pos], 1) != 1)
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
          return 1014;
        case '5':
          return 1015;
        case '6':
          return 1016;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return 1012;
      case 'F':
        return 1013;
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

    int button_code = button & 0x03;
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

  // Check for terminal resize first (even when no input)
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
    if (ws.ws_col != width || ws.ws_row != height) {
      width = ws.ws_col;
      height = ws.ws_row;
      ev.type = EVENT_RESIZE;
      ev.resize.width = width;
      ev.resize.height = height;
      return ev;
    }
  }

  int ch = read_key();
  if (ch == 0) {
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
    // Also check on explicit resize character
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      if (ws.ws_col != width || ws.ws_row != height) {
        width = ws.ws_col;
        height = ws.ws_row;
        ev.type = EVENT_RESIZE;
        ev.resize.width = width;
        ev.resize.height = height;
        return ev;
      }
    }
  }

  ev.type = EVENT_KEY;

  // Decoding bits
  bool mod_shift = (ch & 0x80000) != 0;
  bool mod_alt = (ch & 0x40000) != 0;
  bool mod_ctrl = (ch & 0x20000) != 0;

  int base_key = ch & 0xFFFF; // Mask out high bits

  // Legacy Shift handling (0x8000 for chars, or 2008..2011 ranges)
  // Assuming 2008..2011 are deprecated by the new logic for arrows,
  // but let's keep them if encoded differently elsewhere?
  // My new logic returns 1008 | 0x80000 for Shift+Up.
  // Old logic returned 2008.
  // We should unify.

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

void Terminal::flush() {
  fwrite(buffer.c_str(), 1, buffer.length(), stdout);
  fflush(stdout);
  buffer.clear();
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

void Terminal::clear_to_end() { buffer += "\x1b[0J"; }
