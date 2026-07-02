#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "terminal.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
HANDLE input_handle() { return GetStdHandle(STD_INPUT_HANDLE); }
HANDLE output_handle() { return GetStdHandle(STD_OUTPUT_HANDLE); }

DWORD g_original_input_mode = 0;
DWORD g_original_output_mode = 0;
bool g_have_input_mode = false;
bool g_have_output_mode = false;
DWORD g_last_button_state = 0;

bool read_console_input(INPUT_RECORD &record) {
  DWORD count = 0;
  return ReadConsoleInputW(input_handle(), &record, 1, &count) && count == 1;
}

bool peek_has_console_input() {
  DWORD count = 0;
  if (!GetNumberOfConsoleInputEvents(input_handle(), &count)) {
    return false;
  }
  return count > 0;
}

int ctrl_char_for_letter(wchar_t ch) {
  if (ch >= L'a' && ch <= L'z') {
    return (int)(ch - L'a' + 1);
  }
  if (ch >= L'A' && ch <= L'Z') {
    return (int)(ch - L'A' + 1);
  }
  return 0;
}

int translate_key_event(const KEY_EVENT_RECORD &key, bool &ctrl, bool &shift,
                        bool &alt) {
  const DWORD state = key.dwControlKeyState;
  ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
  shift = (state & SHIFT_PRESSED) != 0;
  alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

  switch (key.wVirtualKeyCode) {
  case VK_BACK:
    return 127;
  case VK_TAB:
    return shift ? 1017 : '\t';
  case VK_RETURN:
    return 13;
  case VK_ESCAPE:
    return 27;
  case VK_DELETE:
    return 1001;
  case VK_UP:
    return 1008;
  case VK_DOWN:
    return 1009;
  case VK_RIGHT:
    return 1010;
  case VK_LEFT:
    return 1011;
  case VK_HOME:
    return 1012;
  case VK_END:
    return 1013;
  case VK_PRIOR:
    return 1015;
  case VK_NEXT:
    return 1016;
  default:
    break;
  }

  wchar_t ch = key.uChar.UnicodeChar;
  if (ch == 0) {
    return -1;
  }
  if (ctrl) {
    int control = ctrl_char_for_letter(ch);
    if (control) {
      return control;
    }
  }
  if (ch >= L'A' && ch <= L'Z') {
    shift = true;
  }
  return (int)ch;
}

void reset_mouse_event(MouseEvent &event) {
  event.x = -1;
  event.y = -1;
  event.button = 0;
  event.pressed = false;
  event.released = false;
  event.ctrl = false;
  event.shift = false;
  event.alt = false;
}

