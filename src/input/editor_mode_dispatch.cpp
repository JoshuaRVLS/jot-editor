#include "editor.h"
#include "python_api.h"

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt,
                          int original_ch) {
  idle_frame_count = 0;
  cursor_visible = true;
  cursor_blink_frame = 0;

  // Terminals encode Ctrl+` inconsistently. Accept common variants:
  // - explicit Ctrl modifier + '`'/'~'
  // - control code 30 (Ctrl+^ / often emitted for Ctrl+`)
  // - keycode 0 with Ctrl flag from modifyOtherKeys paths
  bool ctrl_backtick = (is_ctrl && (ch == '`' || original_ch == '`' ||
                                    ch == '~' || original_ch == '~' || ch == 0 ||
                                    original_ch == 0)) ||
                       ch == 28 || original_ch == 28 || ch == 29 ||
                       original_ch == 29 || ch == 30 || original_ch == 30 ||
                       ch == 31 || original_ch == 31;
  if (ctrl_backtick) {
    toggle_integrated_terminal();
    return;
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
    if (is_ctrl || (ch >= 1 && ch <= 26) || ch == 23 || ch == 12) {
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
    handle_search_panel(ch);
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
