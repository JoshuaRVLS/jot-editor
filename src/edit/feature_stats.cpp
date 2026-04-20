#include "editor.h"
#include <algorithm>
#include <cctype>

void Editor::show_buffer_stats() {
  auto &buf = get_buffer();
  int lines = (int)buf.lines.size();
  long long chars = 0;
  long long words = 0;
  bool in_word = false;

  for (const auto &line : buf.lines) {
    chars += (long long)line.size();
    for (char c : line) {
      if (std::isalnum((unsigned char)c) || c == '_') {
        if (!in_word) {
          words++;
          in_word = true;
        }
      } else {
        in_word = false;
      }
    }
    in_word = false;
  }

  std::string sel_text = "none";
  if (buf.selection.active) {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x)) {
      std::swap(s, e);
    }
    long long selected_chars = 0;
    for (int y = s.y; y <= e.y && y < (int)buf.lines.size(); y++) {
      if (y < 0) {
        continue;
      }
      const std::string &line = buf.lines[y];
      int from = (y == s.y) ? std::clamp(s.x, 0, (int)line.size()) : 0;
      int to = (y == e.y) ? std::clamp(e.x, 0, (int)line.size()) : (int)line.size();
      if (to < from) {
        std::swap(to, from);
      }
      selected_chars += (to - from);
    }
    sel_text = std::to_string(selected_chars) + " chars";
  }

  set_message("Stats: " + std::to_string(lines) + " lines, " +
              std::to_string(chars) + " chars, " + std::to_string(words) +
              " words, selection: " + sel_text);
  needs_redraw = true;
}
