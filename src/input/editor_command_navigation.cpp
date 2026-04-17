#include "editor.h"
#include "editor_features.h"
#include <algorithm>
#include <cctype>

void Editor::handle_telescope(int ch) {
  if (ch == 27) {
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
  } else if (ch == 1011) {
    telescope.go_parent();
    needs_redraw = true;
  } else if (ch == '\n' || ch == 13) {
    std::string path = telescope.get_selected_path();
    if (!path.empty()) {
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
    }
  } else if (ch == 1008 || ch == 'k') {
    telescope.move_up();
    needs_redraw = true;
  } else if (ch == 1009 || ch == 'j') {
    telescope.move_down();
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    std::string q = telescope.get_query();
    if (!q.empty()) {
      q.pop_back();
      telescope.set_query(q);
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    std::string q = telescope.get_query();
    q += ch;
    telescope.set_query(q);
    needs_redraw = true;
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
