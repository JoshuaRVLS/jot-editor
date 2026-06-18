#include "editor.h"

void Editor::vim_delete_line() {
  auto &buf = get_buffer();
  if (buf.line_count() == 0)
    return;

  clipboard = buf.line(buf.cursor.y);

  save_state();
  if (buf.is_lazy()) buf.materialize();
  buf.lines.erase(buf.lines.begin() + buf.cursor.y);
  if (buf.line_count() == 0) {
    buf.lines.push_back("");
  }
  if (buf.cursor.y >= (int)buf.line_count()) {
    buf.cursor.y = (int)buf.line_count() - 1;
  }
  buf.cursor.x = 0;
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  needs_redraw = true;
}

void Editor::vim_delete_char() {
  auto &buf = get_buffer();
  if (buf.cursor.x < (int)buf.line(buf.cursor.y).length()) {
    save_state();
    buf.line_mut(buf.cursor.y).erase(buf.cursor.x, 1);
    buf.modified = true;
    clamp_cursor(get_pane().buffer_id);
    needs_redraw = true;
  } else if (buf.cursor.y < (int)buf.line_count() - 1) {
    save_state();
    if (buf.is_lazy()) buf.materialize();
    std::string next_line = buf.line(buf.cursor.y + 1);
    buf.line_mut(buf.cursor.y) += next_line;
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
    needs_redraw = true;
  }
}

void Editor::vim_yank() {
  auto &buf = get_buffer();
  if (buf.selection.active) {
    copy();
  } else {
    clipboard = buf.line(buf.cursor.y);
    set_message("1 line yanked");
  }
}

void Editor::vim_paste() {
  paste();
  needs_redraw = true;
}
