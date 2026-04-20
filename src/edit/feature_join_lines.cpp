#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

namespace {
std::string ltrim_copy(const std::string &s) {
  size_t i = 0;
  while (i < s.size() && std::isspace((unsigned char)s[i])) {
    i++;
  }
  return s.substr(i);
}
} // namespace

void Editor::join_lines_selection_or_current() {
  auto &buf = get_buffer();
  if (buf.lines.size() <= 1) {
    set_message("Nothing to join");
    return;
  }

  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y + 1;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (end_y <= start_y) {
    set_message("Nothing to join");
    return;
  }

  save_state();
  int joins = 0;
  for (int y = start_y; y < end_y && y + 1 < (int)buf.lines.size();) {
    std::string left = buf.lines[y];
    std::string right = ltrim_copy(buf.lines[y + 1]);
    if (!left.empty() && !right.empty() &&
        !std::isspace((unsigned char)left.back())) {
      left.push_back(' ');
    }
    buf.lines[y] = left + right;
    buf.lines.erase(buf.lines.begin() + y + 1);
    end_y--;
    joins++;
  }

  if (joins == 0) {
    set_message("Nothing to join");
    return;
  }

  buf.modified = true;
  buf.cursor.y = start_y;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[start_y].size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Joined " + std::to_string(joins + 1) + " lines");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
