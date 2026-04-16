#include "editor.h"
#include <algorithm>
#include <cctype>

void Editor::toggle_command_palette() {
  show_command_palette = !show_command_palette;
  if (show_command_palette) {
    command_palette_query = "";
    refresh_command_palette();
  }
}

void Editor::refresh_command_palette() {
  command_palette_results.clear();
  std::string query = command_palette_query;
  std::transform(query.begin(), query.end(), query.begin(), ::tolower);

  std::vector<std::string> commands = {
      "New File",           "Open File",         "Save",
      "Save As",            "Close File",        "Toggle Minimap",
      "Toggle Search",      "Split Horizontal",  "Split Vertical",
      "Next Pane",          "Previous Pane",     "Close Pane",
      "Jump to Bracket",    "Format Document",   "Duplicate Line",
      "Move Line Up",       "Move Line Down",    "Toggle Comment",
      "Toggle Bookmark",    "Next Bookmark",     "Previous Bookmark",
      "Trim Trailing Whitespace", "Toggle Auto Indent",
      "Increase Tab Size",  "Decrease Tab Size",
      "Toggle Case Search",
      "Quit",
      "Theme: Dark",        "Theme: Gruvbox",    "Theme: Dracula",
      "Theme: Nord",        "Theme: Solarized",  "Theme: Monokai",
      "Theme: Light"};

  for (const auto &cmd : commands) {
    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   ::tolower);
    if (query.empty() || cmd_lower.find(query) != std::string::npos) {
      command_palette_results.push_back(cmd);
    }
  }

  for (const auto &cmd : custom_commands) {
    std::string cmd_lower = cmd.name;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   ::tolower);
    if (query.empty() || cmd_lower.find(query) != std::string::npos) {
      command_palette_results.push_back(cmd.name);
    }
  }

  if (command_palette_selected >= (int)command_palette_results.size()) {
    command_palette_selected = 0;
  }
}

void Editor::execute_command(const std::string &cmd) {
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
  } else if (cmd == "Close Pane") {
    close_pane();
  } else if (cmd == "Next Pane") {
    next_pane();
  } else if (cmd == "Previous Pane") {
    prev_pane();
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

void Editor::handle_command_palette(int ch) {
  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    needs_redraw = true;
  } else if (ch == '\n' || ch == 13) {
    if (!command_palette_results.empty()) {
      execute_command(command_palette_results[command_palette_selected]);
      show_command_palette = false;
      command_palette_query.clear();
      needs_redraw = true;
    }
  } else if (ch == 1008 || ch == 'k') {
    if (command_palette_selected > 0)
      command_palette_selected--;
    needs_redraw = true;
  } else if (ch == 1009 || ch == 'j') {
    if (command_palette_selected < (int)command_palette_results.size() - 1)
      command_palette_selected++;
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      refresh_command_palette();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    refresh_command_palette();
    needs_redraw = true;
  }
}
