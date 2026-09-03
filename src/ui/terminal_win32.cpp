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

namespace
{
  HANDLE input_handle()
  {
    return GetStdHandle(STD_INPUT_HANDLE);
  }
  HANDLE output_handle()
  {
    return GetStdHandle(STD_OUTPUT_HANDLE);
  }

  DWORD g_original_input_mode = 0;
  DWORD g_original_output_mode = 0;
  bool g_have_input_mode = false;
  bool g_have_output_mode = false;
  UINT g_original_input_cp = 0;
  UINT g_original_output_cp = 0;
  bool g_have_cp = false;
  DWORD g_last_button_state = 0;

  bool read_console_input(INPUT_RECORD &record)
  {
    DWORD count = 0;
    return ReadConsoleInputW(input_handle(), &record, 1, &count) && count == 1;
  }

  bool peek_has_console_input()
  {
    DWORD count = 0;
    if (!GetNumberOfConsoleInputEvents(input_handle(), &count))
    {
      return false;
    }
    return count > 0;
  }

  int ctrl_char_for_letter(wchar_t ch)
  {
    if (ch >= L'a' && ch <= L'z')
    {
      return (int)(ch - L'a' + 1);
    }
    if (ch >= L'A' && ch <= L'Z')
    {
      return (int)(ch - L'A' + 1);
    }
    return 0;
  }

  int translate_key_event(const KEY_EVENT_RECORD &key, bool &ctrl, bool &shift, bool &alt)
  {
    const DWORD state = key.dwControlKeyState;
    ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    shift = (state & SHIFT_PRESSED) != 0;
    alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    switch (key.wVirtualKeyCode)
    {
    default:
      if (key.wVirtualKeyCode >= VK_F1 && key.wVirtualKeyCode <= VK_F24)
      {
        return KeyCode::function((int)(key.wVirtualKeyCode - VK_F1 + 1));
      }
      break;
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
    }

    wchar_t ch = key.uChar.UnicodeChar;
    if (ch == 0)
    {
      return -1;
    }
    if (ctrl)
    {
      int control = ctrl_char_for_letter(ch);
      if (control)
      {
        return control;
      }
    }
    if (ch >= L'A' && ch <= L'Z')
    {
      shift = true;
    }
    return (int)ch;
  }

  void reset_mouse_event(MouseEvent &event)
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

