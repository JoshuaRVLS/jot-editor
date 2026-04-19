#include "editor.h"
#include "text_features.h"
#include <algorithm>
#include <cctype>

void Editor::handle_telescope(int ch) {
  if (ch == 27) {
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
    return;
  }

  if (ch == 1011) {
    // Left arrow: go parent when query is empty, else clear one char.
    std::string q = telescope.get_query();
    if (q.empty()) {
      telescope.go_parent();
    } else {
      q.pop_back();
      telescope.set_query(q);
    }
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13) {
    std::string path = telescope.get_selected_path();
    if (path.empty()) {
      return;
    }
    if (fs::is_directory(path)) {
      telescope.select();
      needs_redraw = true;
      return;
    }

    std::string ext = get_file_extension(path);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
        ext == ".bmp" || ext == ".svg" || ext == ".webp" || ext == ".ico") {
      image_viewer.open(path);
    } else {
      open_file(path);
    }
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
    return;
  }

  if (ch == 1008 || ch == 'k' || ch == 16) { // Up or Ctrl+P
    telescope.move_up();
    needs_redraw = true;
    return;
  }
  if (ch == 1009 || ch == 'j' || ch == 14) { // Down or Ctrl+N
    telescope.move_down();
    needs_redraw = true;
    return;
  }
  if (ch == 1015) { // Page up
    for (int i = 0; i < 10; i++) {
      telescope.move_up();
    }
    needs_redraw = true;
    return;
  }
  if (ch == 1016) { // Page down
    for (int i = 0; i < 10; i++) {
      telescope.move_down();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8) {
    std::string q = telescope.get_query();
    if (!q.empty()) {
      q.pop_back();
      telescope.set_query(q);
    } else {
      telescope.go_parent();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 21) { // Ctrl+U: clear query
    telescope.set_query("");
    needs_redraw = true;
    return;
  }

  if (ch >= 32 && ch < 127) {
    std::string q = telescope.get_query();
    q += (char)ch;
    telescope.set_query(q);
    needs_redraw = true;
    return;
  }
}

void Editor::toggle_minimap() { show_minimap = !show_minimap; }

void Editor::jump_to_matching_bracket() {
  auto &buf = get_buffer();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
    return;
  if (buf.cursor.x < 0 || buf.cursor.x >= (int)buf.lines[buf.cursor.y].length())
    return;

  char ch = buf.lines[buf.cursor.y][buf.cursor.x];
  int match = -1;

  if (ch == '(')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '(', ')');
  else if (ch == ')')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '(', ')');
  else if (ch == '{')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '{', '}');
  else if (ch == '}')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '{', '}');
  else if (ch == '[')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '[', ']');
  else if (ch == ']')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y,
                                                  buf.cursor.x, '[', ']');

  if (match >= 0) {
    buf.cursor.y = match / 10000;
    buf.cursor.x = match % 10000;
    clamp_cursor(get_pane().buffer_id);
    ensure_cursor_visible();
    needs_redraw = true;
    message = "Jumped to matching bracket";
  }
}

void Editor::select_current_function() {
  auto &buf = get_buffer();
  if (buf.lines.empty()) {
    return;
  }

  int open_line = -1;
  int open_col = -1;
  int depth = 0;

  for (int y = buf.cursor.y; y >= 0; --y) {
    const std::string &line = buf.lines[y];
    if (line.empty()) {
      continue;
    }

    int start_x = (y == buf.cursor.y) ? std::min(buf.cursor.x, (int)line.size() - 1)
                                      : (int)line.size() - 1;
    for (int x = start_x; x >= 0; --x) {
      char ch = line[x];
      if (ch == '}') {
        depth++;
      } else if (ch == '{') {
        if (depth == 0) {
          open_line = y;
          open_col = x;
          break;
        }
        depth--;
      }
    }

    if (open_line != -1) {
      break;
    }
  }

  if (open_line == -1 || open_col == -1) {
    set_message("No surrounding function block found");
    needs_redraw = true;
    return;
  }

  int match = EditorFeatures::find_matching_bracket(buf.lines, open_line,
                                                    open_col, '{', '}');
  if (match < 0) {
    set_message("Function end not found");
    needs_redraw = true;
    return;
  }

  int close_line = match / 10000;
  int close_col = match % 10000;

  buf.selection.start = {0, open_line};
  buf.selection.end = {close_col + 1, close_line};
  buf.selection.active = true;
  buf.cursor = buf.selection.end;
  buf.preferred_x = buf.cursor.x;

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  set_message("Selected function block");
  needs_redraw = true;
}
