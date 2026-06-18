#include "editor.h"

void Editor::enter_normal_mode() {
  mode = MODE_NORMAL;
  visual_line_mode = false;
  visual_motion_count = 0;
  has_pending_key = false;
  pending_key = 0;
  leader_key_pending = false;
  clear_selection();
  needs_redraw = true;
}

void Editor::enter_insert_mode() {
  mode = MODE_INSERT;
  visual_line_mode = false;
  visual_motion_count = 0;
  has_pending_key = false;
  pending_key = 0;
  leader_key_pending = false;
  clear_selection();
  needs_redraw = true;
}

void Editor::enter_visual_mode(bool line_mode) {
  auto &buf = get_buffer();
  mode = MODE_VISUAL;
  visual_line_mode = line_mode;
  visual_motion_count = 0;
  has_pending_key = false;
  pending_key = 0;
  leader_key_pending = false;
  visual_start = buf.cursor;

  if (line_mode) {
    buf.selection.start = {0, buf.cursor.y};
    buf.selection.end = {(int)buf.line(buf.cursor.y).length(), buf.cursor.y};
  } else {
    buf.selection.start = buf.cursor;
    buf.selection.end = buf.cursor;
  }
  buf.selection.active = true;
  needs_redraw = true;
}
