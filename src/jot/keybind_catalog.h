#ifndef KEYBIND_CATALOG_H
#define KEYBIND_CATALOG_H

// Chord-name helpers plus the shared CSI-u (kitty keyboard protocol) decoder.
//
// Only the prefix-group ("Ctrl+T N") branch of the which-key helper remains;
// the old "hold Ctrl → list every Ctrl+… binding" view was Windows-Terminal
//-only (bare modifier records exist only there), so the held-modifier row
// composer and its built-in table were removed.
//
// Kept free of Editor/Lua types so it is unit-testable and cheap to include:
// callers translate live plugin keymaps into KeymapRef entries first.

#include "ui/terminal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace jot
{

  // A live plugin keymap, projected to the fields the helper needs.
  struct KeymapRef
  {
    std::string key; // canonical key, e.g. "Ctrl+T N"
    std::string detail;
    bool has_action; // callback or command registered
  };

  namespace keybind_detail
  {
    // Canonical chord name for a decoded key, e.g. "Ctrl+Enter", "Alt+S",
    // "Ctrl+Shift+Enter". The editor dispatches plugin keymaps by this name,
    // so naming must be exact: named keys (Enter/Tab/Esc/Backspace/Space)
    // keep their name under modifiers, while plain ^A..^Z control codes
    // become their letter form ("Ctrl+S"). `original_ch` is the raw key
    // before ctrl-to-letter translation (when distinct from `ch`).
    inline std::string chord_name(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
    {
      int key = original_ch ? original_ch : ch;
      // Named keys keep their name even under Ctrl (e.g. Ctrl+Enter must not
      // become "Ctrl+M"): only control codes that are plain letters (^A..^Z)
      // translate to their letter form. Tab/Esc/Enter/Backspace/Space are
      // excluded from the letter range here and matched by name below.
      const bool named_ctrl_key =
          is_ctrl && key <= 26 && (key == 9 || key == 13 || key == 27 || key == 8 || key == 32);
      if (is_ctrl && key >= 1 && key <= 26 && !named_ctrl_key)
      {
        key += 96;
      }
      if ((key & 0x8000) != 0)
      {
        key &= 0x7FFF;
        is_shift = true;
      }

      std::string base;
      switch (key)
      {
      case 13:
      case '\n':
        base = "Enter";
        break;
      case 27:
        base = "Esc";
        break;
      case '\t':
        base = "Tab";
        break;
      case 127:
      case 8:
        base = "Backspace";
        break;
      case 1001:
        base = "Delete";
        break;
      case 1008:
        base = "Up";
        break;
      case 1009:
        base = "Down";
        break;
      case 1010:
        base = "Right";
        break;
      case 1011:
        base = "Left";
        break;
      case 1012:
        base = "Home";
        break;
      case 1013:
        base = "End";
        break;
      default:
        if ((key & KeyCode::FunctionMarker) != 0)
        {
          base = "F" + std::to_string((key & 0xFFFF) - KeyCode::FunctionBase);
          break;
        }
        if (key >= 32 && key < 127)
        {
          base = std::string(1, (char)std::toupper((unsigned char)key));
        }
        else
        {
          base = std::to_string(key);
        }
        break;
      }

      std::string out;
      if (is_ctrl)
      {
        out += "Ctrl+";
      }
      if (is_alt)
      {
        out += "Alt+";
      }
      if (is_shift)
      {
        out += "Shift+";
      }
      out += base;
      return out;
    }

    // Decodes a kitty keyboard-protocol sequence (CSI u) captured in `bytes`
    // (e.g. "\x1b[13;5u" = Ctrl+Enter). Returns the internal key code with
    // modifier flags (0x20000 Ctrl / 0x40000 Alt / 0x80000 Shift) or -1 when
    // the bytes are not a valid CSI-u key report. The protocol encodes the
    // modifier as bitmask + 1 (Shift=2, Alt=3, Ctrl=5, Ctrl+Shift=6,
    // Alt+Ctrl=7), NOT the raw bitmask: a naive "5 = Shift+Ctrl" mapping
    // turns Ctrl+Enter into Ctrl+Shift+Enter. The optional leading type
    // field ("CSI 1;code;mod u") is accepted too.
    inline int decode_csi_u_key(const std::string &bytes)
    {
      if (bytes.size() < 4 || bytes[0] != '\x1b' || bytes[1] != '[' || bytes.back() != 'u')
      {
        return -1;
      }
      const std::string body = bytes.substr(2, bytes.size() - 3); // drop ESC [ and u
      std::vector<std::string> parts;
      size_t start = 0;
      while (start <= body.size())
      {
        const size_t sep = body.find(';', start);
        const size_t end = (sep == std::string::npos) ? body.size() : sep;
        if (end > start)
        {
          parts.push_back(body.substr(start, end - start));
        }
        start = end + 1;
      }
      if (parts.empty())
      {
        return -1;
      }
      // Optional leading type field: "CSI 1;code;mod u" vs "CSI code;mod u".
      size_t code_pos = 0;
      if (parts.size() >= 3 && parts[0] == "1")
      {
        code_pos = 1;
      }
      char *end = nullptr;
      const long code = std::strtol(parts[code_pos].c_str(), &end, 10);
      if (!end || *end != '\0' || code < 1 || code > 0xFFFF)
      {
        return -1;
      }
      int flags = 0;
      if (code_pos + 1 < parts.size())
      {
        const long mod = std::strtol(parts[code_pos + 1].c_str(), &end, 10);
        if (!end || *end != '\0' || mod < 1)
        {
          return -1;
        }
        const long bits = mod - 1; // protocol: 1 + bitmask
        if (bits & 1)
          flags |= 0x80000; // Shift
        if (bits & 2)
          flags |= 0x40000; // Alt
        if (bits & 4)
          flags |= 0x20000; // Ctrl
      }
      // Map the code to the editor's key encoding. Codes >= 0x20 are plain
      // unicode codepoints; control keys keep their raw value. Uppercase
      // letters follow the shift convention used by the rest of the input
      // path.
      int key = (int)code;
      if (key >= 'a' && key <= 'z')
      {
        key = std::toupper(key);
      }
      return key | flags;
    }

    inline std::vector<std::string> split_steps(const std::string &key)
    {
      std::vector<std::string> steps;
      size_t start = 0;
      while (start <= key.size())
      {
        const size_t sp = key.find(' ', start);
        const size_t end = (sp == std::string::npos) ? key.size() : sp;
        if (end > start)
        {
          steps.push_back(key.substr(start, end - start));
        }
        start = end + 1;
      }
      return steps;
    }
  } // namespace keybind_detail

} // namespace jot

#endif
