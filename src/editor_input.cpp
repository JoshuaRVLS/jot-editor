#include "autoclose.h"
#include "editor.h"
#include "features.h"
#include "python_api.h"
#include <iostream>
#include <sstream>

struct MEVENT {
  int x, y;
  int bstate;
};

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt,
                          int original_ch) {
  idle_frame_count = 0;
  cursor_visible = true;
  cursor_blink_frame = 0;

  // Python keybinds
  if (python_api) {
    // Always use "normal" or "insert" strings for compatibility
    // or "all" mode since we are now modeless
    if (python_api->handle_keybind(original_ch, is_ctrl, is_shift, is_alt,
                                   "insert")) {
      needs_redraw = true;
      return;
    }
  }

  if (show_sidebar) {
    // Basic modal logic: if we are "in sidebar", handle it.
    // But we don't have distinct focus state yet.
    // Let's assume for now sidebar is just for mouse or Ctrl+B to toggle.
    // Or maybe Arrow keys work if we track focus?
    // Let's keep it simple: input goes to editor unless specialized keys?
    // Actually, VS Code requires clicking sidebar to focus it.
    // Let's defer complex focus logic.
    // Just ensure Ctrl+B works which is already added.
    // Mouse input will be handled in handle_mouse_input.
  }

  // Ctrl+W: Close Pane/Buffer OR Switch focus from Sidebar
  if (ch == 23) {
    if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      focus_state = FOCUS_EDITOR;
    } else {
      close_pane();
    }
    needs_redraw = true;
    return;
  }

  // Ctrl+L: Focus Editor (or Redraw?)
  if (ch == 12) {
    focus_state = FOCUS_EDITOR;
    needs_redraw = true;
    return;
  }

  // NOTE: Backspace (8/127) trap removed here to allow text editing usage
  // below.

  if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
    // If Escape, return to editor
    if (ch == 27) {
      focus_state = FOCUS_EDITOR;
      needs_redraw = true;
      return;
    }
    handle_sidebar_input(ch);
    return;
  }

  if (show_quit_prompt) {
    if (ch == 'y' || ch == 'Y' || ch == '\n') {
      running = false;
    } else if (ch == 'n' || ch == 'N' || ch == 27) {
      show_quit_prompt = false;
      needs_redraw = true;
      set_message("Quit cancelled");
    }
    return;
  }

  if (show_save_prompt) {
    handle_save_prompt(ch);
    return;
  }

  if (image_viewer.is_active()) {
    if (ch == 'q' || ch == 27) {
      image_viewer.close();
      needs_redraw = true;
      return;
    }
  }

  // --- Ctrl+Alt Shortcuts ---
  if (is_ctrl && is_alt) {
    if (ch == 1008) { // Up
      move_line_up();
      needs_redraw = true;
      return;
    } else if (ch == 1009) { // Down
      move_line_down();
      needs_redraw = true;
      return;
    }
  }

  // --- Shortcuts ---
  if (is_ctrl) {
    if (ch == 'q' || ch == 'Q') {
      // Check if any buffer is modified
      bool unsaved = false;
      for (const auto &buf : buffers) {
        if (buf.modified) {
          unsaved = true;
          break;
        }
      }

      if (unsaved) {
        show_quit_prompt = true;
        needs_redraw = true;
      } else {
        running = false;
      }
      return;
    } else if (ch == 's' || ch == 'S') {
      save_file();
      needs_redraw = true;
      return;
    } else if (ch == 'o' || ch == 'O') {
      // Open file dialog isn't implemented yet, but we could add it
      // handle_open_dialog();
      return;
    } else if (ch == 'n' || ch == 'N') {
      create_new_buffer();
      needs_redraw = true;
      return;
    } else if (ch == 'w' || ch == 'W') {
      close_buffer();
      needs_redraw = true;
      return;
    } else if (ch == 'p' || ch == 'P') {
      toggle_command_palette();
      needs_redraw = true;
      return;
    } else if (ch == 'f' || ch == 'F') {
      toggle_search();
      needs_redraw = true;
      return;
    } else if (ch == 'c' || ch == 'C') { // Copy
      copy();
      return;
    } else if (ch == 'x' || ch == 'X') { // Cut
      cut();
      needs_redraw = true;
      return;
    } else if (ch == 'v' || ch == 'V') { // Paste
      paste();
      needs_redraw = true;
      return;
    } else if (ch == 'z' || ch == 'Z') { // Undo
      undo();
      needs_redraw = true;
      return;
    } else if (ch == 'y' || ch == 'Y') { // Redo
      redo();
      needs_redraw = true;
      return;
    } else if (ch == 'a' || ch == 'A') { // Select All
      select_all();
      needs_redraw = true;
      return;
    } else if (ch == 'm' || ch == 'M') {
      toggle_minimap();
      needs_redraw = true;
      return;
    } else if (ch == 'b' || ch == 'B') {
      toggle_sidebar();
      return;
    }
    // Ignore other Ctrl combinations to prevent inserting control chars
    return;
  }

  // --- Navigation & Selection ---

  // Arrow Keys
  // Mapping: Up=1008, Down=1009, Right=1010, Left=1011
  // Home=1012, End=1013, PgUp=1015, PgDn=1016

  if (ch == 1008) { // Up
    move_cursor(0, -1, is_shift);
  } else if (ch == 1009) { // Down
    move_cursor(0, 1, is_shift);
  } else if (ch == 1010) { // Right
    move_cursor(1, 0, is_shift);
  } else if (ch == 1011) { // Left
    move_cursor(-1, 0, is_shift);
  } else if (ch == 1012) { // Home
    move_to_line_start(is_shift);
  } else if (ch == 1013) { // End
    move_to_line_end(is_shift);
  } else if (ch == 1015) { // PgUp
    move_cursor(0, -10, is_shift);
  } else if (ch == 1016) { // PgDn
    move_cursor(0, 10, is_shift);
  }

  // Backspace & Delete
  else if (ch == 127 || ch == 8) { // Backspace
    delete_char(false);
    needs_redraw = true;
  } else if (ch == 1001) { // Delete
    delete_char(true);
    needs_redraw = true;
  }

  // Enter / Tab
  else if (ch == '\n' || ch == 13) {
    new_line();
    needs_redraw = true;
  } else if (ch == '\t' || ch == 9) {
    insert_char('\t');
    needs_redraw = true;
  }

  // Ctrl+Alt+Arrows handled above

  // Text Input
  else if (ch >= 32 &&
           ch < 1000) { // Printable (and valid unicode ranges mostly)
    // If we have a selection and typing a char, usually we delete selection
    // first But insert_char might ideally handle that or we do it here
    auto &buf = get_buffer();
    if (buf.selection.active) {
      delete_selection();
    }
    insert_char((char)ch); // Casting for now, assume ASCII/UTF8 basic
    needs_redraw = true;
  }
}

