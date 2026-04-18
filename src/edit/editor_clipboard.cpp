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
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (start_y <= 0)
    return;

  // Move the whole selected line block up by one row.
  std::rotate(buf.lines.begin() + start_y - 1, buf.lines.begin() + start_y,
              buf.lines.begin() + end_y + 1);

  buf.cursor.y = std::max(0, buf.cursor.y - 1);
  if (buf.selection.active) {
    buf.selection.start.y -= 1;
    buf.selection.end.y -= 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}

void Editor::move_line_down() {
  save_state();
  auto &buf = get_buffer();
  int start_y = buf.cursor.y;
  int end_y = buf.cursor.y;
  if (buf.selection.active) {
    start_y = std::min(buf.selection.start.y, buf.selection.end.y);
    end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  }

  if (end_y >= (int)buf.lines.size() - 1)
    return;

  // Move the whole selected line block down by one row.
  std::rotate(buf.lines.begin() + start_y, buf.lines.begin() + end_y + 1,
              buf.lines.begin() + end_y + 2);

  buf.cursor.y = std::min((int)buf.lines.size() - 1, buf.cursor.y + 1);
  if (buf.selection.active) {
    buf.selection.start.y += 1;
    buf.selection.end.y += 1;
  }

  buf.modified = true;
  ensure_cursor_visible();
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}
