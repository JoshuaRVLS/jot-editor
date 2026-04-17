#include "autoclose.h"
#include "editor.h"
#include "editor_features.h"
#include "python_api.h"

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
    // Check if we should skip closing bracket
    if (AutoClose::is_closing_bracket(c) &&
        AutoClose::should_skip_closing(c, buf.lines[buf.cursor.y],
                                       buf.cursor.x)) {
      buf.cursor.x++;
      needs_redraw = true;
      return;
    }

    buf.lines[buf.cursor.y].insert(buf.cursor.x, 1, c);
    buf.cursor.x++;

    if (auto_indent && (c == '}' || c == ']' || c == ')')) {
      if (EditorFeatures::should_dedent(buf.lines[buf.cursor.y])) {
        int current_indent =
            EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
        if (current_indent >= tab_size) {
          // Simply reduce by one tab stop
          int new_indent = current_indent - tab_size;
          std::string trimmed = buf.lines[buf.cursor.y];
          size_t start = trimmed.find_first_not_of(" \t");
          if (start != std::string::npos) {
            trimmed.erase(0, start);
            buf.lines[buf.cursor.y] =
                EditorFeatures::get_indent_string(new_indent, tab_size) +
                trimmed;
            buf.cursor.x = std::max(0, buf.cursor.x - tab_size);
          }
        }
      }
    }

    if (AutoClose::should_auto_close(c)) {
      char closing = AutoClose::get_closing_bracket(c);
      if (closing != '\0') {
        buf.lines[buf.cursor.y].insert(buf.cursor.x, 1, closing);
      }
    }
  }

  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
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
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
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
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
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
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
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
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}

void Editor::new_line() {
  save_state();
  auto &buf = get_buffer();
  std::string current_line = buf.lines[buf.cursor.y];
  std::string remaining = current_line.substr(buf.cursor.x);
  buf.lines[buf.cursor.y] = current_line.substr(0, buf.cursor.x);

  std::string new_line_str = "";
  bool split_closing_bracket_line = false;
  std::string closing_line_str = remaining;
  if (auto_indent && buf.cursor.y >= 0) {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    if (EditorFeatures::should_auto_indent(buf.lines[buf.cursor.y])) {
      indent += tab_size;
    }
    new_line_str = EditorFeatures::get_indent_string(indent, tab_size);

    if (EditorFeatures::should_dedent(remaining)) {
      size_t content_start = remaining.find_first_not_of(" \t");
      std::string trimmed_remaining =
          content_start == std::string::npos ? "" : remaining.substr(content_start);
      int closing_indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
      closing_line_str =
          EditorFeatures::get_indent_string(closing_indent, tab_size) +
          trimmed_remaining;
      split_closing_bracket_line = true;
    }
  }

  if (split_closing_bracket_line) {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, new_line_str);
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 2, closing_line_str);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  } else {
    buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1,
                     new_line_str + remaining);
    buf.cursor.y++;
    buf.cursor.x = new_line_str.length();
  }
  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}
