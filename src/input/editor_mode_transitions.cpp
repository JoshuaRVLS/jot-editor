#include "editor.h"

void Editor::enter_normal_mode() {
  // Modeless behavior keeps the editor in insert-style operation.
  enter_insert_mode();
}

void Editor::enter_insert_mode() {
  mode = MODE_INSERT;
  visual_line_mode = false;
  has_pending_key = false;
  pending_key = 0;
  clear_selection();
  needs_redraw = true;
}

void Editor::enter_visual_mode(bool line_mode) {
  (void)line_mode;
  // Visual mode is intentionally disabled in modeless editing.
  enter_insert_mode();
}
