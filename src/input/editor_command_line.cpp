#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
const std::vector<std::string> &theme_names() {
  static const std::vector<std::string> names = {
      "Dark", "Gruvbox", "Dracula", "Nord", "Solarized", "Monokai", "Light"};
  return names;
}

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

const std::vector<std::string> &ex_commands() {
  static const std::vector<std::string> commands = {
      "q",      "quit",     "q!",       "quit!",   "w",      "write",
      "wq",     "x",        "xit",      "e",       "edit",   "open",
      "new",    "enew",     "bd",       "bdelete", "close",  "sp",
      "split",  "splith",   "vsp",      "splitv",  "bn",     "nextpane",
      "bp",     "prevpane", "theme",    "colorscheme", "colo", "minimap",
      "term",   "terminal", "termnew",  "terminalnew", "search",
      "format", "trim",     "line",     "goto",        "resizeleft",
      "resizeright", "resizeup", "resizedown", "lspstart", "lspstatus",
      "lspstop", "lsprestart", "help"};
  return commands;
}

bool starts_with_icase(const std::string &value, const std::string &prefix) {
  if (prefix.size() > value.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (std::tolower((unsigned char)value[i]) !=
        std::tolower((unsigned char)prefix[i])) {
      return false;
    }
  }
  return true;
}

bool command_takes_argument(const std::string &cmd) {
  const std::string lc = to_lower_copy(cmd);
  return lc == "e" || lc == "edit" || lc == "open" || lc == "w" ||
         lc == "write" || lc == "theme" || lc == "colorscheme" ||
         lc == "colo" || lc == "line" || lc == "goto";
}

bool parse_line_col(const std::string &s, int &line_out, int &col_out) {
  if (s.empty())
    return false;

  size_t colon = s.find(':');
  std::string line_part = (colon == std::string::npos) ? s : s.substr(0, colon);
  std::string col_part =
      (colon == std::string::npos) ? "" : s.substr(colon + 1);

  if (line_part.empty())
    return false;
  for (char c : line_part) {
    if (!std::isdigit((unsigned char)c))
      return false;
  }
  if (!col_part.empty()) {
    for (char c : col_part) {
      if (!std::isdigit((unsigned char)c))
        return false;
    }
  }

  line_out = std::max(1, std::stoi(line_part));
  col_out = col_part.empty() ? 1 : std::max(1, std::stoi(col_part));
  return true;
}

std::string trim_copy(const std::string &s) {
  const size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  const size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

std::string normalize_theme_name(const std::string &name) {
  const std::string needle = to_lower_copy(trim_copy(name));
  for (const auto &theme : theme_names()) {
    if (to_lower_copy(theme) == needle) {
      return theme;
    }
  }
  return "";
}
} // namespace

void Editor::toggle_command_palette() {
  show_command_palette = !show_command_palette;
  if (show_command_palette) {
    command_palette_query = "";
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
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
  needs_redraw = true;
}

void Editor::refresh_command_palette() {
  command_palette_results.clear();

  const std::string seed = command_palette_theme_mode
                               ? command_palette_theme_original
                               : command_palette_query;
  if (seed.empty()) {
    return;
  }

  bool has_colon = !seed.empty() && seed[0] == ':';
  std::string body = has_colon ? seed.substr(1) : seed;
  std::string trimmed = trim_copy(body);

  std::string cmd;
  std::string arg;
  std::istringstream iss(trimmed);
  iss >> cmd;
  std::getline(iss, arg);
  arg = trim_copy(arg);

  if (cmd.empty()) {
    for (const auto &c : ex_commands()) {
      command_palette_results.push_back(c);
    }
    for (const auto &custom : custom_commands) {
      command_palette_results.push_back(custom.name);
    }
    return;
  }

  const bool completing_command =
      (trimmed.find(' ') == std::string::npos) ||
      (trimmed.back() != ' ' && arg.empty());

  if (completing_command) {
    for (const auto &c : ex_commands()) {
      if (starts_with_icase(c, cmd)) {
        command_palette_results.push_back(c);
      }
    }
    for (const auto &custom : custom_commands) {
      if (starts_with_icase(custom.name, cmd)) {
        command_palette_results.push_back(custom.name);
      }
    }
    return;
  }

  const std::string lcmd = to_lower_copy(cmd);
  if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo") {
    for (const auto &theme : theme_names()) {
      if (arg.empty() || starts_with_icase(theme, arg)) {
        command_palette_results.push_back(theme);
      }
    }
  }
}

