#include "autoclose.h"
#include "editor.h"
#include "editor_features.h"
#include "python_api.h"
#include <sstream>

// Local definition of MEVENT used by handle_mouse()
struct MEVENT {
  int x, y;
  int bstate;
};

// ---------------------------------------------------------------------------
// Mouse input
// ---------------------------------------------------------------------------

void Editor::handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                                bool is_scroll_down) {
  if (show_sidebar) {
    if (x < sidebar_width) {
      if (is_scroll_up) {
        if (file_tree_scroll > 0) file_tree_scroll--;
        needs_redraw = true;
      } else if (is_scroll_down) {
        file_tree_scroll++;
        needs_redraw = true;
      } else if (is_click) {
        focus_state = FOCUS_SIDEBAR;
        handle_sidebar_mouse(x, y, is_click);
      }
      return;
    }
  }

  if (is_click) {
    focus_state = FOCUS_EDITOR;
  }

  auto &buf = get_buffer();
  auto &pane = get_pane();

  if (is_scroll_up) {
    if (buf.scroll_offset > 0) {
      buf.scroll_offset -= 3;
      if (buf.scroll_offset < 0) buf.scroll_offset = 0;
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

  // Check if click is in minimap
  if (show_minimap && is_click) {
    if (event->x >= pane.x + pane.w - minimap_width &&
        event->x < pane.x + pane.w) {
      if (event->y >= pane.y + tab_height && event->y < pane.y + pane.h) {
        int rel_y = event->y - (pane.y + tab_height);
        int h = pane.h - tab_height;
        int total_lines = buf.lines.size();
        if (total_lines > 0) {
          float ratio = (float)h / total_lines;
          if (ratio > 1.0f) ratio = 1.0f;
          int target_line = (int)(rel_y / ratio);
          buf.scroll_offset = target_line;
          if (buf.scroll_offset < 0) buf.scroll_offset = 0;
          if (buf.scroll_offset > (int)buf.lines.size() - 1)
            buf.scroll_offset = (int)buf.lines.size() - 1;
          needs_redraw = true;
          return;
        }
      }
    }
  }

  // Tab bar (top)
  if (is_click && event->y < tab_height) {
    int tab_x = show_sidebar ? sidebar_width : 0;
    for (int i = 0; i < (int)buffers.size(); i++) {
      std::string name = get_filename(buffers[i].filepath);
      if (name.empty()) name = "[No Name]";
      if (buffers[i].modified) name += "+";
      std::string disp = " " + name + " ";
      int w = disp.length() + 1;
      if (event->x >= tab_x && event->x < tab_x + w) {
        current_buffer = i;
        get_pane().buffer_id = current_buffer;
        needs_redraw = true;
        return;
      }
      tab_x += w;
      if (tab_x >= ui->get_width()) break;
    }
  }

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting && bstate != 3) return;

  if (inside_pane && is_click) focus_state = FOCUS_EDITOR;

  int rel_y = event->y - pane.y - 1;
  int rel_x = event->x - pane.x - 7;
  if (rel_y < 0) rel_y = 0;
  if (rel_x < 0) rel_x = 0;

  int click_y = rel_y + buf.scroll_offset;
  int click_x = rel_x + buf.scroll_x;
  if (click_y < 0) click_y = 0;
  if (click_y >= (int)buf.lines.size()) click_y = buf.lines.size() - 1;
  if (click_y < 0) return;
  if (click_x < 0) click_x = 0;
  int line_len = buf.lines[click_y].length();
  if (click_x > line_len) click_x = line_len;

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
    focus_state = FOCUS_EDITOR;
    // Mouse click enters Insert mode (feel free to adjust to Normal)
    if (mode == MODE_VISUAL) enter_normal_mode();

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
      if (buf.cursor.y < buf.scroll_offset)
        buf.scroll_offset = std::max(0, buf.cursor.y - 2);
      if (buf.cursor.y >= buf.scroll_offset + pane.h - 2)
        buf.scroll_offset = buf.cursor.y - pane.h + 3;
      needs_redraw = true;
    }
  }

  clamp_cursor(pane.buffer_id);
}

// ---------------------------------------------------------------------------
// Main event loop
// ---------------------------------------------------------------------------

void Editor::run() {
  while (running) {
    render();

    Event ev = terminal.poll_event();

    if (ev.type == EVENT_REDRAW) {
      // nothing
    } else if (ev.type == EVENT_RESIZE) {
      ui->invalidate();
      ui->resize(ev.resize.width, ev.resize.height);
      update_pane_layout();
      needs_redraw = true;
    } else if (ev.type == EVENT_KEY) {
      int ch = ev.key.key;
      bool is_ctrl  = ev.key.ctrl;
      bool is_shift = ev.key.shift;

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
                if (item == 0) copy();
                else if (item == 1) cut();
                else if (item == 2) paste();
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
          bool is_motion  = (ev.mouse.button & 0x20) != 0;

          if (is_motion) {
            bstate = 32;
          } else if (ev.mouse.pressed) {
            if (button_code == 0)      bstate = 1;
            else if (button_code == 1 || button_code == 2) bstate = 3;
            else bstate = 1;
          } else if (ev.mouse.released) {
            bstate = 2;
          }

          mevent.bstate = bstate;
          handle_mouse(&mevent);
        }
      }
    }
  }
  config.save();
}