bool translate_mouse_event(const MOUSE_EVENT_RECORD &mouse, MouseEvent &event) {
  reset_mouse_event(event);
  event.x = mouse.dwMousePosition.X;
  event.y = mouse.dwMousePosition.Y;

  const DWORD state = mouse.dwControlKeyState;
  event.ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
  event.shift = (state & SHIFT_PRESSED) != 0;
  event.alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

  if (mouse.dwEventFlags == MOUSE_WHEELED) {
    const short delta = HIWORD(mouse.dwButtonState);
    event.button = delta > 0 ? 64 : 65;
    event.pressed = true;
    return true;
  }

  DWORD changed = mouse.dwButtonState ^ g_last_button_state;
  if (mouse.dwEventFlags == MOUSE_MOVED) {
    if (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
      event.button = 0x20;
      return true;
    }
    return false;
  }

  if (changed & FROM_LEFT_1ST_BUTTON_PRESSED) {
    event.button = 0;
    event.pressed = (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    event.released = !event.pressed;
    g_last_button_state = mouse.dwButtonState;
    return true;
  }
  if (changed & RIGHTMOST_BUTTON_PRESSED) {
    event.button = 2;
    event.pressed = (mouse.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;
    event.released = !event.pressed;
    g_last_button_state = mouse.dwButtonState;
    return true;
  }

  g_last_button_state = mouse.dwButtonState;
  return false;
}

void append_mouse_reset(std::string &buffer) {
  buffer += "\x1b[?1003l";
  buffer += "\x1b[?1002l";
  buffer += "\x1b[?1000l";
  buffer += "\x1b[?1006l";
  buffer += "\x1b[?1015l";
  buffer += "\x1b[?1004l";
  buffer += "\x1b[?2004l";
}
} // namespace

Terminal::Terminal()
    : width(80), height(24), poll_timeout_ms(8), raw_mode(false),
      termkey_(nullptr) {}

Terminal::~Terminal() { cleanup(); }

void Terminal::enable_raw_mode() {
  if (raw_mode) {
    return;
  }

  HANDLE in = input_handle();
  HANDLE out = output_handle();
  DWORD input_mode = 0;
  DWORD output_mode = 0;
  if (GetConsoleMode(in, &input_mode)) {
    g_original_input_mode = input_mode;
    g_have_input_mode = true;
    input_mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
#ifdef ENABLE_VIRTUAL_TERMINAL_INPUT
    input_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
#endif
    input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                    ENABLE_PROCESSED_INPUT);
#ifdef ENABLE_QUICK_EDIT_MODE
    input_mode &= ~ENABLE_QUICK_EDIT_MODE;
#endif
    SetConsoleMode(in, input_mode);
  }
  if (GetConsoleMode(out, &output_mode)) {
    g_original_output_mode = output_mode;
    g_have_output_mode = true;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
#ifdef DISABLE_NEWLINE_AUTO_RETURN
    output_mode |= DISABLE_NEWLINE_AUTO_RETURN;
#endif
    SetConsoleMode(out, output_mode);
  }
  raw_mode = true;
}

void Terminal::disable_raw_mode() {
  if (!raw_mode) {
    return;
  }
  if (g_have_input_mode) {
    SetConsoleMode(input_handle(), g_original_input_mode);
  }
  if (g_have_output_mode) {
    SetConsoleMode(output_handle(), g_original_output_mode);
  }
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
  write("\x1b[1 q");
  write("\x1b[?7h");
  write("\x1b[?1049l");
  reset_color();
  flush();
}

void Terminal::init() {
  enable_raw_mode();
  setup_terminal();
  flush();

  const char *cp = getenv("JOT_RENDER_CAPTURE");
  if (cp && cp[0] != '\0') {
    render_capture_ = fopen(cp, "wb");
    if (render_capture_) {
      const char *raw = getenv("JOT_RENDER_CAPTURE_RAW");
      render_capture_raw_ = raw && raw[0] == '1';
      fprintf(render_capture_, "--JOT-RENDER-CAPTURE-START (raw=%d)--\n",
              render_capture_raw_ ? 1 : 0);
      fflush(render_capture_);
    }
  }

  refresh_size(true);
  if (width <= 0) width = 80;
  if (height <= 0) height = 24;
  render_margin_ = 1;

  const char *chunk = getenv("JOT_RENDER_CHUNK_BYTES");
  if (chunk && chunk[0] != '\0') {
    int value = atoi(chunk);
    if (value >= 0) {
      render_chunk_bytes_ = (size_t)value;
    }
  }

  enable_mouse();
}

bool Terminal::refresh_size(bool) {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  int new_width = width;
  int new_height = height;
  bool got = false;
  if (GetConsoleScreenBufferInfo(output_handle(), &info)) {
    new_width = std::max(1, (int)(info.srWindow.Right - info.srWindow.Left + 1));
    new_height = std::max(1, (int)(info.srWindow.Bottom - info.srWindow.Top + 1));
    got = true;
  }
  const char *env_cols = getenv("COLUMNS");
  const char *env_lines = getenv("LINES");
  if (env_cols) {
    new_width = std::max(1, atoi(env_cols));
    got = true;
  }
  if (env_lines) {
    new_height = std::max(1, atoi(env_lines));
    got = true;
  }
  if (!got) {
    return false;
  }
  bool changed = new_width != width || new_height != height;
  width = new_width;
  height = new_height;
  return changed;
}

