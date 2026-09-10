// SDL2 -> jot Event translation. Produces the exact conventions the
// terminal path uses, so every input handler works unchanged:
//
//  Keys:    text codepoints (uppercase A-Z carry the 0x8000 bit), special
//           codes 13/27/127/9/1001/1008-1017, KeyCode::function(n) for
//           F1-F24, modifier bits Shift=0x80000 Alt=0x40000 Ctrl=0x20000.
//  Mouse:   SGR-style button codes (0 left, 1 middle, 2 right, 3 release,
//           64/65/66/67 wheel, 0x20 motion, ctrl/shift/alt bits 0x10/0x04/
//           0x08) with x/y in cells.
//  Paste:   Ctrl+V synthesizes EVENT_PASTE from the SDL clipboard, the GUI
//           analogue of bracketed paste.
//  Resize:  SDL window (points) -> cell grid; pixel size feeds the GL
//           viewport.
#include "gui/gui.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstring>

namespace
{
int termkey_modifier_flags(bool shift, bool alt, bool ctrl)
{
  int flags = 0;
  if (shift)
    flags |= 0x80000;
  if (alt)
    flags |= 0x40000;
  if (ctrl)
    flags |= 0x20000;
  return flags;
}

// Maps one SDL keycode to jot's special key codes (the same values
// translate_termkey_keysym produces). Returns 0 when the key is not a
// navigation/special key (letters, digits and punctuation are handled by
// the printable branch in the caller).
int translate_sdl_keysym(SDL_Keycode k)
{
  switch (k)
  {
  case SDLK_BACKSPACE:
    return 127;
  case SDLK_TAB:
    return '\t';
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    return 13;
  case SDLK_ESCAPE:
    return 27;
  case SDLK_SPACE:
    return ' ';
  case SDLK_DELETE:
    return 1001;
  // Numpad +/- share the printable symbols so Ctrl+KP_Plus / Ctrl+KP_Minus
  // reach the editor as the same chords as the top-row keys (plain numpad
  // keys keep flowing through SDL text input as usual).
  case SDLK_KP_PLUS:
    return '+';
  case SDLK_KP_MINUS:
    return '-';
  case SDLK_UP:
    return 1008;
  case SDLK_DOWN:
    return 1009;
  case SDLK_RIGHT:
    return 1010;
  case SDLK_LEFT:
    return 1011;
  case SDLK_HOME:
    return 1012;
  case SDLK_END:
    return 1013;
  case SDLK_PAGEUP:
    return 1015;
  case SDLK_PAGEDOWN:
    return 1016;
  default:
    if (k >= SDLK_F1 && k <= SDLK_F12)
    {
      return KeyCode::function((int)(k - SDLK_F1) + 1);
    }
    if (k >= SDLK_F13 && k <= SDLK_F24)
    {
      return KeyCode::function((int)(k - SDLK_F13) + 13);
    }
    return 0;
  }
}
} // namespace

