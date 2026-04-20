#include "editor.h"
#include "python_api.h"
#include <sstream>

void Editor::show_popup(const std::string &text, int x, int y) {
  popup.text = text;
  popup.x = x;
  popup.y = y;

  int max_w = 0;
  int h = 0;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if ((int)line.length() > max_w)
      max_w = line.length();
    h++;
  }
  popup.w = max_w + 2;
  popup.h = h + 2;
  popup.visible = true;
  needs_redraw = true;
}

void Editor::hide_popup() {
  popup.visible = false;
  needs_redraw = true;
}

bool Editor::close_active_floating_ui() {
  if (popup.visible) {
    hide_popup();
    return true;
  }

  if (input_prompt_visible) {
    hide_input_prompt();
    return true;
  }

  if (show_context_menu) {
    show_context_menu = false;
    needs_redraw = true;
    return true;
  }

  if (show_command_palette) {
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    needs_redraw = true;
    return true;
  }

  if (show_search) {
    show_search = false;
    needs_redraw = true;
    return true;
  }

  if (show_save_prompt) {
    show_save_prompt = false;
    save_prompt_input.clear();
    needs_redraw = true;
    return true;
  }

  if (show_quit_prompt) {
    show_quit_prompt = false;
    needs_redraw = true;
    return true;
  }

  if (telescope.is_active()) {
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
    return true;
  }

  if (image_viewer.is_active()) {
    image_viewer.close();
    needs_redraw = true;
    return true;
  }

  if (lsp_completion_visible) {
    hide_lsp_completion();
    return true;
  }

  return false;
}

void Editor::run_python_script(const std::string &script) {
  if (python_api) {
    python_api->execute_code("import sys, os; sys.path.append(os.getcwd())");
    python_api->execute_code(script);
  }
}
