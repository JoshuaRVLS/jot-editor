#include "editor.h"

void Editor::toggle_command_palette()
{
  show_command_palette = !show_command_palette;
  if (show_command_palette)
  {
    // Re-open with the query left behind last time (if any), so a cancelled
    // search can be resumed instead of starting from scratch every time.
    open_command_palette(command_palette_last_query);
  }
  else
  {
    // Toggling closed counts as abandoning the search, like Esc: remember
    // what was in the box for the next open.
    command_palette_last_query = command_palette_query;
    command_palette_query.clear();
    command_palette_results.clear();
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    command_palette_selected = 0;
    needs_redraw = true;
  }
}

void Editor::open_command_palette(const std::string &query)
{
  show_command_palette = true;
  command_palette_query = query;
  command_palette_results.clear();
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  command_palette_theme_original.clear();
  refresh_command_palette();
  needs_redraw = true;
}

void Editor::open_theme_chooser()
{
  show_command_palette = true;
  command_palette_query = "theme ";
  command_palette_results.clear();
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  command_palette_theme_original.clear();
  refresh_command_palette();
  needs_redraw = true;
}
