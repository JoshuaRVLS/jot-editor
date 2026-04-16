#include "editor.h"
#include "python_api.h"

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt,
                          int original_ch) {
  idle_frame_count = 0;
  cursor_visible = true;
  cursor_blink_frame = 0;

  if (python_api) {
    std::string mode_str = (mode == MODE_INSERT) ? "insert" : "normal";
    if (python_api->handle_keybind(original_ch, is_ctrl, is_shift, is_alt,
                                   mode_str)) {
      needs_redraw = true;
      return;
    }
  }

  if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
    if (ch == 27) {
      focus_state = FOCUS_EDITOR;
      needs_redraw = true;
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

  if (show_save_prompt) {
    handle_save_prompt(ch);
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

  switch (mode) {
  case MODE_NORMAL:
    handle_normal_mode(ch, is_ctrl, is_shift, is_alt);
    break;
  case MODE_INSERT:
    handle_insert_mode(ch, is_ctrl, is_shift, is_alt);
    break;
  case MODE_VISUAL:
    handle_visual_mode(ch, is_ctrl, is_shift, is_alt);
    break;
  }
}
