#include "editor.h"
#include <algorithm>

// Local definition of MEVENT used by handle_mouse()
struct MEVENT {
  int x, y;
  int bstate;
};

void Editor::run() {
  while (running) {
    render();

    int target_fps = needs_redraw ? render_fps : idle_fps;
    terminal.set_poll_timeout_ms(std::max(1, 1000 / target_fps));
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
      bool is_ctrl = ev.key.ctrl;
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
                if (item == 0)
                  copy();
                else if (item == 1)
                  cut();
                else if (item == 2)
                  paste();
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
            if (button_code == 0)
              bstate = 1;
            else if (button_code == 1 || button_code == 2)
              bstate = 3;
            else
              bstate = 1;
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
