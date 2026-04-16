#include "editor.h"
#include "editor_features.h"
#include "python_api.h"

void Editor::duplicate_line() {
  save_state();
  auto &buf = get_buffer();
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1,
                   buf.lines[buf.cursor.y]);
  buf.cursor.y++;
  buf.modified = true;
  needs_redraw = true;
}

void Editor::insert_line_below() {
  save_state();
  auto &buf = get_buffer();
  // Compute indent from current line
  std::string indent_str = "";
  if (auto_indent) {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    if (EditorFeatures::should_auto_indent(buf.lines[buf.cursor.y]))
      indent += tab_size;
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, indent_str);
  buf.cursor.y++;
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (python_api) python_api->on_buffer_change(buf.filepath, "");
}

void Editor::insert_line_above() {
  save_state();
  auto &buf = get_buffer();
  std::string indent_str = "";
  if (auto_indent) {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y, indent_str);
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (python_api) python_api->on_buffer_change(buf.filepath, "");
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