void Editor::execute_command(const std::string &cmd) {
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
  auto reset_completion_state = [&]() {
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    command_palette_results.clear();
    command_palette_selected = 0;
  };

  auto apply_selected_completion = [&]() {
    if (command_palette_results.empty()) {
      return;
    }

    const std::string seed = command_palette_theme_mode
                                 ? command_palette_theme_original
                                 : command_palette_query;
    const bool has_colon = !seed.empty() && seed[0] == ':';
    std::string body = has_colon ? seed.substr(1) : seed;
    std::string trimmed = trim_copy(body);

    std::string cmd;
    std::string arg;
    std::istringstream iss(trimmed);
    iss >> cmd;
    std::getline(iss, arg);
    arg = trim_copy(arg);

    const std::string chosen = command_palette_results[command_palette_selected];
    const bool completing_command =
        (trimmed.find(' ') == std::string::npos) ||
        (trimmed.back() != ' ' && arg.empty());

    std::string next_body;
    if (completing_command) {
      next_body = chosen;
      if (command_takes_argument(chosen)) {
        next_body += " ";
      }
    } else {
      next_body = cmd + " " + chosen;
    }

    command_palette_query = has_colon ? ":" + next_body : next_body;
  };

  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == '\n' || ch == 13) {
    auto has_unsaved_buffers = [&]() {
      for (const auto &b : buffers) {
        if (b.modified)
          return true;
      }
      return false;
    };

    std::string line = trim_copy(command_palette_query);
    if (!line.empty() && line[0] == ':') {
      line.erase(0, 1);
    }

    std::string cmd;
    std::string arg;
    std::istringstream iss(line);
    iss >> cmd;
    std::getline(iss, arg);
    arg = trim_copy(arg);
    std::string lcmd = to_lower_copy(cmd);

    auto goto_line_col = [&](int line_1based, int col_1based) {
      auto &buf = get_buffer();
      if (buf.lines.empty()) {
        return;
      }
      buf.cursor.y = std::clamp(line_1based - 1, 0, (int)buf.lines.size() - 1);
      int line_len = (int)buf.lines[buf.cursor.y].length();
      buf.cursor.x = std::clamp(col_1based - 1, 0, line_len);
      clear_selection();
      ensure_cursor_visible();
      set_message("Jumped to line " + std::to_string(buf.cursor.y + 1) +
                  ", col " + std::to_string(buf.cursor.x + 1));
    };

    int parsed_line = 0, parsed_col = 1;
    bool close_prompt = true;
    if (lcmd.empty()) {
      // Nothing to execute, just close.
    } else if (parse_line_col(lcmd, parsed_line, parsed_col)) {
      goto_line_col(parsed_line, parsed_col);
    } else if (lcmd == "q" || lcmd == "quit") {
      if (has_unsaved_buffers()) {
        show_quit_prompt = true;
      } else {
        running = false;
      }
    } else if (lcmd == "q!" || lcmd == "quit!") {
      running = false;
    } else if (lcmd == "w" || lcmd == "write" || lcmd == "save") {
      if (!arg.empty()) {
        get_buffer().filepath = arg;
      }
      save_file();
    } else if (lcmd == "wq" || lcmd == "x" || lcmd == "xit") {
      if (!arg.empty()) {
        get_buffer().filepath = arg;
      }
      save_file();
      running = false;
    } else if (lcmd == "e" || lcmd == "edit" || lcmd == "open") {
      if (arg.empty()) {
        set_message("Usage: :e <file>");
      } else {
        open_file(arg);
      }
    } else if (lcmd == "new" || lcmd == "enew") {
      create_new_buffer();
    } else if (lcmd == "bd" || lcmd == "bdelete" || lcmd == "close") {
      close_buffer();
    } else if (lcmd == "sp" || lcmd == "split" || lcmd == "splith") {
      split_pane_horizontal();
    } else if (lcmd == "vsp" || lcmd == "splitv") {
      split_pane_vertical();
    } else if (lcmd == "bn" || lcmd == "nextpane") {
      next_pane();
    } else if (lcmd == "bp" || lcmd == "prevpane") {
      prev_pane();
    } else if (lcmd == "minimap") {
      toggle_minimap();
    } else if (lcmd == "term" || lcmd == "terminal") {
      toggle_integrated_terminal();
    } else if (lcmd == "termnew" || lcmd == "terminalnew") {
      create_integrated_terminal();
    } else if (lcmd == "lspstart") {
      auto &buf = get_buffer();
      if (buf.filepath.empty()) {
        set_message("LSP start requires a saved file");
      } else if (ensure_lsp_for_file(buf.filepath)) {
        notify_lsp_open(buf.filepath);
        set_message("LSP started for current file");
      } else {
        set_message("No LSP server configured for this file");
      }
    } else if (lcmd == "lspstatus") {
      show_lsp_status();
    } else if (lcmd == "lspstop") {
      stop_all_lsp_clients();
    } else if (lcmd == "lsprestart") {
      restart_all_lsp_clients();
    } else if (lcmd == "search") {
      toggle_search();
    } else if (lcmd == "format") {
      format_document();
    } else if (lcmd == "trim") {
      trim_trailing_whitespace();
    } else if (lcmd == "line" || lcmd == "goto") {
      if (arg.empty()) {
        set_message("Usage: :line <line>[:col]");
      } else if (parse_line_col(arg, parsed_line, parsed_col)) {
        goto_line_col(parsed_line, parsed_col);
      } else {
        set_message("Invalid location: " + arg);
      }
    } else if (lcmd == "resizeleft") {
      if (pane_layout_mode != PANE_LAYOUT_VERTICAL || !resize_current_pane(-2)) {
        set_message("Resize left unavailable for current layout");
      } else {
        set_message("Pane resized left");
      }
    } else if (lcmd == "resizeright") {
      if (pane_layout_mode != PANE_LAYOUT_VERTICAL || !resize_current_pane(2)) {
        set_message("Resize right unavailable for current layout");
      } else {
        set_message("Pane resized right");
      }
    } else if (lcmd == "resizeup") {
      if (pane_layout_mode != PANE_LAYOUT_HORIZONTAL || !resize_current_pane(-2)) {
        set_message("Resize up unavailable for current layout");
      } else {
        set_message("Pane resized up");
      }
    } else if (lcmd == "resizedown") {
      if (pane_layout_mode != PANE_LAYOUT_HORIZONTAL || !resize_current_pane(2)) {
        set_message("Resize down unavailable for current layout");
      } else {
        set_message("Pane resized down");
      }
    } else if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo") {
      if (arg.empty()) {
        std::string list = "Themes: ";
        const auto &themes = theme_names();
        for (size_t i = 0; i < themes.size(); i++) {
          if (i > 0)
            list += ", ";
          list += themes[i];
        }
        set_message(list);
      } else {
        std::string theme = normalize_theme_name(arg);
        if (theme.empty()) {
          set_message("Unknown theme: " + arg);
        } else {
          apply_theme(theme);
        }
      }
    } else if (lcmd == "help" || lcmd == "h") {
      set_message("Commands: :w :q :wq :e <file> :line N[:C] :bd :sp :vsp :bn :bp :resizeleft :resizeright :resizeup :resizedown :lspstart :lspstatus :lspstop :lsprestart :theme <name>");
    } else {
      bool handled_custom = false;
      for (const auto &custom : custom_commands) {
        if (to_lower_copy(custom.name) == lcmd) {
          handled_custom = true;
          if (python_api) {
            python_api->invoke_callback(custom.callback, arg);
          } else {
            set_message("Python runtime unavailable for command: " + custom.name);
          }
          break;
        }
      }
      if (!handled_custom) {
        set_message("Unknown command: " + line);
      }
    }

    if (close_prompt) {
      show_command_palette = false;
      command_palette_query.clear();
    }
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == '\t' || ch == 9) {
    if (!command_palette_theme_mode) {
      command_palette_theme_mode = true;
      command_palette_theme_original = command_palette_query;
      command_palette_selected = 0;
    }

    refresh_command_palette();
    if (command_palette_results.empty()) {
      set_message("No completion");
      reset_completion_state();
    } else {
      if (command_palette_selected >= (int)command_palette_results.size()) {
        command_palette_selected = 0;
      }
      apply_selected_completion();
      command_palette_selected =
          (command_palette_selected + 1) % (int)command_palette_results.size();
    }
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      reset_completion_state();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    reset_completion_state();
    needs_redraw = true;
  }
}
