#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <random>

void Editor::shuffle_selected_lines() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    set_message("Select lines first: :shufflelines");
    return;
  }

  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  start_y = std::clamp(start_y, 0, (int)buf.lines.size() - 1);
  end_y = std::clamp(end_y, 0, (int)buf.lines.size() - 1);
  if (start_y >= end_y) {
    set_message("Select multiple lines to shuffle");
    return;
  }

  save_state();
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1, gen);

  buf.modified = true;
  buf.cursor.y = start_y;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[start_y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Shuffled selected lines");
  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty()) {
    notify_lsp_change(buf.filepath);
  }
}
