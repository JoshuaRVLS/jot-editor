#include "editor.h"

void Editor::handle_normal_mode(int ch, bool is_ctrl, bool is_shift,
                                bool /*is_alt*/) {
  auto &buf = get_buffer();

  if (!is_ctrl) {
    recent_keys.push_back(ch);
    if ((int)recent_keys.size() > 8)
      recent_keys.erase(recent_keys.begin());

    if ((int)recent_keys.size() == 8 &&
        recent_keys[0] == 1008 && recent_keys[1] == 1008 &&
        recent_keys[2] == 1009 && recent_keys[3] == 1009 &&
        recent_keys[4] == 1011 && recent_keys[5] == 1010 &&
        recent_keys[6] == 1011 && recent_keys[7] == 1010) {
      easter_egg_timer = 180;
      recent_keys.clear();
      needs_redraw = true;
    }
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
    case 'p':
    case 'P':
      toggle_command_palette();
      needs_redraw = true;
      return;
    case 'b':
    case 'B':
      toggle_sidebar();
      return;
    case 'm':
    case 'M':
      toggle_minimap();
      needs_redraw = true;
      return;
    case 'r':
    case 'R':
      redo();
      needs_redraw = true;
      return;
    case 'n':
    case 'N':
      create_new_buffer();
      needs_redraw = true;
      return;
    case 'w':
    case 'W':
      close_buffer();
      needs_redraw = true;
      return;
    }
    (void)is_shift;
    return;
  }

  if (has_pending_key) {
    char first = pending_key;
    has_pending_key = false;
    pending_key = 0;

    if (first == 'g') {
      if (ch == 'g') {
        move_to_file_start();
        needs_redraw = true;
        return;
      }
    } else if (first == 'd') {
      if (ch == 'd') {
        vim_delete_line();
        needs_redraw = true;
        return;
      }
      if (ch == 'w') {
        move_word_forward();
        delete_char(false);
        needs_redraw = true;
        return;
      }
      return;
    } else if (first == 'y') {
      if (ch == 'y') {
        vim_yank();
        needs_redraw = true;
        return;
      }
      return;
    } else if (first == 'c') {
      if (ch == 'c') {
        save_state();
        buf.lines[buf.cursor.y] = "";
        buf.cursor.x = 0;
        buf.modified = true;
        enter_insert_mode();
        return;
      }
      return;
    } else if (first == 'r') {
      if (ch >= 32 && ch < 127) {
        save_state();
        int line_len = (int)buf.lines[buf.cursor.y].length();
        if (buf.cursor.x < line_len) {
          buf.lines[buf.cursor.y][buf.cursor.x] = (char)ch;
          buf.modified = true;
        }
        needs_redraw = true;
      }
      return;
    }
  }

  switch (ch) {
  case 'i':
    enter_insert_mode();
    return;
  case 'I':
    move_to_line_start();
    enter_insert_mode();
    return;
  case 'a':
    if (buf.cursor.x < (int)buf.lines[buf.cursor.y].length())
      buf.cursor.x++;
    enter_insert_mode();
    return;
  case 'A':
    move_to_line_end();
    enter_insert_mode();
    return;
  case 'o':
    insert_line_below();
    enter_insert_mode();
    return;
  case 'O':
    insert_line_above();
    enter_insert_mode();
    return;
  case 'v':
    enter_visual_mode(false);
    return;
  case 'V':
    enter_visual_mode(true);
    return;

  case 'h':
  case 1011:
    move_cursor(-1, 0);
    return;
  case 'l':
  case 1010:
    move_cursor(1, 0);
    return;
  case 'k':
  case 1008:
    move_cursor(0, -1);
    return;
  case 'j':
  case 1009:
    move_cursor(0, 1);
    return;
  case 'w':
    move_word_forward();
    needs_redraw = true;
    return;
  case 'b':
    move_word_backward();
    needs_redraw = true;
    return;
  case '0':
  case '^':
    move_to_line_start();
    needs_redraw = true;
    return;
  case '$':
    move_to_line_end();
    needs_redraw = true;
    return;
  case 'G':
    move_to_file_end();
    needs_redraw = true;
    return;
  case 'g':
    has_pending_key = true;
    pending_key = 'g';
    return;

  case 1015:
    move_cursor(0, -10);
    return;
  case 1016:
    move_cursor(0, 10);
    return;
  case 1012:
    move_to_line_start();
    needs_redraw = true;
    return;
  case 1013:
    move_to_line_end();
    needs_redraw = true;
    return;

  case 'x':
    vim_delete_char();
    return;
  case 'X':
    if (buf.cursor.x > 0) {
      save_state();
      buf.cursor.x--;
      buf.lines[buf.cursor.y].erase(buf.cursor.x, 1);
      buf.modified = true;
      clamp_cursor(get_pane().buffer_id);
      needs_redraw = true;
    }
    return;
  case 'd':
    has_pending_key = true;
    pending_key = 'd';
    return;
  case 'D':
    save_state();
    if (buf.cursor.x < (int)buf.lines[buf.cursor.y].length()) {
      buf.lines[buf.cursor.y].erase(buf.cursor.x);
      buf.modified = true;
      clamp_cursor(get_pane().buffer_id);
      needs_redraw = true;
    }
    return;
  case 'y':
    has_pending_key = true;
    pending_key = 'y';
    return;
  case 'p':
    vim_paste();
    return;
  case 'P':
    vim_paste();
    return;
  case 'u':
    undo();
    needs_redraw = true;
    return;
  case 'c':
    has_pending_key = true;
    pending_key = 'c';
    return;
  case 'r':
    has_pending_key = true;
    pending_key = 'r';
    return;
  case 'J':
    if (buf.cursor.y < (int)buf.lines.size() - 1) {
      save_state();
      buf.lines[buf.cursor.y] += " " + buf.lines[buf.cursor.y + 1];
      buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
      buf.modified = true;
      needs_redraw = true;
    }
    return;
  case '>': {
    save_state();
    buf.lines[buf.cursor.y].insert(0, "    ");
    buf.modified = true;
    needs_redraw = true;
    return;
  }
  case '<': {
    save_state();
    auto &line = buf.lines[buf.cursor.y];
    int count = 0;
    while (count < 4 && count < (int)line.size() && line[count] == ' ')
      count++;
    if (count > 0) {
      line.erase(0, count);
      buf.modified = true;
    }
    needs_redraw = true;
    return;
  }

  case '/':
    toggle_search();
    needs_redraw = true;
    return;
  case 'n':
    find_next();
    needs_redraw = true;
    return;
  case 'N':
    find_prev();
    needs_redraw = true;
    return;
  case ':':
    toggle_command_palette();
    needs_redraw = true;
    return;
  case 'm':
    toggle_bookmark();
    needs_redraw = true;
    return;
  case '[':
    prev_bookmark();
    needs_redraw = true;
    return;
  case ']':
    next_bookmark();
    needs_redraw = true;
    return;
  case '=':
    format_document();
    return;
  case '#':
    toggle_comment();
    return;
  case 'Y':
    duplicate_line();
    return;
  case 'T':
    trim_trailing_whitespace();
    return;
  case '\\':
    toggle_auto_indent_setting();
    return;
  case '+':
    change_tab_size(1);
    return;
  case '-':
    change_tab_size(-1);
    return;

  case 27:
    has_pending_key = false;
    clear_selection();
    needs_redraw = true;
    return;
  }
}
