#include "autoclose.h"
#include "editor.h"
#include "features.h"

void Editor::insert_char(char c) {
  save_state();
  auto &buf = get_buffer();
  if (buf.selection.active) {
    delete_selection();
  }

  if (c == '\t') {
    std::string spaces(tab_size, ' ');
    buf.lines[buf.cursor.y].insert(buf.cursor.x, spaces);
    buf.cursor.x += tab_size;
  } else {
    buf.lines[buf.cursor.y].insert(buf.cursor.x, 1, c);
    buf.cursor.x++;

    if (AutoClose::should_auto_close(c)) {
      char closing = AutoClose::get_closing_bracket(c);
      if (closing != '\0') {
        buf.lines[buf.cursor.y].insert(buf.cursor.x, 1, closing);
      }
    }
  }

  buf.modified = true;
  needs_redraw = true;
}

void Editor::insert_string(const std::string &str) {
  save_state();
  auto &buf = get_buffer();
  if (buf.selection.active) {
    delete_selection();
  }
  buf.lines[buf.cursor.y].insert(buf.cursor.x, str);
  buf.cursor.x += str.length();
  buf.modified = true;
}

void Editor::delete_char(bool forward) {
  save_state();
  auto &buf = get_buffer();
  if (buf.selection.active) {
    delete_selection();
    needs_redraw = true;
    return;
  }

  if (forward) {
    if (buf.cursor.x < (int)buf.lines[buf.cursor.y].length()) {
      buf.lines[buf.cursor.y].erase(buf.cursor.x, 1);
      buf.modified = true;
    } else if (buf.cursor.y < (int)buf.lines.size() - 1) {
      buf.lines[buf.cursor.y] += buf.lines[buf.cursor.y + 1];
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }
  } else {
    if (buf.cursor.x > 0) {
      buf.cursor.x--;
      buf.lines[buf.cursor.y].erase(buf.cursor.x, 1);
      buf.modified = true;
    } else if (buf.cursor.y > 0) {
      buf.cursor.y--;
      buf.cursor.x = buf.lines[buf.cursor.y].length();
      buf.lines[buf.cursor.y] += buf.lines[buf.cursor.y + 1];
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
    }
  }
  needs_redraw = true;
}

void Editor::delete_selection() {
  save_state();
  auto &buf = get_buffer();
  if (!buf.selection.active)
    return;

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

  if (start_y == end_y) {
    buf.lines[start_y].erase(start_x, end_x - start_x);
    buf.cursor.y = start_y;
    buf.cursor.x = start_x;
  } else {
    buf.lines[start_y] =
        buf.lines[start_y].substr(0, start_x) + buf.lines[end_y].substr(end_x);
    buf.lines.erase(buf.lines.begin() + start_y + 1,
                    buf.lines.begin() + end_y + 1);
    buf.cursor.y = start_y;
    buf.cursor.x = start_x;
  }

  buf.selection.active = false;
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  needs_redraw = true;
}

void Editor::delete_line() {
  save_state();
  auto &buf = get_buffer();
  if (buf.lines.size() == 1) {
    clipboard = buf.lines[0];
    buf.lines[0] = "";
  } else {
    clipboard = buf.lines[buf.cursor.y];
    buf.lines.erase(buf.lines.begin() + buf.cursor.y);
    if (buf.cursor.y >= (int)buf.lines.size())
      buf.cursor.y = buf.lines.size() - 1;
  }
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  needs_redraw = true;
}

void Editor::new_line() {
  save_state();
  auto &buf = get_buffer();
  std::string remaining = buf.lines[buf.cursor.y].substr(buf.cursor.x);
  buf.lines[buf.cursor.y] = buf.lines[buf.cursor.y].substr(0, buf.cursor.x);

  std::string new_line_str = "";
  if (auto_indent && buf.cursor.y >= 0) {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    if (EditorFeatures::should_auto_indent(buf.lines[buf.cursor.y])) {
      indent += tab_size;
    }
    new_line_str = EditorFeatures::get_indent_string(indent, tab_size);
  }

  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1,
                   new_line_str + remaining);
  buf.cursor.y++;
  buf.cursor.x = new_line_str.length();
  buf.modified = true;
  needs_redraw = true;
}

void Editor::duplicate_line() {
  save_state();
  auto &buf = get_buffer();
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1,
                   buf.lines[buf.cursor.y]);
  buf.cursor.y++;
  buf.modified = true;
  needs_redraw = true;
}

void Editor::toggle_comment() {
  save_state();
  auto &buf = get_buffer();
  std::string ext = get_file_extension(buf.filepath);
  std::string comment = "//";
  if (ext == ".py")
    comment = "#";
  else if (ext == ".html" || ext == ".xml")
    comment = "<!--";

  bool all_commented = true;
  int start_y = buf.selection.active
                    ? std::min(buf.selection.start.y, buf.selection.end.y)
                    : buf.cursor.y;
  int end_y = buf.selection.active
                  ? std::max(buf.selection.start.y, buf.selection.end.y)
                  : buf.cursor.y;

  for (int i = start_y; i <= end_y; i++) {
    if (buf.lines[i].substr(0, comment.length()) != comment) {
      all_commented = false;
      break;
    }
  }

  for (int i = start_y; i <= end_y; i++) {
    if (all_commented) {
      if (buf.lines[i].substr(0, comment.length()) == comment) {
        buf.lines[i] = buf.lines[i].substr(comment.length());
      }
    } else {
      buf.lines[i] = comment + buf.lines[i];
    }
  }

  buf.modified = true;
  needs_redraw = true;
}

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
