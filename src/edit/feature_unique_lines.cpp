#include "editor.h"
#include "python_api.h"
#include <unordered_set>

void Editor::unique_selected_lines() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    set_message("Select lines first: :uniquelines");
    return;
  }

  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (start_y >= end_y) {
    set_message("Select multiple lines for unique");
    return;
  }

  save_state();
  std::unordered_set<std::string> seen;
  int removed = 0;
  for (int y = start_y; y <= end_y && y < (int)buf.lines.size();) {
    if (seen.insert(buf.lines[y]).second) {
      y++;
      continue;
    }
    buf.lines.erase(buf.lines.begin() + y);
    end_y--;
    removed++;
  }

  if (removed == 0) {
    set_message("No duplicate lines found");
    return;
  }

  buf.modified = true;
  buf.cursor.y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[buf.cursor.y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed " + std::to_string(removed) + " duplicate line(s)");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