void Editor::handle_save_prompt(int ch) {
  if (ch == 27) { // Escape
    show_save_prompt = false;
    needs_redraw = true;
    set_message("Save cancelled");
  } else if (ch == '\n' || ch == 13) { // Enter
    show_save_prompt = false;
    if (!save_prompt_input.empty()) {
      get_buffer().filepath = save_prompt_input;
      save_file();
    } else {
      set_message("Save cancelled (empty filename)");
      needs_redraw = true;
    }
  } else if (ch == 127 || ch == 8) { // Backspace
    if (!save_prompt_input.empty()) {
      save_prompt_input.pop_back();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) { // Printable chars
    save_prompt_input += (char)ch;
    needs_redraw = true;
  }
}

// Old mode handlers removed
// void Editor::enter_normal_mode() {}
// void Editor::enter_insert_mode() {}
// void Editor::enter_visual_mode() {}
// void Editor::handle_normal_mode(...) {}
// void Editor::handle_insert_mode(...) {}
// void Editor::handle_visual_mode(...) {}

void Editor::vim_delete_line() {
  auto &buf = get_buffer();
  if (buf.lines.empty())
    return;

  save_state();
  buf.lines.erase(buf.lines.begin() + buf.cursor.y);
  if (buf.lines.empty()) {
    buf.lines.push_back("");
  }
  if (buf.cursor.y >= (int)buf.lines.size()) {
    buf.cursor.y = buf.lines.size() - 1;
  }
  buf.cursor.x = 0;
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  needs_redraw = true;
}

void Editor::vim_delete_char() {
  auto &buf = get_buffer();
  if (buf.cursor.x < (int)buf.lines[buf.cursor.y].length()) {
    save_state();
    buf.lines[buf.cursor.y].erase(buf.cursor.x, 1);
    buf.modified = true;
    clamp_cursor(get_pane().buffer_id);
    needs_redraw = true;
  } else if (buf.cursor.y < (int)buf.lines.size() - 1) {
    // Join with next line
    save_state();
    std::string next_line = buf.lines[buf.cursor.y + 1];
    buf.lines[buf.cursor.y] += next_line;
    buf.lines.erase(buf.lines.begin() + buf.cursor.y + 1);
    buf.modified = true;
    needs_redraw = true;
  }
}

void Editor::vim_yank() { copy(); }

void Editor::vim_paste() {
  paste();
  needs_redraw = true;
}

void Editor::handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                                bool is_scroll_down) {
  if (show_sidebar) {
    if (x < sidebar_width) {
      if (is_scroll_up) {
        if (file_tree_scroll > 0)
          file_tree_scroll--;
        needs_redraw = true;
      } else if (is_scroll_down) {
        file_tree_scroll++;
        needs_redraw = true;
      } else if (is_click) { // Explicitly check is_click before focusing
        focus_state = FOCUS_SIDEBAR; // Focus on click
        handle_sidebar_mouse(x, y, is_click);
      }
      return;
    }
  }

  // If clicked inside editor area, focus editor
  if (is_click) {
    focus_state = FOCUS_EDITOR;
  }

  // Scroll
  auto &buf = get_buffer();
  auto &pane = get_pane();

  if (is_scroll_up) {
    if (buf.scroll_offset > 0) {
      buf.scroll_offset -= 3;
      if (buf.scroll_offset < 0)
        buf.scroll_offset = 0;
      needs_redraw = true;
    }
    return;
  }
  if (is_scroll_down) {
    if (buf.scroll_offset < (int)buf.lines.size() - pane.h + 1) {
      buf.scroll_offset += 3;
      if (buf.scroll_offset > (int)buf.lines.size() - 1)
        buf.scroll_offset = (int)buf.lines.size() - 1;
      needs_redraw = true;
    }
    return;
  }
}

