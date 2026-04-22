#include "terminal.h"

#include <algorithm>
#include <chrono>
#include <conio.h>
#include <iostream>
#include <thread>
#include <windows.h>

namespace {
HANDLE stdin_handle() { return GetStdHandle(STD_INPUT_HANDLE); }
HANDLE stdout_handle() { return GetStdHandle(STD_OUTPUT_HANDLE); }

DWORD g_original_input_mode = 0;
DWORD g_original_output_mode = 0;
bool g_input_mode_saved = false;
bool g_output_mode_saved = false;

int map_extended_key(int code) {
  switch (code) {
  case 72:
    return 1008; // Up
  case 80:
    return 1009; // Down
  case 77:
    return 1010; // Right
  case 75:
    return 1011; // Left
  case 71:
    return 1012; // Home
  case 79:
    return 1013; // End
  case 73:
    return 1006; // PageUp
  case 81:
    return 1007; // PageDown
  case 82:
    return 1000; // Insert
  case 83:
    return 1001; // Delete

  // Function keys.
  case 59:
    return 2001; // F1
  case 60:
    return 2002; // F2
  case 61:
    return 2003; // F3
  case 62:
    return 2004; // F4
  case 63:
    return 2005; // F5
  case 64:
    return 2006; // F6
  case 65:
    return 2007; // F7
  case 66:
    return 2008; // F8
  case 67:
    return 2009; // F9
  case 68:
    return 2010; // F10
  case 133:
    return 2011; // F11
  case 134:
    return 2012; // F12

  // Ctrl + arrows/home/end/delete mappings (common conhost codes).
  case 115:
    return 1011; // Ctrl+Left
  case 116:
    return 1010; // Ctrl+Right
  case 141:
    return 1008; // Ctrl+Up
  case 145:
    return 1009; // Ctrl+Down
  case 119:
    return 1013; // Ctrl+End
  case 117:
    return 1012; // Ctrl+Home
  case 147:
    return 1001; // Ctrl+Delete

  // Alt + arrows/home/end mappings.
  case 152:
    return 1008; // Alt+Up
  case 160:
    return 1009; // Alt+Down
  case 157:
    return 1010; // Alt+Right
  case 155:
    return 1011; // Alt+Left
  case 151:
    return 1012; // Alt+Home
  case 159:
    return 1013; // Alt+End
  default:
    return -1;
  }
}

void refresh_console_size(int &w, int &h) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(stdout_handle(), &csbi)) {
    w = std::max(1, csbi.srWindow.Right - csbi.srWindow.Left + 1);
    h = std::max(1, csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
  }
}

bool modifier_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool configure_console_modes() {
  HANDLE in = stdin_handle();
  HANDLE out = stdout_handle();
  if (in == INVALID_HANDLE_VALUE || out == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD in_mode = 0;
  if (GetConsoleMode(in, &in_mode)) {
    if (!g_input_mode_saved) {
      g_original_input_mode = in_mode;
      g_input_mode_saved = true;
    }
    // Keep processed input on so Ctrl+C still works in editor process.
    in_mode |= ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS;
    in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    SetConsoleMode(in, in_mode);
  }

  DWORD out_mode = 0;
  if (GetConsoleMode(out, &out_mode)) {
    if (!g_output_mode_saved) {
      g_original_output_mode = out_mode;
      g_output_mode_saved = true;
    }
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(out, out_mode);
  }

  // UTF-8 output avoids garbled glyphs for Nerd Font and symbols.
  SetConsoleOutputCP(CP_UTF8);
  return true;
}

void restore_console_modes() {
  HANDLE in = stdin_handle();
  HANDLE out = stdout_handle();
  if (g_input_mode_saved && in != INVALID_HANDLE_VALUE) {
    SetConsoleMode(in, g_original_input_mode);
  }
  if (g_output_mode_saved && out != INVALID_HANDLE_VALUE) {
    SetConsoleMode(out, g_original_output_mode);
  }
}
} // namespace

Terminal::Terminal() : width(80), height(24), poll_timeout_ms(8), raw_mode(false) {}

Terminal::~Terminal() { cleanup(); }