  bool translate_mouse_event(const MOUSE_EVENT_RECORD &mouse, MouseEvent &event)
  {
    reset_mouse_event(event);
    event.x = mouse.dwMousePosition.X;
    event.y = mouse.dwMousePosition.Y;

    const DWORD state = mouse.dwControlKeyState;
    event.ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    event.shift = (state & SHIFT_PRESSED) != 0;
    event.alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    if (mouse.dwEventFlags == MOUSE_WHEELED)
    {
      const short delta = HIWORD(mouse.dwButtonState);
      event.button = delta > 0 ? 64 : 65;
      event.pressed = true;
      return true;
    }

    DWORD changed = mouse.dwButtonState ^ g_last_button_state;
    if (mouse.dwEventFlags == MOUSE_MOVED)
    {
      if (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)
      {
        event.button = 0x20;
        return true;
      }
      return false;
    }

    if (changed & FROM_LEFT_1ST_BUTTON_PRESSED)
    {
      event.button = 0;
      event.pressed = (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
      event.released = !event.pressed;
      g_last_button_state = mouse.dwButtonState;
      return true;
    }
    if (changed & RIGHTMOST_BUTTON_PRESSED)
    {
      event.button = 2;
      event.pressed = (mouse.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;
      event.released = !event.pressed;
      g_last_button_state = mouse.dwButtonState;
      return true;
    }

    g_last_button_state = mouse.dwButtonState;
    return false;
  }

  void append_mouse_reset(std::string &buffer)
  {
    buffer += "\x1b[?1003l";
    buffer += "\x1b[?1002l";
    buffer += "\x1b[?1000l";
    buffer += "\x1b[?1006l";
    buffer += "\x1b[?1015l";
    buffer += "\x1b[?1004l";
    buffer += "\x1b[?2004l";
  }

  // ---------------------------------------------------------------------------
  // VT passthrough input.
  //
  // The renderer enables mouse tracking and bracketed paste by writing
  // \x1b[?1003h / \x1b[?2004h ... to the output stream. Some console
  // configurations (classic conhost, certain conpty versions) then deliver
  // the resulting input as raw byte records (wVirtualKeyCode == 0) even
  // though this backend reads with the legacy console API. Those bytes used
  // to be interpreted as regular key events, so hovering printed garbage
  // like "[MC4'" straight into the buffer. The assembler below reconstructs
  // the sequences (X10/SGR mouse reports, CSI keys, bracketed-paste
  // delimiters) into proper events; anything unrecognized is dropped instead
  // of being typed.
  // ---------------------------------------------------------------------------

  enum VtByteResult
  {
    kVtNone,  // consumed, waiting for more bytes
    kVtEvent, // out contains a complete event
    kVtDrop,  // consumed, deliberately discarded
  };

  int g_vt_state = 0;          // 0 idle, 1 ESC seen, 2 CSI, 3 X10 body, 4 SS3
  std::string g_vt_params;     // CSI parameter bytes
  int g_x10_pos = 0;           // X10 payload bytes consumed (out of 3)
  int g_x10_code = 0;          // X10 button byte, already -32
  int g_x10_bytes[2] = {0, 0}; // X10 x/y bytes, already -32
  int g_x10_last_button = 0;

  BOOL WINAPI ignore_console_control(DWORD type)
  {
    (void)type;
    // Swallow console control events (Ctrl+C, Ctrl+Break). With
    // ENABLE_PROCESSED_INPUT cleared, Ctrl+C also arrives as an ordinary key
    // event; this handler guarantees a control event can never tear the
    // editor down (which used to drop the user back into the shell while the
    // stale editor frame stayed on screen).
    return TRUE;
  }

  VtByteResult emit_vt_char(int byte, Event &out)
  {
    if (byte == 0)
    {
      return kVtDrop;
    }
    out.type = EVENT_KEY;
    out.key.key = byte;
    out.key.ctrl = byte >= 1 && byte <= 26 && byte != 9 && byte != 13;
    out.key.shift = false;
    out.key.alt = false;
    return kVtEvent;
  }

  void fill_mouse_event(int button,
                        int x,
                        int y,
                        bool ctrl,
                        bool shift,
                        bool alt,
                        bool pressed,
                        bool released,
                        MouseEvent &event)
  {
    reset_mouse_event(event);
    event.x = x;
    event.y = y;
    event.button = button;
    event.ctrl = ctrl;
    event.shift = shift;
    event.alt = alt;
    event.pressed = pressed;
    event.released = released;
  }

  // SGR mouse: ESC [ < b ; x ; y M | m  (x/y are 1-based; 'm' = release).
  // Mirrors the POSIX Terminal::parse_mouse_event semantics exactly so both
  // backends deliver identical MouseEvent values to the editor.
  VtByteResult decode_sgr_mouse(const std::string &params, char final, Event &out)
  {
    int b = 0;
    int x = 0;
    int y = 0;
    if (sscanf(params.c_str(), "<%d;%d;%d", &b, &x, &y) != 3 || x <= 0 || y <= 0)
    {
      return kVtDrop;
    }
    const bool release = final == 'm';
    const bool motion = (b & 0x20) != 0;
    const bool wheel = b >= 64 && b <= 67;
    MouseEvent m;
    fill_mouse_event(b,
                     x - 1,
                     y - 1,
                     (b & 16) != 0,
                     (b & 4) != 0,
                     (b & 8) != 0,
                     wheel || (!release && !motion),
                     release && !motion && !wheel,
                     m);
    out.type = EVENT_MOUSE;
    out.mouse = m;
    return kVtEvent;
  }

  VtByteResult decode_x10_mouse(int code, int x, int y, Event &out)
  {
    if (x <= 0 || y <= 0)
    {
      g_x10_last_button = 0;
      return kVtDrop;
    }
    const bool ctrl = (code & 16) != 0;
    const bool shift = (code & 4) != 0;
    const bool alt = (code & 8) != 0;
    MouseEvent m;
    if (code & 0x20)
    {
      // Motion (hover / drag): no press or release state.
      fill_mouse_event(code, x - 1, y - 1, ctrl, shift, alt, false, false, m);
    }
    else if ((code & 3) == 3)
    {
      // Release-all: report the button we last saw pressed.
      fill_mouse_event(g_x10_last_button, x - 1, y - 1, ctrl, shift, alt, false, true, m);
    }
    else
    {
      g_x10_last_button = code & 3;
      fill_mouse_event(code & 3, x - 1, y - 1, ctrl, shift, alt, true, false, m);
    }
    out.type = EVENT_MOUSE;
    out.mouse = m;
    return kVtEvent;
  }

  // A finished CSI sequence (params already collected, final byte in hand).
  VtByteResult finish_csi(const std::string &params, char final, Event &out)
  {
    if (!params.empty() && params[0] == '<')
    {
      return decode_sgr_mouse(params, final, out);
    }
    if (params.empty() && final == 'M')
    {
      // X10 mouse report: ESC [ M followed by three payload bytes.
      g_vt_state = 3;
      g_x10_pos = 0;
      g_x10_code = 0;
      g_x10_bytes[0] = 0;
      g_x10_bytes[1] = 0;
      return kVtNone;
    }
    if (params == "200" && final == '~')
    {
      return kVtDrop; // bracketed-paste start
    }
    if (params == "201" && final == '~')
    {
      return kVtDrop; // bracketed-paste end
    }

    // CSI key sequences (only reachable if a terminal feeds keys as bytes).
    int key = 0;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    size_t semi = params.find(';');
    std::string main = semi == std::string::npos ? params : params.substr(0, semi);
    if (semi != std::string::npos)
    {
      int mod = atoi(params.c_str() + (int)semi + 1);
      shift = (mod == 2 || mod == 4 || mod == 6 || mod == 8);
      alt = (mod == 3 || mod == 4 || mod == 7 || mod == 8);
      ctrl = (mod >= 5 && mod <= 8);
    }
    if (final == 'A')
      key = 1008;
    else if (final == 'B')
      key = 1009;
    else if (final == 'C')
      key = 1010;
    else if (final == 'D')
      key = 1011;
    else if (final == 'H')
      key = 1012;
    else if (final == 'F')
      key = 1013;
    else if (final == 'Z')
      key = 1017;
    else if (final == '~')
    {
      switch (atoi(main.c_str()))
      {
      case 1:
      case 7:
        key = 1012;
        break;
      case 3:
        key = 1001;
        break;
      case 4:
      case 8:
        key = 1013;
        break;
      case 5:
        key = 1015;
        break;
      case 6:
        key = 1016;
        break;
      default:
        return kVtDrop;
      }
    }
    else
    {
      return kVtDrop;
    }
    out.type = EVENT_KEY;
    out.key.key = key;
    out.key.ctrl = ctrl;
    out.key.shift = shift;
    out.key.alt = alt;
    return kVtEvent;
  }

  // Feed one raw byte from a VK==0 KEY_EVENT record into the VT assembler.
  VtByteResult feed_vt_byte(int byte, Event &out)
  {
    if (g_vt_state == 3)
    {
      // X10 payload: consume exactly three bytes after ESC [ M.
      if (g_x10_pos == 0)
      {
        g_x10_code = byte - 32;
      }
      else
      {
        g_x10_bytes[g_x10_pos - 1] = byte - 32;
      }
      g_x10_pos++;
      if (g_x10_pos < 3)
      {
        return kVtNone;
      }
      g_vt_state = 0;
      return decode_x10_mouse(g_x10_code, g_x10_bytes[0], g_x10_bytes[1], out);
    }
    if (g_vt_state == 2)
    {
      // Collecting CSI: parameters are 0x30..0x3F, final byte 0x40..0x7E.
      if (byte >= 0x30 && byte <= 0x3F)
      {
        g_vt_params.push_back((char)byte);
        return kVtNone;
      }
      if (byte >= 0x40 && byte <= 0x7E)
      {
        std::string params = g_vt_params;
        g_vt_params.clear();
        g_vt_state = 0;
        return finish_csi(params, (char)byte, out);
      }
      // Malformed: discard what we collected and retry this byte fresh.
      g_vt_params.clear();
      g_vt_state = 0;
      return feed_vt_byte(byte, out);
    }
    if (g_vt_state == 4)
    {
      g_vt_state = 0; // SS3 final byte (function keys): not supported as bytes
      return kVtDrop;
    }
    if (g_vt_state == 1)
    {
      g_vt_state = 0;
      if (byte == '[')
      {
        g_vt_state = 2;
        return kVtNone;
      }
      if (byte == 'O')
      {
        g_vt_state = 4;
        return kVtNone;
      }
      if (byte == 27)
      {
        // ESC ESC: treat as a single Escape key.
        out.type = EVENT_KEY;
        out.key.key = 27;
        out.key.ctrl = false;
        out.key.shift = false;
        out.key.alt = false;
        return kVtEvent;
      }
      // ESC + printable: swallow the ESC prefix, forward the character.
      return emit_vt_char(byte, out);
    }
    if (byte == 27)
    {
      g_vt_state = 1;
      return kVtNone;
    }
    return emit_vt_char(byte, out);
  }
} // namespace

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
  {
    return;
  }

  HANDLE in = input_handle();
  HANDLE out = output_handle();
  DWORD input_mode = 0;
  DWORD output_mode = 0;

  // The renderer emits UTF-8 (box-drawing characters, etc.). The console only
  // interprets bytes as UTF-8 when the output code page is set to CP_UTF8,
  // otherwise box-drawing chars render as mojibake (e.g. "Γöé").
  if (!g_have_cp)
  {
    g_original_input_cp = GetConsoleCP();
    g_original_output_cp = GetConsoleOutputCP();
    g_have_cp = true;
  }
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);

