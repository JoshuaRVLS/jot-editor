#include "editor.h"


// ---------------------------------------------------------------------------
// Theme / Color Scheme switching
// Terminal color numbers used:
//   0=Black  1=Red  2=Green  3=Yellow  4=Blue  5=Magenta  6=Cyan  7=White  8=Grey
// ---------------------------------------------------------------------------

void Editor::apply_theme(const std::string &name) {
  current_theme_name = name;
  Theme t; // start from default and override

  if (name == "Dark") {
    // Default dark theme — already the struct defaults, nothing to do
  }
  else if (name == "Gruvbox") {
    // Warm retro look: yellows/oranges/greens on dark
    t.fg_default     = 3;  // yellow text
    t.fg_keyword     = 1;  // red keywords
    t.fg_string      = 2;  // green strings
    t.fg_comment     = 8;  // grey comments
    t.fg_number      = 5;  // magenta numbers
    t.fg_function    = 3;  // yellow functions
    t.fg_type        = 4;  // blue types
    t.bg_selection   = 1;  // red selection
    t.fg_selection   = 7;
    t.fg_panel_border= 3;  // yellow border
    t.fg_status      = 3;
    t.fg_telescope_selected = 7;
    t.bg_telescope_selected = 1;
    t.fg_line_num    = 8;
    t.bg_status      = 0;
  }
  else if (name == "Dracula") {
    // Purple/pink/cyan sci-fi palette
    t.fg_default     = 7;  // white text
    t.fg_keyword     = 5;  // magenta keywords
    t.fg_string      = 2;  // green strings
    t.fg_comment     = 8;  // grey comments
    t.fg_number      = 3;  // yellow numbers
    t.fg_function    = 6;  // cyan functions
    t.fg_type        = 5;  // magenta types
    t.bg_default     = 0;
    t.bg_selection   = 5;  // magenta selection
    t.fg_selection   = 0;
    t.fg_panel_border= 5;  // magenta borders
    t.fg_status      = 7;
    t.fg_telescope_selected = 0;
    t.bg_telescope_selected = 5;
    t.fg_line_num    = 8;
    t.bg_status      = 0;
    t.fg_bracket1 = 5; t.fg_bracket2 = 6;
    t.fg_bracket3 = 2; t.fg_bracket4 = 3;
    t.fg_bracket5 = 4; t.fg_bracket6 = 1;
  }
  else if (name == "Nord") {
    // Cold blue arctic palette
    t.fg_default     = 6;  // cyan text
    t.fg_keyword     = 4;  // blue keywords
    t.fg_string      = 2;  // green strings
    t.fg_comment     = 8;  // grey comments
    t.fg_number      = 3;  // yellow numbers
    t.fg_function    = 6;  // cyan functions
    t.fg_type        = 4;  // blue types
    t.bg_default     = 0;
    t.bg_selection   = 4;  // blue selection
    t.fg_selection   = 7;
    t.fg_panel_border= 4;  // blue borders
    t.fg_status      = 6;
    t.fg_telescope_selected = 7;
    t.bg_telescope_selected = 4;
    t.fg_line_num    = 8;
    t.bg_status      = 0;
    t.fg_bracket1 = 4; t.fg_bracket2 = 6;
    t.fg_bracket3 = 2; t.fg_bracket4 = 5;
    t.fg_bracket5 = 3; t.fg_bracket6 = 1;
  }
  else if (name == "Solarized") {
    // Muted balanced palette inspired by Solarized Dark
    t.fg_default     = 6;  // cyan (base0)
    t.fg_keyword     = 2;  // green keywords
    t.fg_string      = 3;  // yellow strings
    t.fg_comment     = 8;  // grey comments
    t.fg_number      = 1;  // red numbers
    t.fg_function    = 4;  // blue functions
    t.fg_type        = 5;  // magenta types
    t.bg_default     = 0;
    t.bg_selection   = 8;  // grey selection
    t.fg_selection   = 7;
    t.fg_panel_border= 8;  // grey borders
    t.fg_status      = 6;
    t.fg_telescope_selected = 0;
    t.bg_telescope_selected = 6;
    t.fg_line_num    = 8;
    t.bg_status      = 0;
    t.fg_bracket1 = 2; t.fg_bracket2 = 3;
    t.fg_bracket3 = 4; t.fg_bracket4 = 5;
    t.fg_bracket5 = 1; t.fg_bracket6 = 6;
  }
  else if (name == "Monokai") {
    // Classic Sublime Text Monokai: green/yellow/pink/cyan on dark
    t.fg_default     = 7;  // white text
    t.fg_keyword     = 1;  // red/pink keywords
    t.fg_string      = 3;  // yellow strings
    t.fg_comment     = 8;  // grey comments
    t.fg_number      = 5;  // magenta numbers
    t.fg_function    = 2;  // green functions
    t.fg_type        = 6;  // cyan types
    t.bg_default     = 0;
    t.bg_selection   = 8;
    t.fg_selection   = 7;
    t.fg_panel_border= 8;
    t.fg_status      = 7;
    t.fg_telescope_selected = 0;
    t.bg_telescope_selected = 2;
    t.fg_line_num    = 8;
    t.bg_status      = 0;
    t.fg_bracket1 = 1; t.fg_bracket2 = 3;
    t.fg_bracket3 = 2; t.fg_bracket4 = 6;
    t.fg_bracket5 = 5; t.fg_bracket6 = 4;
  }
  else if (name == "Light") {
    // High-contrast light theme
    t.fg_default     = 0;  // black text
    t.bg_default     = 7;  // white background
    t.fg_keyword     = 4;  // blue keywords
    t.bg_keyword     = 7;
    t.fg_string      = 2;  // green strings
    t.bg_string      = 7;
    t.fg_comment     = 8;  // grey comments
    t.bg_comment     = 7;
    t.fg_number      = 1;  // red numbers
    t.bg_number      = 7;
    t.fg_function    = 4;  // blue functions
    t.bg_function    = 7;
    t.fg_type        = 5;  // magenta types
    t.bg_type        = 7;
    t.fg_panel_border= 8;
    t.bg_panel_border= 7;
    t.fg_selection   = 7;
    t.bg_selection   = 4;  // blue selection
    t.fg_line_num    = 8;
    t.bg_line_num    = 7;
    t.fg_status      = 7;
    t.bg_status      = 0;
    t.fg_command     = 0;
    t.bg_command     = 7;
    t.fg_minimap     = 8;
    t.bg_minimap     = 7;
    t.fg_telescope   = 0;
    t.bg_telescope   = 7;
    t.fg_telescope_selected = 7;
    t.bg_telescope_selected = 4;
    t.fg_telescope_preview  = 0;
    t.bg_telescope_preview  = 7;
  }

  theme = t;
  config.set("color_scheme", name);
  needs_redraw = true;
  set_message("Theme: " + name);
}
