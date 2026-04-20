#include "editor.h"
#include "python_api.h"
#include <algorithm>

void Editor::reverse_selected_lines() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    set_message("Select lines first: :reverselines");
    return;
  }

  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (start_y >= end_y) {
    set_message("Select multiple lines to reverse");
    return;
  }

  save_state();
  std::reverse(buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1);
  buf.modified = true;
  buf.cursor.y = start_y;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[start_y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Reversed selected lines");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
