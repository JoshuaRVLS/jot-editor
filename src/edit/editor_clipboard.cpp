#include "editor.h"
#include "python_api.h"
#include <sstream>

void Editor::copy() {
  auto &buf = get_buffer();
  if (!buf.selection.active) {
    clipboard = buf.lines[buf.cursor.y];
    return;
  }

  clipboard = "";
  int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  int start_x =
      buf.selection.start.y < buf.selection.end.y
          ? buf.selection.start.x
          : (buf.selection.start.y == buf.selection.end.y
                 ? std::min(buf.selection.start.x, buf.selection.end.x)
                 : buf.selection.end.x);
  int end_x = buf.selection.start.y < buf.selection.end.y
                  ? buf.selection.end.x
                  : (buf.selection.start.y == buf.selection.end.y
                         ? std::max(buf.selection.start.x, buf.selection.end.x)
                         : buf.selection.start.x);

  for (int i = start_y; i <= end_y; i++) {
    if (i == start_y && i == end_y) {
      clipboard += buf.lines[i].substr(start_x, end_x - start_x);
    } else if (i == start_y) {
      clipboard += buf.lines[i].substr(start_x) + "\n";
    } else if (i == end_y) {
      clipboard += buf.lines[i].substr(0, end_x);
    } else {
      clipboard += buf.lines[i] + "\n";
    }
  }
}

void Editor::cut() {
  copy();
  delete_selection();
}

void Editor::paste() {
  if (clipboard.empty())
    return;
  save_state();
  auto &buf = get_buffer();
  if (buf.selection.active) {
    delete_selection();
  }

  std::istringstream iss(clipboard);
  std::string line;
  bool first = true;
  while (std::getline(iss, line)) {
    if (!first) {
      buf.lines.insert(buf.lines.begin() + buf.cursor.y, line);
      buf.cursor.y++;
    } else {
      buf.lines[buf.cursor.y].insert(buf.cursor.x, line);
      buf.cursor.x += line.length();
    }
    first = false;
  }

  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}

void Editor::move_line_up() {
  save_state();
  auto &buf = get_buffer();
  if (buf.cursor.y > 0) {
    std::swap(buf.lines[buf.cursor.y], buf.lines[buf.cursor.y - 1]);
    buf.cursor.y--;
    buf.modified = true;
    needs_redraw = true;
  }
}

void Editor::move_line_down() {
  save_state();
  auto &buf = get_buffer();
  if (buf.cursor.y < (int)buf.lines.size() - 1) {
    std::swap(buf.lines[buf.cursor.y], buf.lines[buf.cursor.y + 1]);
    buf.cursor.y++;
    buf.modified = true;
    needs_redraw = true;
  }
}