void Editor::handle_mouse(void *event_ptr) {
  MEVENT *event = (MEVENT *)event_ptr;
  if (panes.empty())
    return;

  auto &pane = get_pane();

  int bstate = event->bstate;

  if (bstate == 3) {
    show_context_menu = true;
    context_menu_x = event->x;
    context_menu_y = event->y;
    context_menu_selected = 0;
    needs_redraw = true;
    return;
  }

  auto &buf = get_buffer(pane.buffer_id);

  // We should only handle focus changes on BUTTON PRESS (bstate == 1) or
  // RELEASE (bstate == 2) Not on every event (which includes motion).

  bool is_click = (bstate == 1);

  // Check if click is in sidebar
  if (show_sidebar && is_click) {
    if (event->x < sidebar_width && event->y >= tab_height &&
        event->y < terminal.get_height() - status_height) {
      focus_state = FOCUS_SIDEBAR;
      needs_redraw = true;
      return;
    }
  }

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting && bstate != 3) {
    return;
  }

  // If clicked inside editor pane, focus editor
  if (inside_pane && is_click) {
    focus_state = FOCUS_EDITOR;
  }

  int rel_y = event->y - pane.y - 1;
  int rel_x = event->x - pane.x - 7; // Matching render offset

  if (rel_y < 0)
    rel_y = 0;
  if (rel_x < 0)
    rel_x = 0;

  int click_y = rel_y + buf.scroll_offset;
  int click_x = rel_x + buf.scroll_x;

  if (click_y < 0)
    click_y = 0;
  if (click_y >= (int)buf.lines.size())
    click_y = buf.lines.size() - 1;
  if (click_y < 0)
    return;

  if (click_x < 0)
    click_x = 0;
  int line_len = buf.lines[click_y].length();
  if (click_x > line_len) {
    click_x = line_len;
  }

  if (bstate == 3) {
    show_context_menu = true;
    context_menu_x = event->x;
    context_menu_y = event->y;
    context_menu_selected = 0;
    needs_redraw = true;
    return;
  }

  if (bstate == 1) {
    if (show_context_menu) {
      show_context_menu = false;
      needs_redraw = true;
    }

    // Ensure we are focused if we click
    focus_state = FOCUS_EDITOR;

    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    mouse_selecting = true;
    mouse_start = {click_x, click_y};
    buf.cursor.x = click_x;
    buf.cursor.y = click_y;
    buf.selection.start = mouse_start;
    buf.selection.end = {click_x, click_y};
    buf.selection.active = true;
    needs_redraw = true;
  } else if (bstate == 2) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    if (mouse_selecting) {
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.end = {click_x, click_y};
      mouse_selecting = false;
      needs_redraw = true;
    } else {
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.active = false;
      needs_redraw = true;
    }
  } else if (bstate == 32) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    if (mouse_selecting) {
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.end = {click_x, click_y};

      if (buf.cursor.y < buf.scroll_offset) {
        buf.scroll_offset = std::max(0, buf.cursor.y - 2);
      }
      if (buf.cursor.y >= buf.scroll_offset + pane.h - 2) {
        buf.scroll_offset = buf.cursor.y - pane.h + 3;
      }

      needs_redraw = true;
    }
  }

  clamp_cursor(pane.buffer_id);
}

