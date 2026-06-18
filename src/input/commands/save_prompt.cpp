#include "editor.h"

void Editor::handle_save_prompt(int ch) {
  if (ch == 27) {
    show_save_prompt = false;
    needs_redraw = true;
    set_message("Save cancelled");
  } else if (ch == '\n' || ch == 13) {
    show_save_prompt = false;
    if (!save_prompt_input.empty()) {
      get_buffer().filepath = save_prompt_input;
      save_file();
    } else {
      set_message("Save cancelled (empty filename)");
      needs_redraw = true;
    }
  } else if (ch == 127 || ch == 8) {
    if (!save_prompt_input.empty()) {
      save_prompt_input.pop_back();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    save_prompt_input += (char)ch;
    needs_redraw = true;
  }
}