void Terminal::cleanup() {
  disable_mouse();
  restore_terminal();
  disable_raw_mode();
  if (render_capture_) {
    fprintf(render_capture_, "\n--JOT-RENDER-CAPTURE-END--\n");
    fclose(render_capture_);
    render_capture_ = nullptr;
  }
}

int Terminal::read_termkey_result() { return -1; }

int Terminal::read_key() { return -1; }

void Terminal::parse_mouse_event(int, MouseEvent &event) { reset_mouse_event(event); }

Event Terminal::poll_event() {
  Event ev{};
  ev.type = EVENT_REDRAW;
  DWORD wait_ms = (DWORD)std::clamp(poll_timeout_ms, 0, 250);
  if (WaitForSingleObject(input_handle(), wait_ms) == WAIT_OBJECT_0 &&
      peek_has_console_input()) {
    Event input = read_event();
    if (input.type != EVENT_REDRAW) {
      return input;
    }
  }
  Event rsz = check_resize_event();
  return rsz.type != EVENT_REDRAW ? rsz : ev;
}

Event Terminal::check_resize_event() {
  Event ev{};
  ev.type = EVENT_REDRAW;
  if (refresh_size()) {
    ev.type = EVENT_RESIZE;
    ev.resize.width = width;
    ev.resize.height = height;
  }
  return ev;
}

Event Terminal::read_event() {
  Event ev{};
  ev.type = EVENT_REDRAW;

  INPUT_RECORD record{};
  while (peek_has_console_input() && read_console_input(record)) {
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
      bool ctrl = false;
      bool shift = false;
      bool alt = false;
      int key = translate_key_event(record.Event.KeyEvent, ctrl, shift, alt);
      if (key < 0) {
        continue;
      }
      ev.type = EVENT_KEY;
      ev.key.key = key;
      ev.key.ctrl = ctrl || (key >= 1 && key <= 26 && key != 13 && key != 9);
      ev.key.shift = shift;
      ev.key.alt = alt;
      return ev;
    }
    if (record.EventType == MOUSE_EVENT) {
      if (translate_mouse_event(record.Event.MouseEvent, ev.mouse)) {
        ev.type = EVENT_MOUSE;
        return ev;
      }
      continue;
    }
    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      refresh_size(true);
      ev.type = EVENT_RESIZE;
      ev.resize.width = width;
      ev.resize.height = height;
      return ev;
    }
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
  if (render_capture_ && render_capture_raw_) {
    fwrite(buffer.c_str(), 1, n, render_capture_);
  }
  const char *p = buffer.c_str();
  size_t remaining = (size_t)n;
  while (remaining > 0) {
    DWORD written = 0;
    DWORD chunk = (DWORD)std::min<size_t>(remaining, 64 * 1024);
    if (!WriteFile(output_handle(), p, chunk, &written, nullptr) || written == 0) {
      break;
    }
    p += written;
    remaining -= written;
  }
  buffer.clear();
}

void Terminal::flush_if_buffer_exceeds() {
  if (render_chunk_bytes_ > 0 && buffer.size() >= render_chunk_bytes_) {
    flush();
  }
}

void Terminal::try_drain() { flush_if_buffer_exceeds(); }

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
void Terminal::set_bold(bool on) { buffer += on ? "\x1b[1m" : "\x1b[22m"; }
void Terminal::set_italic(bool on) { buffer += on ? "\x1b[3m" : "\x1b[23m"; }
void Terminal::set_reverse(bool on) { buffer += on ? "\x1b[7m" : "\x1b[27m"; }
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
  if (!render_capture_) {
    return;
  }
  render_capture_seq_++;
  fprintf(render_capture_,
          "\n--MARKER %d: %s bytes=%d w=%d h=%d rows=%d--\n",
          render_capture_seq_, label.c_str(), last_flush_bytes_, width, height,
          rows_rendered);
  fflush(render_capture_);
}
