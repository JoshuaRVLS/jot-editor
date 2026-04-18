#include "editor.h"

void Editor::handle_insert_mode(int ch, bool is_ctrl, bool is_shift,
                                bool is_alt) {
  if (is_ctrl && is_shift && (ch == 'f' || ch == 'F')) {
    select_current_function();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'l' || ch == 'L')) {
    select_current_line();
    return;
  }

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
    case 'g':
    case 'G':
      show_command_palette = true;
      command_palette_query = "line ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      needs_redraw = true;
      return;
    case 'p':
    case 'P':
      toggle_command_palette();
      needs_redraw = true;
      return;
    case 'm':
    case 'M':
      toggle_minimap();
      needs_redraw = true;
      return;
    case 't':
    case 'T':
      open_theme_chooser();
      needs_redraw = true;
      return;
    case 'd':
    case 'D':
      duplicate_line();
      return;
    case 'k':
    case 'K':
      delete_line();
      return;
    case '/':
    case '?':
    case 31:
      toggle_comment();
      return;
    case 'h':
    case 'H':
    case 'w':
    case 'W':
      delete_word_backward();
      return;
    case 127:
    case 8:
    case 23:
      delete_word_backward();
      return;
    case 1001:
      delete_word_forward();
      return;
    case 0:
    case ' ':
      return;
    }
    return;
  }

  if (ch == 27) {
    clear_selection();
    needs_redraw = true;
    return;
  }

  // Terminal fallback mappings for control bytes that may arrive without the
  // ctrl modifier bit on some terminals.
  if (ch == 19) { // Ctrl+S
    save_file();
    needs_redraw = true;
    return;
  }

  // Terminal fallback mappings for Ctrl+Backspace / Ctrl+/
  if (ch == 23) {
    delete_word_backward();
    return;
  }
  if (ch == 31) {
    toggle_comment();
    return;
  }

  // VSCode-like line move shortcut.
  if (is_alt && ch == 1008) {
    move_line_up();
    return;
  }
  if (is_alt && ch == 1009) {
    move_line_down();
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
    move_to_line_smart_start(is_shift);
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
    auto &buf = get_buffer();
    if (buf.selection.active) {
      indent_selection();
    } else {
      insert_char('\t');
    }
    needs_redraw = true;
    return;
  }

  // Shift+Tab (terminal escape \e[Z mapped in terminal.cpp)
  if (ch == 1017) {
    auto &buf = get_buffer();
    if (buf.selection.active) {
      outdent_selection();
    }
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
