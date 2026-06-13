#include "commands/utils.h"
#include "editor.h"

#include <sstream>

using namespace CommandLineUtils;

void Editor::execute_command(const std::string &cmd) {
  std::string ex_line = trim_copy(cmd);
  bool explicit_ex = !ex_line.empty() && ex_line[0] == ':';
  if (explicit_ex) {
    ex_line.erase(0, 1);
    ex_line = trim_copy(ex_line);
  }
  std::string first_token;
  {
    std::istringstream iss(ex_line);
    iss >> first_token;
  }
  const std::string first_lc = to_lower_copy(first_token);
  if (!ex_line.empty() &&
      (explicit_ex || first_lc == "cppimpl" || first_lc == "cpppair")) {
    show_command_palette = true;
    command_palette_query = ex_line;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    handle_command_palette('\n');
    return;
  }

  if (cmd == "Choose Color Scheme") {
    open_theme_chooser();
    return;
  }

  if (cmd == "New File") {
    create_new_buffer();
  } else if (cmd == "Open File") {
    message = "Press Space+F to open file finder";
  } else if (cmd == "Save") {
    save_file();
  } else if (cmd == "Save As") {
    save_file_as();
  } else if (cmd == "Close File") {
    close_buffer();
  } else if (cmd == "Toggle Minimap") {
    toggle_minimap();
  } else if (cmd == "Toggle Search") {
    toggle_search();
  } else if (cmd == "Split Horizontal") {
    split_pane_horizontal();
  } else if (cmd == "Split Vertical") {
    split_pane_vertical();
  } else if (cmd == "Split Left") {
    split_pane_left();
  } else if (cmd == "Split Right") {
    split_pane_right();
  } else if (cmd == "Split Up") {
    split_pane_up();
  } else if (cmd == "Split Down") {
    split_pane_down();
  } else if (cmd == "Close Pane") {
    close_pane();
  } else if (cmd == "Next Pane") {
    next_pane();
  } else if (cmd == "Previous Pane") {
    prev_pane();
  } else if (cmd == "Focus Pane Left") {
    if (!focus_pane_direction('h')) {
      set_message("No pane to the left");
    }
  } else if (cmd == "Focus Pane Right") {
    if (!focus_pane_direction('l')) {
      set_message("No pane to the right");
    }
  } else if (cmd == "Focus Pane Up") {
    if (!focus_pane_direction('k')) {
      set_message("No pane above");
    }
  } else if (cmd == "Focus Pane Down") {
    if (!focus_pane_direction('j')) {
      set_message("No pane below");
    }
  } else if (cmd == "Jump to Bracket") {
    jump_to_matching_bracket();
  } else if (cmd == "Format Document") {
    format_document();
  } else if (cmd == "Duplicate Line") {
    duplicate_line();
  } else if (cmd == "Move Line Up") {
    move_line_up();
  } else if (cmd == "Move Line Down") {
    move_line_down();
  } else if (cmd == "Toggle Comment") {
    toggle_comment();
  } else if (cmd == "Toggle Bookmark") {
    toggle_bookmark();
  } else if (cmd == "Next Bookmark") {
    next_bookmark();
  } else if (cmd == "Previous Bookmark") {
    prev_bookmark();
  } else if (cmd == "Trim Trailing Whitespace") {
    trim_trailing_whitespace();
  } else if (cmd == "Toggle Auto Indent") {
    toggle_auto_indent_setting();
  } else if (cmd == "Increase Tab Size") {
    change_tab_size(1);
  } else if (cmd == "Decrease Tab Size") {
    change_tab_size(-1);
  } else if (cmd == "Toggle Case Search") {
    search_case_sensitive = !search_case_sensitive;
    message = search_case_sensitive ? "Search: case-sensitive ON"
                                    : "Search: case-sensitive OFF";
    if (!search_query.empty()) {
      perform_search();
    }
    needs_redraw = true;
  } else if (cmd == "Quit") {
    running = false;
  } else if (cmd.substr(0, 7) == "Theme: ") {
    apply_theme(cmd.substr(7));
  }
}