  if (GetConsoleMode(in, &input_mode))
  {
    g_original_input_mode = input_mode;
    g_have_input_mode = true;
    input_mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
    // NOTE: ENABLE_VIRTUAL_TERMINAL_INPUT must stay OFF. When it is on, the
    // console delivers navigation keys (arrows, home/end, ...) as raw VT
    // sequences split into separate KEY_EVENT records (ESC, '[', 'A') instead
    // of single VK_* records, and suppresses MOUSE_EVENT records entirely
    // (they arrive as SGR sequences). The parser below is built around
    // KEY_EVENT_RECORD/MOUSE_EVENT_RECORD, so VT input mode breaks both.
    input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
#ifdef ENABLE_QUICK_EDIT_MODE
    // Quick-edit is only honored when ENABLE_EXTENDED_FLAGS is set. Without
    // this dance the bit is silently ignored, the console keeps doing
    // text-selection on click, and we never receive MOUSE_EVENT records.
    input_mode &= ~ENABLE_QUICK_EDIT_MODE;
    input_mode |= ENABLE_EXTENDED_FLAGS;
#endif
    SetConsoleMode(in, input_mode);
  }
  if (GetConsoleMode(out, &output_mode))
  {
    g_original_output_mode = output_mode;
    g_have_output_mode = true;
    output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
#ifdef DISABLE_NEWLINE_AUTO_RETURN
    output_mode |= DISABLE_NEWLINE_AUTO_RETURN;
#endif
    SetConsoleMode(out, output_mode);
  }