bool UIGui::poll_event(Event &out)
{
  // Draining multi-codepoint text input first keeps IME commits ordered.
  if (!pending_events_.empty())
  {
    out = pending_events_.front();
    pending_events_.erase(pending_events_.begin());
    return true;
  }

  SDL_Event ev;
  if (!SDL_PollEvent(&ev))
  {
    return false;
  }

  // Modifiers as captured on the event itself (keysym.mod), not the live
  // keyboard state: the event's snapshot is exact even if a modifier is
  // released before the event is polled, and synthetic/pushed events carry
  // their own state.
  const Uint16 mods = ev.type == SDL_KEYDOWN || ev.type == SDL_TEXTINPUT
                          ? ev.key.keysym.mod
                          : SDL_GetModState();
  const bool ctrl = (mods & KMOD_CTRL) != 0;
  const bool shift = (mods & KMOD_SHIFT) != 0;
  const bool alt = (mods & KMOD_ALT) != 0;

  switch (ev.type)
  {
  case SDL_QUIT:
    quit_requested_ = true;
    return false;

  case SDL_WINDOWEVENT:
    switch (ev.window.event)
    {
    case SDL_WINDOWEVENT_FOCUS_GAINED:
      out.type = EVENT_FOCUS_IN;
      return true;
    case SDL_WINDOWEVENT_FOCUS_LOST:
      out.type = EVENT_FOCUS_OUT;
      return true;
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    {
      // New window size; the grid is sized in cells. Drawable size (the
      // GL viewport, pixels) can differ from the point size on HiDPI.
      SDL_GL_GetDrawableSize(window_, &pixel_w_, &pixel_h_);
      int cols = std::max(1, (int)(ev.window.data1 / cell_w_));
      int rows = std::max(1, (int)(ev.window.data2 / cell_h_));
      if (std::getenv("JOT_GUI_DEBUG"))
      {
        std::fprintf(stderr, "jot-gui: window %s %dx%d -> grid %dx%d drawable=%dx%d\n",
                     ev.window.event == SDL_WINDOWEVENT_RESIZED ? "RESIZED" : "SIZE_CHANGED",
                     ev.window.data1, ev.window.data2, cols, rows, pixel_w_, pixel_h_);
      }
      if (cols == width && rows == height)
      {
        return false;
      }
      out.type = EVENT_RESIZE;
      out.resize.width = cols;
      out.resize.height = rows;
      return true;
    }
    default:
      return false;
    }

  case SDL_KEYDOWN:
  {
    // Ctrl+V is the GUI analogue of bracketed paste.
    if (ctrl && !ev.key.repeat && ev.key.keysym.sym == SDLK_v)
    {
      char *txt = SDL_GetClipboardText();
      if (txt && txt[0])
      {
        pending_paste_text_ = txt;
        out.type = EVENT_PASTE;
        out.paste.text = pending_paste_text_.c_str();
        SDL_free(txt);
        return true;
      }
      SDL_free(txt);
      return false;
    }
    // Modifier-only keys produce no events.
    if (ev.key.keysym.sym == SDLK_LSHIFT || ev.key.keysym.sym == SDLK_RSHIFT
        || ev.key.keysym.sym == SDLK_LCTRL || ev.key.keysym.sym == SDLK_RCTRL
        || ev.key.keysym.sym == SDLK_LALT || ev.key.keysym.sym == SDLK_RALT
        || ev.key.keysym.sym == SDLK_LGUI || ev.key.keysym.sym == SDLK_RGUI
        || ev.key.keysym.sym == SDLK_CAPSLOCK || ev.key.keysym.sym == SDLK_NUMLOCKCLEAR
        || ev.key.keysym.sym == SDLK_SCROLLLOCK)
    {
      return false;
    }

    int flags = termkey_modifier_flags(shift, alt, ctrl);
    int ch = translate_sdl_keysym(ev.key.keysym.sym);
    if (ch == 0)
    {
      // Printable ASCII. Without a chord modifier (Ctrl/Alt) the character
      // is delivered by SDL text input instead -- exactly one event per
      // keystroke, like the terminal's termkey path, with the shifted
      // symbol and the 0x8000 uppercase bit applied there. With Ctrl/Alt
      // held (text input suppressed), synthesize the chord from the
      // keycode the way termkey does: Ctrl+letter is a control byte
      // (1-26, TERMKEY_FLAG_CTRLC behavior -- the shared +96 conversion
      // in the event dispatcher turns it back into the letter), and
      // anything else keeps the unshifted codepoint plus modifier bits.
      if (ev.key.keysym.sym >= 32 && ev.key.keysym.sym <= 126)
      {
        if (!ctrl && !alt)
        {
          return false;
        }
        ch = (int)ev.key.keysym.sym;
        if (ctrl && ch >= 'a' && ch <= 'z')
        {
          ch = ch - 96; // control byte, like a terminal's Ctrl+letter
        }
      }
      else
      {
        return false;
      }
    }
    else if (ev.key.keysym.sym == SDLK_TAB && shift)
    {
      ch = 1017; // shift+tab
      flags &= ~0x80000;
    }

    // Shared with the terminal backend: strips modifier bits and the
    // 0x8000 uppercase bit into the ctrl/shift/alt booleans, unwraps
    // shifted arrows, and keeps function keys intact.
    out.type = EVENT_KEY;
    out.key = decode_key_event(ch | flags);
    return true;
  }

  case SDL_TEXTINPUT:
  {
    // Text input carries composed characters (IME). With Ctrl/Alt held the
    // chord is delivered via KEY_DOWN; only plain typing lands here.
    if (ctrl || alt)
    {
      return false;
    }
    const char *text = ev.text.text;
    size_t len = std::strlen(text);
    if (len == 0)
    {
      return false;
    }
    int flags = termkey_modifier_flags(shift, false, false);
    bool first = true;
    for (size_t i = 0; i < len;)
    {
      uint32_t cp = decode_utf8(text, len, i);
      if (cp == 0)
      {
        break;
      }
      int ch = (int)cp;
      if (shift && ch >= 'A' && ch <= 'Z')
      {
        ch |= 0x8000; // termkey marks shifted letters this way
      }
      Event key_ev;
      key_ev.type = EVENT_KEY;
      key_ev.key = decode_key_event(ch | flags);
      if (first)
      {
        out = key_ev;
        first = false;
      }
      else
      {
        pending_events_.push_back(key_ev);
      }
    }
    return !first;
  }

  case SDL_MOUSEMOTION:
  {
    int cx = std::clamp((int)(ev.motion.x / cell_w_), 0, width - 1);
    int cy = std::clamp((int)(ev.motion.y / cell_h_), 0, height - 1);
    // SGR motion encoding: 0x20 bit + the held button (0/1/2), or 3 when
    // no button is held (plain hover).
    int base = 3;
    if ((ev.motion.state & SDL_BUTTON_LMASK) != 0)
      base = 0;
    else if ((ev.motion.state & SDL_BUTTON_MMASK) != 0)
      base = 1;
    else if ((ev.motion.state & SDL_BUTTON_RMASK) != 0)
      base = 2;
    int button = 0x20 | base;
    if (ctrl)
      button |= 0x10;
    if (shift)
      button |= 0x04;
    if (alt)
      button |= 0x08;
    out.type = EVENT_MOUSE;
    out.mouse.x = cx;
    out.mouse.y = cy;
    out.mouse.button = button;
    out.mouse.pressed = false;
    out.mouse.released = false;
    out.mouse.ctrl = ctrl;
    out.mouse.shift = shift;
    out.mouse.alt = alt;
    return true;
  }

  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
  {
    int cx = std::clamp((int)(ev.button.x / cell_w_), 0, width - 1);
    int cy = std::clamp((int)(ev.button.y / cell_h_), 0, height - 1);
    bool down = ev.type == SDL_MOUSEBUTTONDOWN;
    int base = 3;
    if (down)
    {
      if (ev.button.button == SDL_BUTTON_LEFT)
        base = 0;
      else if (ev.button.button == SDL_BUTTON_MIDDLE)
        base = 1;
      else if (ev.button.button == SDL_BUTTON_RIGHT)
        base = 2;
    }
    int button = base;
    if (ctrl)
      button |= 0x10;
    if (shift)
      button |= 0x04;
    if (alt)
      button |= 0x08;
    out.type = EVENT_MOUSE;
    out.mouse.x = cx;
    out.mouse.y = cy;
    out.mouse.button = button;
    out.mouse.pressed = down;
    out.mouse.released = !down;
    out.mouse.ctrl = ctrl;
    out.mouse.shift = shift;
    out.mouse.alt = alt;
    return true;
  }

  case SDL_MOUSEWHEEL:
  {
    // The SDL2 ABI never bumped its soname while SDL_MouseWheelEvent grew
    // fields (preciseX/Y in 2.0.18, mouseX/Y in 2.26), so on systems where
    // the runtime .so was built against different headers than the installed
    // ones, y/preciseY can land swapped (y holds the pointer coordinate, the
    // notch delta shows up in preciseY). A wheel notch is a tiny integer
    // while a coordinate is large, so trust whichever field carries the
    // small value -- correct under both layouts.
    auto notch = [](float a, float b) -> float
    {
      const float aa = std::abs(a);
      const float bb = std::abs(b);
      if (aa < bb && aa < 100.0f)
      {
        return a;
      }
      return b;
    };
    const float dy = notch((float)ev.wheel.y, ev.wheel.preciseY);
    const float dx = notch((float)ev.wheel.x, ev.wheel.preciseX);
    // SGR wheel buttons: 64 up, 65 down, 66 left, 67 right. Direction
    // decides the button (SDL2 gives one notch per event).
    int button = 0;
    if (dy > 0.0f)
      button = 64;
    else if (dy < 0.0f)
      button = 65;
    else if (dx > 0.0f)
      button = 67;
    else if (dx < 0.0f)
      button = 66;
    else
      return false;
    if (ctrl)
      button |= 0x10;
    if (shift)
      button |= 0x04;
    if (alt)
      button |= 0x08;
    // Wheel events carry the pointer cell (SGR supplies coords). Prefer the
    // event's own mouseX/mouseY -- unlike y/preciseY those fields never
    // drifted, and SDL_GetMouseState reads (0,0) whenever the pointer is
    // not over this window (multi-monitor setups). Fall back to the live
    // state only when the event carries no coordinates.
    int mx = ev.wheel.mouseX;
    int my = ev.wheel.mouseY;
    if (mx == 0 && my == 0)
    {
      SDL_GetMouseState(&mx, &my);
    }
    out.type = EVENT_MOUSE;
    out.mouse.x = std::clamp((int)(mx / cell_w_), 0, width - 1);
    out.mouse.y = std::clamp((int)(my / cell_h_), 0, height - 1);
    out.mouse.button = button;
    out.mouse.pressed = true;
    out.mouse.released = false;
    out.mouse.ctrl = ctrl;
    out.mouse.shift = shift;
    out.mouse.alt = alt;
    return true;
  }

  default:
    return false;
  }
}

void UIGui::window_size(int &w, int &h) const
{
  SDL_GetWindowSize(window_, &w, &h);
}

void UIGui::drawable_size(int &w, int &h) const
{
  SDL_GL_GetDrawableSize(window_, &w, &h);
}

bool UIGui::note_drawable_size(int w, int h)
{
  if (w == pixel_w_ && h == pixel_h_)
  {
    return false;
  }
  pixel_w_ = w;
  pixel_h_ = h;
  return true;
}