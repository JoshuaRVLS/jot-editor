#include "editor.h"
#include "python_api.h"
#include <cctype>

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt,
                          int original_ch) {
  idle_frame_count = 0;
  cursor_visible = true;
  cursor_blink_frame = 0;

  // Terminals encode Ctrl+` inconsistently. Accept common variants:
  // - explicit Ctrl modifier + '`'/'~' (and fallback Ctrl+\ for layouts where
  //   backtick is hard to emit)
  // - control code 30 (Ctrl+^ / sometimes emitted for Ctrl+`)
  bool ctrl_backtick =
      (is_ctrl && (ch == '`' || original_ch == '`' || ch == '~' ||
                   original_ch == '~' || ch == '\\' || original_ch == '\\' ||
                   ch == '|' || original_ch == '|')) ||
      ch == 28 || original_ch == 28 || ch == 30 || original_ch == 30;
  if (ctrl_backtick) {
    if (show_home_menu) {
      show_home_menu = false;
    }
    toggle_integrated_terminal();
    return;
  }

  // Fallback for terminals where Ctrl+` cannot be emitted reliably.
  bool alt_backtick = is_alt && (ch == '`' || original_ch == '`' ||
                                 ch == '\\' || original_ch == '\\' ||
                                 ch == '|' || original_ch == '|');
  if (alt_backtick) {
    if (show_home_menu) {
      show_home_menu = false;
    }
    toggle_integrated_terminal();
    return;
  }

  if (show_home_menu) {
    if (handle_home_menu_input(ch, is_ctrl, is_shift, is_alt)) {
      return;
    }
  }

  const bool ctrl_q =
      (is_ctrl && (ch == 'q' || ch == 'Q' || original_ch == 'q' ||
                   original_ch == 'Q')) ||
      ch == 17 || original_ch == 17;
  if (ctrl_q) {
    if (close_active_floating_ui()) {
      return;
    }
    if (panes.size() > 1) {
      close_pane();
    } else {
      bool unsaved = false;
      for (const auto &b : buffers) {
        if (b.modified) {
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
    }
    return;
  }

  // Global pane keybinds (before mode-specific handlers so they never get
  // swallowed by insert-mode Ctrl handling).
  // Resize:
  // - Ctrl+Shift+H/J/K/L
  // - Ctrl+Arrow
  if (is_ctrl &&
      (((is_shift || std::isupper((unsigned char)ch)) &&
        (ch == 'h' || ch == 'H' || ch == 'j' || ch == 'J' || ch == 'k' ||
         ch == 'K' || ch == 'l' || ch == 'L')) ||
       ch == 1008 || ch == 1009 || ch == 1010 || ch == 1011 ||
       original_ch == 1008 || original_ch == 1009 || original_ch == 1010 ||
       original_ch == 1011)) {
    bool resized = false;
    if (ch == 'h' || ch == 'H' || original_ch == 'h' || original_ch == 'H' ||
        ch == 1011 || original_ch == 1011) {
      resized = resize_current_pane_direction('h', 2);
    } else if (ch == 'j' || ch == 'J' || original_ch == 'j' ||
               original_ch == 'J' || ch == 1009 || original_ch == 1009) {
      resized = resize_current_pane_direction('j', 1);
    } else if (ch == 'k' || ch == 'K' || original_ch == 'k' ||
               original_ch == 'K' || ch == 1008 || original_ch == 1008) {
      resized = resize_current_pane_direction('k', 1);
    } else if (ch == 'l' || ch == 'L' || original_ch == 'l' ||
               original_ch == 'L' || ch == 1010 || original_ch == 1010) {
      resized = resize_current_pane_direction('l', 2);
    }

    if (resized) {
      set_message("Pane resized");
      return;
    }
    set_message("Resize unavailable in this direction");
    return;
  }

  // Split:
  // - Ctrl+Alt+H/J/K/L
  if (is_ctrl && is_alt) {
    if (ch == 'q' || ch == 'Q' || original_ch == 'q' || original_ch == 'Q') {
      if (panes.size() > 1) {
        close_pane();
      } else {
        bool unsaved = false;
        for (const auto &b : buffers) {
          if (b.modified) {
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
      }
      return;
    }
    if (ch == 'h' || ch == 'H' || original_ch == 'h' || original_ch == 'H') {
      split_pane_left();
      return;
    }
    if (ch == 'j' || ch == 'J' || original_ch == 'j' || original_ch == 'J') {
      split_pane_down();
      return;
    }
    if (ch == 'k' || ch == 'K' || original_ch == 'k' || original_ch == 'K') {
      split_pane_up();
      return;
    }
    if (ch == 'l' || ch == 'L' || original_ch == 'l' || original_ch == 'L') {
      split_pane_right();
      return;
    }
  }

  if (is_alt) {
    bool resized = false;
    if (ch == 'h' || ch == 'H' || original_ch == 'h' || original_ch == 'H') {
      resized = resize_current_pane_direction('h', 2);
    } else if (ch == 'l' || ch == 'L' || original_ch == 'l' ||
               original_ch == 'L') {
      resized = resize_current_pane_direction('l', 2);
    } else if (ch == 'k' || ch == 'K' || original_ch == 'k' ||
               original_ch == 'K') {
      resized = resize_current_pane_direction('k', 2);
    } else if (ch == 'j' || ch == 'J' || original_ch == 'j' ||
               original_ch == 'J') {
      resized = resize_current_pane_direction('j', 2);
    }

    if (resized) {
      set_message("Pane resized");
      return;
    }
  }

  IntegratedTerminal *active_terminal = get_integrated_terminal();
  if (show_integrated_terminal && active_terminal &&
      active_terminal->is_focused()) {
    handle_integrated_terminal_input(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  // Reserve Ctrl+S for save so plugin keybinds cannot swallow it.
  if ((is_ctrl && (ch == 's' || ch == 'S')) || ch == 19 || original_ch == 19) {
    save_file();
    needs_redraw = true;
    return;
  }

  // Save prompt input should always receive keystrokes first.
  if (show_save_prompt) {
    handle_save_prompt(ch);
    return;
  }

  if (python_api) {
    std::string mode_str = "all";
    if (python_api->handle_keybind(original_ch, is_ctrl, is_shift, is_alt,
                                   mode_str)) {
      needs_redraw = true;
      return;
    }
  }

  // Global sidebar toggle should work regardless of current focus.
  if (is_ctrl && (ch == 'b' || ch == 'B')) {
    toggle_sidebar();
    return;
  }

  if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
    if (ch == 27) {
      focus_state = FOCUS_EDITOR;
      needs_redraw = true;
      return;
    }
    // Keep global/editor shortcuts usable while explorer is focused.
    // Ctrl-based keybinds are routed through insert-mode handlers.
    bool ctrl_control_byte = (ch >= 1 && ch <= 26 && ch != 9 && ch != 10 &&
                              ch != 13);
    if (is_ctrl || ctrl_control_byte || ch == 23 || ch == 12) {
      handle_insert_mode(ch, is_ctrl, is_shift, is_alt);
      return;
    }
    handle_sidebar_input(ch);
    return;
  }

  if (ch == 23) {
    if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      focus_state = FOCUS_EDITOR;
    } else {
      close_pane();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 12) {
    focus_state = FOCUS_EDITOR;
    ui->invalidate();
    needs_redraw = true;
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

  if (show_search) {
    handle_search_panel(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  if (show_command_palette) {
    handle_command_palette(ch);
    return;
  }

  if (input_prompt_visible) {
    handle_input_prompt(ch);
    return;
  }

  if (image_viewer.is_active()) {
    if (ch == 'q' || ch == 27) {
      image_viewer.close();
      needs_redraw = true;
    }
    return;
  }

  // Modeless editor flow: always handle keys as direct editing commands.
  handle_insert_mode(ch, is_ctrl, is_shift, is_alt);
}
