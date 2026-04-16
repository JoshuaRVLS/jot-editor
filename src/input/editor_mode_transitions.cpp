#include "editor.h"

void Editor::enter_normal_mode() {
  mode = MODE_NORMAL;
  visual_line_mode = false;
  has_pending_key = false;
  pending_key = 0;

  auto &buf = get_buffer();
  int line_len = (int)buf.lines[buf.cursor.y].length();
  if (line_len > 0 && buf.cursor.x >= line_len)
    buf.cursor.x = line_len - 1;

  clear_selection();
  needs_redraw = true;
}

void Editor::enter_insert_mode() {
  mode = MODE_INSERT;
  has_pending_key = false;
  pending_key = 0;
  clear_selection();
  needs_redraw = true;
}

void Editor::enter_visual_mode(bool line_mode) {
  mode = MODE_VISUAL;
  visual_line_mode = line_mode;
  has_pending_key = false;
  pending_key = 0;

  auto &buf = get_buffer();
  visual_start = buf.cursor;
  buf.selection.start = buf.cursor;
  buf.selection.end = buf.cursor;
  buf.selection.active = true;
  needs_redraw = true;
}
