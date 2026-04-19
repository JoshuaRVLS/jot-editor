#include "editor.h"
#include <cctype>

static void update_visual_selection(FileBuffer &buf, const Cursor &visual_start,
                                    bool line_mode) {
  if (line_mode) {
    int sy = visual_start.y;
    int ey = buf.cursor.y;
    if (sy > ey)
      std::swap(sy, ey);
    buf.selection.start = {0, sy};
    buf.selection.end = {(int)buf.lines[ey].length(), ey};
  } else {
    buf.selection.start = visual_start;
    buf.selection.end = buf.cursor;
  }
  buf.selection.active = true;
}

void Editor::handle_visual_mode(int ch, bool is_ctrl, bool is_shift,
                                bool /*is_alt*/) {
  auto &buf = get_buffer();

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
    }
    return;
  }

  (void)is_shift;

  if (ch == 27) {
    enter_normal_mode();
    return;
  }

  if (ch == 'v' && !visual_line_mode) {
    enter_normal_mode();
    return;
  }
  if (ch == 'V' && visual_line_mode) {
    enter_normal_mode();
    return;
  }
  if (ch == 'v' && visual_line_mode) {
    visual_line_mode = false;
    visual_start = buf.cursor;
    update_visual_selection(buf, visual_start, false);
    needs_redraw = true;
    return;
  }
  if (ch == 'V' && !visual_line_mode) {
    visual_line_mode = true;
    update_visual_selection(buf, visual_start, true);
    needs_redraw = true;
    return;
  }

  auto move_and_update = [&](auto move_fn) {
    move_fn();
    update_visual_selection(buf, visual_start, visual_line_mode);
    needs_redraw = true;
  };

  switch (ch) {
  case 'h':
  case 1011:
    move_and_update([&] { move_cursor(-1, 0); });
    return;
  case 'l':
  case 1010:
    move_and_update([&] { move_cursor(1, 0); });
    return;
  case 'k':
  case 1008:
    move_and_update([&] { move_cursor(0, -1); });
    return;
  case 'j':
  case 1009:
    move_and_update([&] { move_cursor(0, 1); });
    return;
  case 'w':
    move_and_update([&] { move_word_forward(); });
    return;
  case 'b':
    move_and_update([&] { move_word_backward(); });
    return;
  case '0':
  case '^':
    move_and_update([&] { move_to_line_start(); });
    return;
  case '$':
    move_and_update([&] { move_to_line_end(); });
    return;
  case 'G':
    move_and_update([&] { move_to_file_end(); });
    return;
  case 1015:
    move_and_update([&] { move_cursor(0, -10); });
    return;
  case 1016:
    move_and_update([&] { move_cursor(0, 10); });
    return;

  case 'y':
    update_visual_selection(buf, visual_start, visual_line_mode);
    vim_yank();
    enter_normal_mode();
    return;

  case 'd':
  case 'x':
    update_visual_selection(buf, visual_start, visual_line_mode);
    save_state();
    delete_selection();
    enter_normal_mode();
    return;

  case 'c':
    update_visual_selection(buf, visual_start, visual_line_mode);
    save_state();
    delete_selection();
    enter_insert_mode();
    return;

  case '>': {
    save_state();
    Cursor s = buf.selection.start, e = buf.selection.end;
    if (s.y > e.y)
      std::swap(s, e);
    for (int ly = s.y; ly <= e.y && ly < (int)buf.lines.size(); ly++)
      buf.lines[ly].insert(0, "    ");
    buf.modified = true;
    update_visual_selection(buf, visual_start, visual_line_mode);
    needs_redraw = true;
    return;
  }
  case '<': {
    save_state();
    Cursor s = buf.selection.start, e = buf.selection.end;
    if (s.y > e.y)
      std::swap(s, e);
    for (int ly = s.y; ly <= e.y && ly < (int)buf.lines.size(); ly++) {
      auto &line = buf.lines[ly];
      int count = 0;
      while (count < 4 && count < (int)line.size() && line[count] == ' ')
        count++;
      if (count > 0)
        line.erase(0, count);
    }
    buf.modified = true;
    update_visual_selection(buf, visual_start, visual_line_mode);
    needs_redraw = true;
    return;
  }
  case '~': {
    save_state();
    Cursor s = buf.selection.start, e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x))
      std::swap(s, e);
    for (int ly = s.y; ly <= e.y && ly < (int)buf.lines.size(); ly++) {
      auto &line = buf.lines[ly];
      int x0 = (ly == s.y) ? s.x : 0;
      int x1 = (ly == e.y) ? e.x : (int)line.length();
      for (int xi = x0; xi < x1 && xi < (int)line.size(); xi++) {
        char c = line[xi];
        if (std::isupper((unsigned char)c))
          line[xi] = (char)std::tolower((unsigned char)c);
        else if (std::islower((unsigned char)c))
          line[xi] = (char)std::toupper((unsigned char)c);
      }
    }
    buf.modified = true;
    enter_normal_mode();
    return;
  }
  }
}