void Terminal::enable_raw_mode() { raw_mode = true; }

void Terminal::disable_raw_mode() {
  if (!raw_mode)
    return;
  restore_console_modes();
  raw_mode = false;
}

void Terminal::setup_terminal() {
  write("\x1b[?1049h");
  write("\x1b[?25l");
  write("\x1b[2J\x1b[H");
}

void Terminal::restore_terminal() {
  write("\x1b[?25h");
  write("\x1b[?1049l");
  show_cursor();
  reset_color();
  flush();
}

void Terminal::init() {
  if (raw_mode) {
    return;
  }
  enable_raw_mode();
  configure_console_modes();

  refresh_console_size(width, height);
  setup_terminal();
}

void Terminal::cleanup() {
  disable_mouse();
  disable_raw_mode();
  restore_terminal();
}

int Terminal::read_key() {
  if (!_kbhit()) {
    return -1;
  }

  int ch = _getch();
  // Alt+<key> emits 0x00/0xE0 prefix followed by key code in many terminals.
  if (ch == 0 || ch == 224) {
    int ext = _getch();
    return map_extended_key(ext);
  }
  if (ch == '\t') {
    return 9;
  }
  if (ch == 13) {
    return '\n';
  }
  return ch;
}

void Terminal::parse_mouse_event(int, MouseEvent &event) {
  event = {0, 0, 0, false, false};
}

Event Terminal::poll_event() {
  Event ev{};

  int prev_w = width;
  int prev_h = height;
  refresh_console_size(width, height);
  if (width != prev_w || height != prev_h) {
    ev.type = EVENT_RESIZE;
    ev.resize.width = width;
    ev.resize.height = height;
    return ev;
  }

  int waited_ms = 0;
  const int sleep_step_ms = 1;
  int key = -1;
  while (waited_ms <= std::max(0, poll_timeout_ms)) {
    key = read_key();
    if (key >= 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_step_ms));
    waited_ms += sleep_step_ms;
  }

  if (key >= 0) {
    ev.type = EVENT_KEY;
    ev.key.key = key;
    ev.key.ctrl = modifier_down(VK_CONTROL);
    ev.key.shift = modifier_down(VK_SHIFT);
    ev.key.alt = modifier_down(VK_MENU);
    return ev;
  }

  ev.type = EVENT_REDRAW;
  return ev;
}

void Terminal::set_poll_timeout_ms(int timeout_ms) {
  poll_timeout_ms = std::max(0, timeout_ms);
}

void Terminal::flush() { std::cout << std::flush; }

void Terminal::clear() { write("\x1b[2J\x1b[H"); }

void Terminal::move_cursor(int x, int y) {
  write("\x1b[" + std::to_string(std::max(1, y + 1)) + ";" +
        std::to_string(std::max(1, x + 1)) + "H");
}

void Terminal::hide_cursor() { write("\x1b[?25l"); }

void Terminal::show_cursor() { write("\x1b[?25h"); }

void Terminal::set_color(int fg, int bg) {
  int safe_fg = std::clamp(fg, 0, 255);
  int safe_bg = std::clamp(bg, 0, 255);
  write("\x1b[38;5;" + std::to_string(safe_fg) + "m");
  write("\x1b[48;5;" + std::to_string(safe_bg) + "m");
}

void Terminal::reset_color() { write("\x1b[0m"); }

void Terminal::set_bold(bool on) { write(on ? "\x1b[1m" : "\x1b[22m"); }

void Terminal::set_italic(bool on) { write(on ? "\x1b[3m" : "\x1b[23m"); }

void Terminal::set_reverse(bool on) { write(on ? "\x1b[7m" : "\x1b[27m"); }

void Terminal::write(const std::string &str) { std::cout << str; }

void Terminal::write_char(char c) { std::cout << c; }

void Terminal::enable_mouse() {}

void Terminal::disable_mouse() {}

void Terminal::save_cursor() { write("\x1b[s"); }

void Terminal::restore_cursor() { write("\x1b[u"); }

void Terminal::clear_line() { write("\x1b[2K"); }

void Terminal::clear_to_end() { write("\x1b[K"); }