  // Never let the console deliver Ctrl+C / Ctrl+Break as process-killing
  // control events. With ENABLE_PROCESSED_INPUT cleared above, ^C also
  // arrives as an ordinary key event; the handler guarantees a stray control
  // event (for example one generated while another console app runs in the
  // same window) cannot terminate jot mid-session.
  SetConsoleCtrlHandler(&ignore_console_control, TRUE);

  raw_mode = true;
}

void Terminal::disable_raw_mode()
{
  if (!raw_mode)
  {
    return;
  }
  if (g_have_input_mode)
  {
    SetConsoleMode(input_handle(), g_original_input_mode);
  }
  if (g_have_output_mode)
  {
    SetConsoleMode(output_handle(), g_original_output_mode);
  }
  if (g_have_cp)
  {
    SetConsoleCP(g_original_input_cp);
    SetConsoleOutputCP(g_original_output_cp);
  }
  SetConsoleCtrlHandler(&ignore_console_control, FALSE);
  raw_mode = false;
}

void Terminal::setup_terminal()
{
  write("\x1b[?1049h");
  write("\x1b[?25l");
  write("\x1b[2J");
  write("\x1b[H");
  write("\x1b[?2004h");
}

void Terminal::restore_terminal()
{
  append_mouse_reset(buffer);
  write("\x1b[?25h");
  write("\x1b[1 q");
  write("\x1b[?7h");
  write("\x1b[?1049l");
  reset_color();
  flush();
}