void Editor::run() {
  while (running) {
    render();

    Event ev = terminal.poll_event();

    if (ev.type == EVENT_REDRAW) {
      needs_redraw = true;
    } else if (ev.type == EVENT_RESIZE) {
      ui->resize(ev.resize.width, ev.resize.height);
      update_pane_layout();
      needs_redraw = true;
    } else if (ev.type == EVENT_KEY) {
      int ch = ev.key.key;
      bool is_ctrl = ev.key.ctrl;
      bool is_shift = ev.key.shift;

      if (ch == '\n' || ch == 13) {
        is_ctrl = false;
      }

      // Store original ch for Python keybinds (before conversion)
      int original_ch = ch;

      if (is_ctrl && ch >= 1 && ch <= 26) {
        ch = ch + 96;
      }

      if (show_context_menu) {
        if (ch == 27) {
          show_context_menu = false;
          needs_redraw = true;
        }
      } else if (show_command_palette) {
        handle_command_palette(ch);
      } else if (show_search) {
        handle_search_panel(ch);
      } else if (telescope.is_active()) {
        handle_telescope(ch);
      } else {
        // Pass original_ch for Python keybind checking
        handle_input(ch, is_ctrl, is_shift, ev.key.alt, original_ch);
      }
    } else if (ev.type == EVENT_MOUSE) {
      if (show_context_menu) {
        if (ev.mouse.pressed || ev.mouse.released) {
          int menu_x = context_menu_x;
          int menu_y = context_menu_y;
          int menu_w = 20;
          int menu_h = 4;

          if (ev.mouse.x >= menu_x && ev.mouse.x < menu_x + menu_w &&
              ev.mouse.y >= menu_y && ev.mouse.y < menu_y + menu_h) {
            int item = ev.mouse.y - menu_y - 1;
            if (item >= 0 && item < 3) {
              if (ev.mouse.released) {
                if (item == 0) {
                  copy();
                } else if (item == 1) {
                  cut();
                } else if (item == 2) {
                  paste();
                }
                show_context_menu = false;
                needs_redraw = true;
              } else {
                context_menu_selected = item;
                needs_redraw = true;
              }
            } else {
              show_context_menu = false;
              needs_redraw = true;
            }
          } else {
            show_context_menu = false;
            needs_redraw = true;
          }
        }
      } else {
        int button = ev.mouse.button;
        bool is_wheel = (button >= 64 && button <= 67);

        if (is_wheel && !telescope.is_active() && !show_command_palette &&
            !show_search) {
          handle_mouse_input(ev.mouse.x, ev.mouse.y, false, button == 64,
                             button == 65);
        } else {
          MEVENT mevent;
          mevent.x = ev.mouse.x;
          mevent.y = ev.mouse.y;
          int bstate = 0;

          int button_code = ev.mouse.button & 0x03;
          bool is_motion = (ev.mouse.button & 0x20) != 0;

          if (is_motion) {
            bstate = 32;
          } else if (ev.mouse.pressed) {
            if (button_code == 0) {
              bstate = 1;
            } else if (button_code == 1 || button_code == 2) {
              bstate = 3;
            } else {
              bstate = 1;
            }
          } else if (ev.mouse.released) {
            if (button_code == 0) {
              bstate = 2;
            } else {
              bstate = 2;
            }
          }

          mevent.bstate = bstate;
          handle_mouse(&mevent);
        }
      }
    }
  }
  config.save();
}
