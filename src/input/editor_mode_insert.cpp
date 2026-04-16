#include "editor.h"

void Editor::handle_insert_mode(int ch, bool is_ctrl, bool is_shift,
                                bool /*is_alt*/) {
  if (is_ctrl) {
    switch (ch) {
    case 'q':
    case 'Q': {
      bool unsaved = false;
      for (const auto &b : buffers)
        if (b.modified) {
          unsaved = true;
          break;
        }
      if (unsaved) {
        show_quit_prompt = true;
        needs_redraw = true;
      } else
        running = false;
      return;
    }
    case 's':
    case 'S':
      save_file();
      needs_redraw = true;
      return;
    case 'z':
    case 'Z':
      undo();
      needs_redraw = true;
      return;
    case 'y':
    case 'Y':
      redo();
      needs_redraw = true;
      return;
    case 'a':
    case 'A':
      select_all();
      needs_redraw = true;
      return;
    case 'c':
    case 'C':
      copy();
      return;
    case 'x':
    case 'X':
      cut();
      needs_redraw = true;
      return;
    case 'v':
    case 'V':
      paste();
      needs_redraw = true;
      return;
    case 'b':
    case 'B':
      toggle_sidebar();
      return;
    case 'f':
    case 'F':
      toggle_search();
      needs_redraw = true;
      return;
    case 'p':
    case 'P':
      show_command_palette = false;
      telescope.open(root_dir);
      needs_redraw = true;
      return;
    case 'm':
    case 'M':
      toggle_minimap();
      needs_redraw = true;
      return;
    }
    return;
  }

  if (ch == 27) {
    enter_normal_mode();
    return;
  }

  if (ch == 1008) {
    move_cursor(0, -1, is_shift);
    return;
  }
  if (ch == 1009) {
    move_cursor(0, 1, is_shift);
    return;
  }
  if (ch == 1010) {
    move_cursor(1, 0, is_shift);
    return;
  }
  if (ch == 1011) {
    move_cursor(-1, 0, is_shift);
    return;
  }
  if (ch == 1012) {
    move_to_line_start(is_shift);
    return;
  }
  if (ch == 1013) {
    move_to_line_end(is_shift);
    return;
  }
  if (ch == 1015) {
    move_cursor(0, -10, is_shift);
    return;
  }
  if (ch == 1016) {
    move_cursor(0, 10, is_shift);
    return;
  }

  if (ch == 127 || ch == 8) {
    delete_char(false);
    needs_redraw = true;
    return;
  }
  if (ch == 1001) {
    delete_char(true);
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13) {
    new_line();
    needs_redraw = true;
    return;
  }

  if (ch == '\t' || ch == 9) {
    insert_char('\t');
    needs_redraw = true;
    return;
  }

  if (ch >= 32 && ch < 1000) {
    auto &buf = get_buffer();
    if (buf.selection.active)
      delete_selection();
    insert_char((char)ch);
    needs_redraw = true;
    return;
  }
}
