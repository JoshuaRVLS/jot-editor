#include "editor.h"
#include <algorithm>
#include <chrono>

// Local definition of MEVENT used by handle_mouse()
struct MEVENT {
  int x, y;
  int bstate;
};

void Editor::run() {
  while (running) {
    const auto now = std::chrono::steady_clock::now();
    const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now.time_since_epoch())
                                 .count();

    if (auto_save_enabled && auto_save_interval_ms > 0 &&
        (last_auto_save_ms <= 0 ||
         now_ms - last_auto_save_ms >= auto_save_interval_ms)) {
      auto_save_modified_buffers();
      last_auto_save_ms = now_ms;
    }

    poll_lsp_clients();
    refresh_git_status(false);

    for (auto &term : integrated_terminals) {
      if (term && term->poll_output()) {
        needs_redraw = true;
      }
    }

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
      bool is_alt = ev.key.alt;

      int original_ch = ch;

      // Global integrated terminal toggle (Ctrl+`), handled early so it works
      // even while overlays like command palette/search are open.
      bool toggle_terminal_shortcut =
          (is_ctrl && (ch == '`' || ch == '~' || ch == '\\' || ch == '|')) ||
          (is_ctrl && (ch == 'x' || ch == 'X')) ||
          ch == 24 || original_ch == 24 ||
          ch == 28 || original_ch == 28 || ch == 30 || original_ch == 30;
      if (toggle_terminal_shortcut) {
        toggle_integrated_terminal();
        continue;
      }

      if (is_ctrl && ch >= 1 && ch <= 26) {
        ch = ch + 96;
      }

      if (show_command_palette) {
        handle_command_palette(ch);
      } else if (show_search) {
        handle_search_panel(ch, is_ctrl, is_shift, is_alt);
      } else if (telescope.is_active()) {
        handle_telescope(ch);
      } else {
        handle_input(ch, is_ctrl, is_shift, is_alt, original_ch);
      }
    } else if (ev.type == EVENT_MOUSE) {
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
  config.save();
}
