#include "editor.h"

void Editor::toggle_command_palette() {
  show_command_palette = !show_command_palette;
  if (show_command_palette) {
    command_palette_query = "";
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
  }
}

void Editor::open_theme_chooser() {
  show_command_palette = true;
  command_palette_query = "theme ";
  command_palette_results.clear();
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  command_palette_theme_original.clear();
  refresh_command_palette();
  needs_redraw = true;
}
