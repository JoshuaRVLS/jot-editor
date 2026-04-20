#include "editor.h"

void Editor::duplicate_selection_or_line() {
  auto &buf = get_buffer();
  if (buf.selection.active) {
    std::string prev_clipboard = clipboard;
    copy();
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x)) {
      std::swap(s, e);
    }
    clear_selection();
    buf.cursor = e;
    buf.preferred_x = buf.cursor.x;
    paste();
    clipboard = prev_clipboard;
    set_message("Duplicated selection");
    needs_redraw = true;
    return;
  }
  duplicate_line();
  set_message("Duplicated line");
}
