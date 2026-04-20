#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

namespace {
bool is_blank_line(const std::string &line) {
  for (char c : line) {
    if (!std::isspace((unsigned char)c)) {
      return false;
    }
  }
  return true;
}
} // namespace

void Editor::trim_blank_lines_in_selection() {
  auto &buf = get_buffer();
  if (buf.lines.empty()) {
    set_message("Nothing to trim");
    return;
  }

  int start_y = 0;
  int end_y = (int)buf.lines.size() - 1;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }
  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);

  save_state();
  int removed = 0;
  for (int y = end_y; y >= start_y && y < (int)buf.lines.size(); y--) {
    if (!is_blank_line(buf.lines[y])) {
      continue;
    }
    if ((int)buf.lines.size() == 1) {
      break;
    }
    buf.lines.erase(buf.lines.begin() + y);
    removed++;
  }

  if (removed == 0) {
    set_message("No blank lines removed");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed " + std::to_string(removed) + " blank line(s)");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
