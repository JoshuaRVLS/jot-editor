#include "editor.h"
#include "editor_features.h"
#include "python_api.h"
#include <algorithm>

namespace {
int remove_one_indent_level(std::string &line, int tab_size) {
  if (line.empty())
    return 0;

  if (line[0] == '\t') {
    line.erase(0, 1);
    return 1;
  }

  int removed = 0;
  while (removed < tab_size && removed < (int)line.size() &&
         line[removed] == ' ') {
    removed++;
  }
  if (removed > 0) {
    line.erase(0, removed);
  }
  return removed;
}
} // namespace

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

void Editor::indent_selection() {
  auto &buf = get_buffer();
  if (!buf.selection.active)
    return;

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  const std::string indent(tab_size, ' ');

  for (int y = start_y; y <= end_y; y++) {
    buf.lines[y].insert(0, indent);
  }

  buf.selection.start.x += tab_size;
  buf.selection.end.x += tab_size;
  buf.cursor.x += tab_size;

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
}

void Editor::outdent_selection() {
  auto &buf = get_buffer();
  if (!buf.selection.active)
    return;

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);

  int removed_start = 0;
  int removed_end = 0;
  int removed_cursor = 0;

  for (int y = start_y; y <= end_y; y++) {
    int removed = remove_one_indent_level(buf.lines[y], tab_size);
    if (y == buf.selection.start.y)
      removed_start = removed;
    if (y == buf.selection.end.y)
      removed_end = removed;
    if (y == buf.cursor.y)
      removed_cursor = removed;
  }

  buf.selection.start.x = std::max(0, buf.selection.start.x - removed_start);
  buf.selection.end.x = std::max(0, buf.selection.end.x - removed_end);
  buf.cursor.x = std::max(0, buf.cursor.x - removed_cursor);

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (python_api)
    python_api->on_buffer_change(buf.filepath, "");
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