void Terminal::init()
{
  enable_raw_mode();
  setup_terminal();
  flush();

  const char *cp = getenv("JOT_RENDER_CAPTURE");
  if (cp && cp[0] != '\0')
  {
    render_capture_ = fopen(cp, "wb");
    if (render_capture_)
    {
      const char *raw = getenv("JOT_RENDER_CAPTURE_RAW");
      render_capture_raw_ = raw && raw[0] == '1';
      fprintf(
          render_capture_, "--JOT-RENDER-CAPTURE-START (raw=%d)--\n", render_capture_raw_ ? 1 : 0);
      fflush(render_capture_);
    }
  }

  refresh_size(true);
  if (width <= 0)
    width = 80;
  if (height <= 0)
    height = 24;
  render_margin_ = 1;

  const char *chunk = getenv("JOT_RENDER_CHUNK_BYTES");
  if (chunk && chunk[0] != '\0')
  {
    int value = atoi(chunk);
    if (value >= 0)
    {
      render_chunk_bytes_ = (size_t)value;
    }
  }

  enable_mouse();
}

bool Terminal::refresh_size(bool)
{
  CONSOLE_SCREEN_BUFFER_INFO info{};
  int new_width = width;
  int new_height = height;
  bool got = false;
  if (GetConsoleScreenBufferInfo(output_handle(), &info))
  {
    new_width = std::max(1, (int)(info.srWindow.Right - info.srWindow.Left + 1));
    new_height = std::max(1, (int)(info.srWindow.Bottom - info.srWindow.Top + 1));
    got = true;
  }
  const char *env_cols = getenv("COLUMNS");
  const char *env_lines = getenv("LINES");
  if (env_cols)
  {
    new_width = std::max(1, atoi(env_cols));
    got = true;
  }
  if (env_lines)
  {
    new_height = std::max(1, atoi(env_lines));
    got = true;
  }
  if (!got)
  {
    return false;
  }
  bool changed = new_width != width || new_height != height;
  width = new_width;
  height = new_height;
  return changed;
}

void Terminal::cleanup()
{
  disable_mouse();
  restore_terminal();
  disable_raw_mode();
  if (render_capture_)
  {
    fprintf(render_capture_, "\n--JOT-RENDER-CAPTURE-END--\n");
    fclose(render_capture_);
    render_capture_ = nullptr;
  }
}

int Terminal::read_termkey_result()
{
  return -1;
}

int Terminal::read_key()
{
  return -1;
}

void Terminal::parse_mouse_event(int, MouseEvent &event)
{
  reset_mouse_event(event);
}

Event Terminal::poll_event()
{
  Event ev{};
  ev.type = EVENT_REDRAW;
  DWORD wait_ms = (DWORD)std::clamp(poll_timeout_ms, 0, 250);
  if (WaitForSingleObject(input_handle(), wait_ms) == WAIT_OBJECT_0 && peek_has_console_input())
  {
    Event input = read_event();
    if (input.type != EVENT_REDRAW)
    {
      return input;
    }
  }
  Event rsz = check_resize_event();
  return rsz.type != EVENT_REDRAW ? rsz : ev;
}

Event Terminal::check_resize_event()
{
  Event ev{};
  ev.type = EVENT_REDRAW;
  if (refresh_size())
  {
    ev.type = EVENT_RESIZE;
    ev.resize.width = width;
    ev.resize.height = height;
  }
  return ev;
}

