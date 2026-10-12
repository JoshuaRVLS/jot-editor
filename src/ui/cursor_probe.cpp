#include "ui/cursor_probe.h"

namespace jot_ui
{
  std::string cursor_probe_park_bytes(int park_x, int park_y)
  {
    // "\0337" is ESC 7 (DECSC): an octal escape stops after three digits, so the
    // 7 is a literal. The hide leads the write, so the park that follows can
    // never land with a cursor on screen.
    std::string bytes = "\033[?25l\0337";
    bytes += "\033[";
    bytes += std::to_string(park_y);
    bytes += ";";
    bytes += std::to_string(park_x);
    bytes += "H";
    bytes += "\033[6n";
    return bytes;
  }

  std::string cursor_probe_restore_bytes(bool cursor_shown)
  {
    std::string bytes = "\0338"; // ESC 8 (DECRC): the cell DECSC saved
    if (cursor_shown)
    {
      bytes += "\033[?25h"; // the visibility DECSC does not carry back
    }
    return bytes;
  }
} // namespace jot_ui