Event Terminal::read_event()
{
  Event ev{};
  ev.type = EVENT_REDRAW;

  // Drain whatever the console has queued right now: a VT mouse report or key
  // sequence arrives as several byte records, so a sequence can only be
  // reassembled when the whole batch is visible. Records we don't consume
  // stay in the queue for the next poll.
  int processed = 0;
  while (processed < 256 && peek_has_console_input())
  {
    processed++;
    INPUT_RECORD record{};
    if (!read_console_input(record))
    {
      break;
    }

    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
    {
      const KEY_EVENT_RECORD &key = record.Event.KeyEvent;
      if (key.wVirtualKeyCode == 0 && key.uChar.UnicodeChar != 0)
      {
        // Raw byte injected by the console (mouse reports, bracketed paste,
        // CSI sequences). Assemble it instead of typing it into the buffer.
        VtByteResult result = feed_vt_byte((int)key.uChar.UnicodeChar, ev);
        if (result == kVtEvent)
        {
          return ev;
        }
        continue;
      }

      bool ctrl = false;
      bool shift = false;
      bool alt = false;
      int keycode = translate_key_event(key, ctrl, shift, alt);
      if (keycode < 0)
      {
        continue;
      }
      ev.type = EVENT_KEY;
      ev.key.key = keycode;
      ev.key.ctrl = ctrl || (keycode >= 1 && keycode <= 26 && keycode != 13 && keycode != 9);
      ev.key.shift = shift;
      ev.key.alt = alt;
      return ev;
    }
    if (record.EventType == MOUSE_EVENT)
    {
      if (translate_mouse_event(record.Event.MouseEvent, ev.mouse))
      {
        ev.type = EVENT_MOUSE;
        return ev;
      }
      continue;
    }
    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
    {
      refresh_size(true);
      ev.type = EVENT_RESIZE;
      ev.resize.width = width;
      ev.resize.height = height;
      return ev;
    }
  }

  // A lone ESC byte with nothing after it in the queue is a real Escape key.
  if (g_vt_state == 1)
  {
    g_vt_state = 0;
    ev.type = EVENT_KEY;
    ev.key.key = 27;
    ev.key.ctrl = false;
    ev.key.shift = false;
    ev.key.alt = false;
  }
  return ev;
}

void Terminal::set_poll_timeout_ms(int timeout_ms)
{
  poll_timeout_ms = std::clamp(timeout_ms, 1, 250);
}

void Terminal::flush()
{
  int n = (int)buffer.length();
  last_flush_bytes_ = n;
  if (n <= 0)
  {
    return;
  }
  if (render_capture_ && render_capture_raw_)
  {
    fwrite(buffer.c_str(), 1, n, render_capture_);
  }
  const char *p = buffer.c_str();
  size_t remaining = (size_t)n;
  while (remaining > 0)
  {
    DWORD written = 0;
    DWORD chunk = (DWORD)std::min<size_t>(remaining, 64 * 1024);
    if (!WriteFile(output_handle(), p, chunk, &written, nullptr) || written == 0)
    {
      break;
    }
    p += written;
    remaining -= written;
  }
  buffer.clear();
}

void Terminal::flush_if_buffer_exceeds()
{
  if (render_chunk_bytes_ > 0 && buffer.size() >= render_chunk_bytes_)
  {
    flush();
  }
}

void Terminal::try_drain()
{
  flush_if_buffer_exceeds();
}

void Terminal::clear()
{
  buffer += "\x1b[2J";
  buffer += "\x1b[H";
}

void Terminal::move_cursor(int x, int y)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
  buffer += buf;
}

void Terminal::hide_cursor()
{
  buffer += "\x1b[?25l";
}
void Terminal::show_cursor()
{
  buffer += "\x1b[?25h";
}

void Terminal::set_color(int fg, int bg)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[38;5;%dm\x1b[48;5;%dm", fg, bg);
  buffer += buf;
}

void Terminal::reset_color()
{
  buffer += "\x1b[0m";
}
void Terminal::set_bold(bool on)
{
  buffer += on ? "\x1b[1m" : "\x1b[22m";
}
void Terminal::set_italic(bool on)
{
  buffer += on ? "\x1b[3m" : "\x1b[23m";
}
void Terminal::set_dim(bool on)
{
  buffer += on ? "\x1b[2m" : "\x1b[22m";
}
void Terminal::set_reverse(bool on)
{
  buffer += on ? "\x1b[7m" : "\x1b[27m";
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
  {
    return;
  }
  render_capture_seq_++;
  fprintf(render_capture_,
          "\n--MARKER %d: %s bytes=%d w=%d h=%d rows=%d--\n",
          render_capture_seq_,
          label.c_str(),
          last_flush_bytes_,
          width,
          height,
          rows_rendered);
  fflush(render_capture_);
}
