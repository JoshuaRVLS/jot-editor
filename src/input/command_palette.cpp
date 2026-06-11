#include "command_utils.h"
#include "cpp_assist.h"
#include "editor.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace CommandLineUtils;

namespace {
struct CommandMeta {
  const char *name;
  const char *category;
  const char *detail;
  int priority;
};

const std::vector<CommandMeta> &command_palette_metadata() {
  static const std::vector<CommandMeta> meta = {
      {"w", "File", "Save current file", 100},
      {"write", "File", "Save current file", 90},
      {"q", "File", "Quit, prompting if buffers are unsaved", 100},
      {"quit", "File", "Quit, prompting if buffers are unsaved", 90},
      {"wq", "File", "Save and quit", 95},
      {"e", "File", "Open file path", 95},
      {"edit", "File", "Open file path", 90},
      {"open", "File", "Open file path", 85},
      {"new", "File", "Create a new buffer", 80},
      {"bd", "File", "Close current buffer", 85},
      {"find", "File", "Open file finder at path", 85},
      {"ff", "File", "Open file finder at path", 80},
      {"mkfile", "Workspace", "Create file in workspace", 80},
      {"mkdir", "Workspace", "Create folder in workspace", 80},
      {"cppimpl", "C++", "Generate missing method implementations", 82},
      {"cpppair", "C++", "Create matching C++ header/source files", 78},
      {"rename", "Workspace", "Rename file or folder", 80},
      {"rm", "Workspace", "Delete file or folder", 75},
      {"recent", "File", "Show recent files", 70},
      {"openrecent", "File", "Open recent file by number or name", 75},
      {"reopen", "File", "Reopen last closed tab", 70},
      {"split", "Pane", "Split pane right by default", 85},
      {"sp", "Pane", "Split pane right by default", 80},
      {"vsp", "Pane", "Vertical split", 80},
      {"splitleft", "Pane", "Split pane left", 75},
      {"splitright", "Pane", "Split pane right", 75},
      {"splitup", "Pane", "Split pane up", 75},
      {"splitdown", "Pane", "Split pane down", 75},
      {"wincmd", "Pane", "Focus pane by h/j/k/l direction", 80},
      {"focusleft", "Pane", "Focus pane left", 70},
      {"focusright", "Pane", "Focus pane right", 70},
      {"focusup", "Pane", "Focus pane up", 70},
      {"focusdown", "Pane", "Focus pane down", 70},
      {"resizeleft", "Pane", "Resize current pane left", 65},
      {"resizeright", "Pane", "Resize current pane right", 65},
      {"resizeup", "Pane", "Resize current pane up", 65},
      {"resizedown", "Pane", "Resize current pane down", 65},
      {"theme", "Appearance", "Apply color theme", 90},
      {"colorscheme", "Appearance", "Apply color theme", 80},
      {"minimap", "Appearance", "Toggle minimap", 70},
      {"term", "Terminal", "Open, focus, or hide terminal", 90},
      {"terminal", "Terminal", "Open, focus, or hide terminal", 80},
      {"termnew", "Terminal", "Create terminal tab", 85},
      {"task", "Terminal", "Run or list terminal tasks", 85},
      {"tasknew", "Terminal", "Run task in a fresh terminal tab", 80},
      {"taskrerun", "Terminal", "Rerun last task", 75},
      {"search", "Edit", "Open search panel", 75},
      {"format", "Edit", "Format document", 75},
      {"trim", "Edit", "Trim trailing whitespace", 75},
      {"upper", "Edit", "Uppercase selection or word", 65},
      {"lower", "Edit", "Lowercase selection or word", 65},
      {"sortlines", "Edit", "Sort selected lines", 65},
      {"sortdesc", "Edit", "Sort selected lines descending", 60},
      {"reverselines", "Edit", "Reverse selected lines", 60},
      {"uniquelines", "Edit", "Deduplicate selected lines", 60},
      {"shufflelines", "Edit", "Shuffle selected lines", 60},
      {"joinlines", "Edit", "Join selected/current lines", 60},
      {"dupe", "Edit", "Duplicate selection or current line", 65},
      {"trimblank", "Edit", "Trim blank lines in selection", 60},
      {"replace", "Edit", "Replace text case-sensitively", 65},
      {"replacei", "Edit", "Replace text case-insensitively", 60},
      {"replaceword", "Edit", "Replace whole words", 60},
      {"replacere", "Edit", "Replace by regex", 60},
      {"surround", "Edit", "Surround selection or word", 60},
      {"unsurround", "Edit", "Remove surrounding pair", 60},
      {"incnum", "Edit", "Increment number at cursor", 55},
      {"decnum", "Edit", "Decrement number at cursor", 55},
      {"copypath", "Clipboard", "Copy current file path", 60},
      {"copyname", "Clipboard", "Copy current file name", 60},
      {"datetime", "Insert", "Insert current date/time", 55},
      {"stats", "Info", "Show buffer statistics", 55},
      {"line", "Navigation", "Go to line[:column]", 80},
      {"goto", "Navigation", "Go to line[:column]", 75},
      {"lspstart", "LSP", "Start LSP for current file", 70},
      {"lspstatus", "LSP", "Show LSP status", 70},
      {"lspstop", "LSP", "Stop all LSP clients", 65},
      {"lsprestart", "LSP", "Restart all LSP clients", 65},
      {"lspinstall", "LSP", "Install LSP server", 65},
      {"lspremove", "LSP", "Remove LSP server", 65},
      {"lspmanager", "LSP", "Show LSP manager", 65},
      {"gitstatus", "Git", "Show git status", 75},
      {"gitdiff", "Git", "Show git diff for file", 75},
      {"gitblame", "Git", "Show blame for current line", 70},
      {"gitrefresh", "Git", "Refresh git status", 70},
      {"autosave", "Settings", "Configure auto-save", 70},
      {"help", "Help", "Show help or command list", 80},
      {"h", "Help", "Show help or command list", 70},
  };
  return meta;
}

const CommandMeta *find_command_meta(const std::string &name) {
  const std::string key = to_lower_copy(name);
  for (const auto &meta : command_palette_metadata()) {
    if (key == meta.name) {
      return &meta;
    }
  }
  return nullptr;
}

int fuzzy_score(const std::string &value, const std::string &query) {
  if (query.empty()) {
    return 10;
  }

  const std::string hay = to_lower_copy(value);
  const std::string needle = to_lower_copy(query);
  if (hay == needle) {
    return 10000;
  }
  if (starts_with_icase(value, query)) {
    return 8000 - (int)hay.size();
  }

  size_t pos = 0;
  int score = 4000;
  int streak = 0;
  for (char qc : needle) {
    bool found = false;
    for (; pos < hay.size(); pos++) {
      if (hay[pos] == qc) {
        score -= (int)pos;
        score += 20 + streak * 10;
        streak++;
        pos++;
        found = true;
        break;
      }
      streak = 0;
    }
    if (!found) {
      return -1;
    }
  }
  return score - (int)hay.size();
}

std::string strip_leading_colon(std::string s) {
  if (!s.empty() && s[0] == ':') {
    s.erase(s.begin());
  }
  return s;
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

void Editor::refresh_command_palette() {
  command_palette_results.clear();

  const std::string seed = command_palette_theme_mode
                               ? command_palette_theme_original
                               : command_palette_query;
  auto add_result = [&](const std::string &insert_text, std::string label,
                        std::string category, std::string detail, int score) {
    if (insert_text.empty()) {
      return;
    }
    if (label.empty()) {
      label = insert_text;
    }
    CommandPaletteSuggestion suggestion;
    suggestion.insert_text = insert_text;
    suggestion.label = std::move(label);
    suggestion.category = std::move(category);
    suggestion.detail = std::move(detail);
    suggestion.score = score;
    command_palette_results.push_back(std::move(suggestion));
  };
  auto complete_workspace_path = [&](const std::string &path_arg) {
    std::vector<std::string> out;
    std::error_code ec;

    std::string dir_part;
    std::string name_prefix = path_arg;
    size_t slash = path_arg.find_last_of("/\\");
    if (slash != std::string::npos) {
      dir_part = path_arg.substr(0, slash + 1);
      name_prefix = path_arg.substr(slash + 1);
    }

    fs::path base = root_dir.empty() ? fs::path(".") : fs::path(root_dir);
    fs::path list_dir = dir_part.empty() ? base : base / fs::path(dir_part);
    if (!fs::exists(list_dir, ec) || !fs::is_directory(list_dir, ec)) {
      return out;
    }

    for (const auto &entry : fs::directory_iterator(list_dir, ec)) {
      if (ec) {
        break;
      }
      std::string name = entry.path().filename().string();
      if (name.empty()) {
        continue;
      }
      if (!name_prefix.empty() && !starts_with_icase(name, name_prefix) &&
          fuzzy_score(name, name_prefix) < 0) {
        continue;
      }
      std::string suggestion = dir_part + name;
      if (entry.is_directory(ec)) {
        suggestion += "/";
      }
      out.push_back(suggestion);
      if (out.size() >= 64) {
        break;
      }
    }

    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) {
                return to_lower_copy(a) < to_lower_copy(b);
              });
    return out;
  };

  bool has_colon = !seed.empty() && seed[0] == ':';
  std::string body = has_colon ? seed.substr(1) : seed;
  std::string trimmed = trim_copy(body);

  std::string cmd;
  std::string arg;
  std::istringstream iss(trimmed);
  iss >> cmd;
  std::getline(iss, arg);
  arg = trim_copy(arg);

  if (cmd.empty() || (trimmed.find(' ') == std::string::npos &&
                      !(seed.size() > 0 && seed.back() == ' '))) {
    std::string needle = trim_copy(strip_leading_colon(seed));
    for (const auto &c : ex_commands()) {
      int score = fuzzy_score(c, needle);
      if (needle.empty() || score >= 0) {
        const CommandMeta *meta = find_command_meta(c);
        add_result(c, c, meta ? meta->category : "Command",
                   meta ? meta->detail : "Run command",
                   score + (meta ? meta->priority : 40));
      }
    }
  } else {
    const std::string lcmd = to_lower_copy(cmd);
    auto add_arg = [&](const std::string &value, const std::string &category,
                       const std::string &detail, int base_score = 100) {
      int score = fuzzy_score(value, arg);
      if (arg.empty() || score >= 0) {
        add_result(value, value, category, detail, score + base_score);
      }
    };
    if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo") {
      for (const auto &theme : list_available_themes()) {
        add_arg(theme, "Theme", "Apply color theme", 120);
      }
    } else if (lcmd == "openrecent") {
      for (int i = 0; i < (int)recent_files.size() && i < 12; i++) {
        std::string idx = std::to_string(i + 1);
        add_arg(idx, "Recent", get_filename(recent_files[i]), 130);

        std::string recent_name = get_filename(recent_files[i]);
        if (!recent_name.empty()) {
          add_arg(recent_name, "Recent", recent_files[i], 110);
        }
      }
    } else if (lcmd == "autosave") {
      const std::vector<std::string> opts = {"on",   "off",  "toggle",
                                             "status", "250", "500",
                                             "1000", "2000", "5000", "10000"};
      for (const auto &opt : opts) {
        add_arg(opt, "Auto-save", "Set auto-save mode or interval", 110);
      }
    } else if (lcmd == "wincmd") {
      const std::vector<std::string> opts = {"h", "j", "k", "l",
                                             "left", "down", "up", "right"};
      for (const auto &opt : opts) {
        add_arg(opt, "Pane", "Focus pane direction", 110);
      }
    } else if (lcmd == "task" || lcmd == "tasknew") {
      for (const auto &name : list_terminal_task_names()) {
        add_arg(name, "Task", "Run terminal task", 120);
      }
    } else if (lcmd == "lspinstall" || lcmd == "lspremove") {
      const std::vector<std::string> opts = {"python", "typescript", "cpp",
                                             "rust", "go", "lua", "bash"};
      for (const auto &opt : opts) {
        add_arg(opt, "LSP", "Language server", 110);
      }
    } else if (lcmd == "gitdiff") {
      auto &buf = get_buffer();
      if (!buf.filepath.empty()) {
        std::string rel = to_git_relative_path(buf.filepath);
        if (!rel.empty()) {
          add_arg(rel, "Git", "Current file", 130);
        }
      }
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Workspace path", 100);
      }
    } else if (lcmd == "find" || lcmd == "ff") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Open finder at path", 100);
      }
    } else if (lcmd == "mkfile" || lcmd == "mkdir" || lcmd == "rm" ||
               lcmd == "cppimpl" || lcmd == "cpppair") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "Workspace path", 100);
      }
    } else if (lcmd == "rename") {
      size_t split = arg.find_first_of(" \t");
      if (split == std::string::npos) {
        auto paths = complete_workspace_path(arg);
        for (const auto &path : paths) {
          add_arg(path, "Source", "Path to rename", 110);
        }
      } else {
        std::string right = trim_copy(arg.substr(split + 1));
        auto paths = complete_workspace_path(right);
        for (const auto &path : paths) {
          add_arg(path, "Destination", "New path or name", 100);
        }
      }
    } else if (lcmd == "line" || lcmd == "goto") {
      auto &buf = get_buffer();
      int cur_line = std::max(1, buf.cursor.y + 1);
      int cur_col = std::max(1, buf.cursor.x + 1);
      int last_line = std::max(1, (int)buf.line_count());
      std::vector<std::string> opts = {
          std::to_string(cur_line),
          std::to_string(cur_line) + ":" + std::to_string(cur_col),
          std::to_string(std::max(1, cur_line - 10)),
          std::to_string(std::min(last_line, cur_line + 10)),
          std::to_string(last_line)};
      for (const auto &opt : opts) {
        add_arg(opt, "Line", "Jump target", 120);
      }
    } else if (lcmd == "help" || lcmd == "h") {
      const std::vector<std::string> topics = {"commands", "cmd", "ex", "keys"};
      for (const auto &topic : topics) {
        add_arg(topic, "Help", "Help topic", 120);
      }
      for (const auto &c : ex_commands()) {
        add_arg(c, "Command", "Command help", 80);
      }
    } else if (lcmd == "e" || lcmd == "edit" || lcmd == "open" ||
               lcmd == "w" || lcmd == "write" || lcmd == "wq" ||
               lcmd == "x" || lcmd == "xit") {
      auto paths = complete_workspace_path(arg);
      for (const auto &path : paths) {
        add_arg(path, "Path", "File path", 110);
      }
    }
  }

  std::sort(command_palette_results.begin(), command_palette_results.end(),
            [](const CommandPaletteSuggestion &a,
               const CommandPaletteSuggestion &b) {
              if (a.score != b.score) {
                return a.score > b.score;
              }
              return to_lower_copy(a.label) < to_lower_copy(b.label);
            });

  std::set<std::string> seen;
  std::vector<CommandPaletteSuggestion> unique;
  for (auto &result : command_palette_results) {
    std::string key = to_lower_copy(result.insert_text);
    if (seen.insert(key).second) {
      unique.push_back(std::move(result));
    }
    if (unique.size() >= 128) {
      break;
    }
  }
  command_palette_results = std::move(unique);
  if (command_palette_selected >= (int)command_palette_results.size()) {
    command_palette_selected = 0;
  }
  if (command_palette_selected < 0) {
    command_palette_selected = 0;
  }
}

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

void Editor::handle_command_palette(int ch) {
  auto parse_quoted_tokens = [](const std::string &text) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;
    char quote_char = '\0';
    bool escape = false;
    for (char c : text) {
      if (escape) {
        current.push_back(c);
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (in_quote) {
        if (c == quote_char) {
          in_quote = false;
        } else {
          current.push_back(c);
        }
        continue;
      }
      if (c == '"' || c == '\'') {
        in_quote = true;
        quote_char = c;
        continue;
      }
      if (std::isspace((unsigned char)c)) {
        if (!current.empty()) {
          tokens.push_back(current);
          current.clear();
        }
        continue;
      }
      current.push_back(c);
    }
    if (!current.empty()) {
      tokens.push_back(current);
    }
    return tokens;
  };

  auto reset_completion_state = [&]() {
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
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
    size_t body_start = body.find_first_not_of(" \t");
    std::string parse_body =
        body_start == std::string::npos ? "" : body.substr(body_start);

    std::string cmd;
    std::string arg;
    std::string raw_arg;
    std::istringstream iss(parse_body);
    iss >> cmd;
    std::getline(iss, raw_arg);
    arg = trim_copy(raw_arg);

    const std::string chosen =
        command_palette_results[command_palette_selected].insert_text;
    const size_t cmd_pos = parse_body.find(cmd);
    const size_t after_cmd =
        cmd_pos == std::string::npos ? std::string::npos : cmd_pos + cmd.size();
    const bool has_argument_position =
        after_cmd != std::string::npos && after_cmd < parse_body.size() &&
        (parse_body[after_cmd] == ' ' || parse_body[after_cmd] == '\t');
    const bool completing_command = !has_argument_position;

    std::string next_body;
    bool switched_to_argument_completion = false;
    if (completing_command) {
      next_body = chosen;
      if (command_takes_argument(chosen)) {
        next_body += " ";
        switched_to_argument_completion = true;
      }
    } else if (to_lower_copy(cmd) == "rename") {
      std::string rename_args = raw_arg;
      size_t first_non_space = rename_args.find_first_not_of(" \t");
      if (first_non_space == std::string::npos) {
        rename_args.clear();
      } else {
        rename_args.erase(0, first_non_space);
      }
      size_t split = rename_args.find_first_of(" \t");
      if (split == std::string::npos) {
        next_body = cmd + " " + chosen;
      } else {
        std::string left = trim_copy(rename_args.substr(0, split));
        next_body = cmd + " " + left + " " + chosen;
      }
    } else {
      next_body = cmd + " " + chosen;
    }

    command_palette_query = has_colon ? ":" + next_body : next_body;

    // If we just completed a command that expects an argument, the next Tab
    // should complete arguments from the new command context, not keep cycling
    // the old command-prefix candidate list.
    if (switched_to_argument_completion) {
      command_palette_theme_original = command_palette_query;
      command_palette_selected = 0;
    } else {
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
    }
    refresh_command_palette();
  };

  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == 1008) { // Up
    refresh_command_palette();
    if (!command_palette_results.empty()) {
      command_palette_selected =
          (command_palette_selected - 1 + (int)command_palette_results.size()) %
          (int)command_palette_results.size();
      needs_redraw = true;
    }
  } else if (ch == 1009) { // Down
    refresh_command_palette();
    if (!command_palette_results.empty()) {
      command_palette_selected =
          (command_palette_selected + 1) % (int)command_palette_results.size();
      needs_redraw = true;
    }
  } else if (ch == '\n' || ch == 13) {
    auto has_unsaved_buffers = [&]() {
      for (const auto &b : buffers) {
        if (b.modified)
          return true;
      }
      return false;
    };

    if (trim_copy(command_palette_query).empty() &&
        !command_palette_results.empty()) {
      command_palette_query =
          command_palette_results[std::clamp(command_palette_selected, 0,
                                             (int)command_palette_results.size() - 1)]
              .insert_text;
    }

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
      if (buf.line_count() == 0) {
        return;
      }
      buf.cursor.y = std::clamp(line_1based - 1, 0, (int)buf.line_count() - 1);
      int line_len = (int)buf.line(buf.cursor.y).length();
      buf.cursor.x = std::clamp(col_1based - 1, 0, line_len);
      clear_selection();
      ensure_cursor_visible();
      set_message("Jumped to line " + std::to_string(buf.cursor.y + 1) +
                  ", col " + std::to_string(buf.cursor.x + 1));
    };
    auto resolve_path = [&](const std::string &raw) -> fs::path {
      std::error_code ec;
      fs::path p(raw);
      if (p.is_relative()) {
        fs::path base =
            root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
        if (ec) {
          ec.clear();
          base = fs::path(".");
        }
        p = base / p;
      }
      p = fs::absolute(p, ec);
      if (ec) {
        return fs::path(raw);
      }
      return p.lexically_normal();
    };
    auto find_existing_header_for_source = [&](const fs::path &source) {
      fs::path direct = CppAssist::counterpart_path_for(source);
      std::error_code ec;
      if (fs::exists(direct, ec)) {
        return direct;
      }
      for (const char *ext : {".hpp", ".h", ".hh", ".hxx"}) {
        fs::path candidate = source;
        candidate.replace_extension(ext);
        ec.clear();
        if (fs::exists(candidate, ec)) {
          return candidate;
        }
      }
      return direct;
    };
    auto starts_with_path = [&](const std::string &child,
                                const std::string &parent) {
      if (child.size() < parent.size()) {
        return false;
      }
      if (child.compare(0, parent.size(), parent) != 0) {
        return false;
      }
      return child.size() == parent.size() || child[parent.size()] == '/' ||
             child[parent.size()] == '\\';
    };
    auto close_buffers_for_path = [&](const std::string &target_abs,
                                      bool is_dir) {
      std::error_code ec;
      const std::string norm_target =
          fs::path(target_abs).lexically_normal().string();
      const std::string dir_prefix =
          norm_target + std::string(1, fs::path::preferred_separator);
      for (int i = (int)buffers.size() - 1; i >= 0; --i) {
        if (buffers[i].filepath.empty()) {
          continue;
        }
        fs::path bp = fs::absolute(buffers[i].filepath, ec);
        if (ec) {
          ec.clear();
          continue;
        }
        std::string buf_path = bp.lexically_normal().string();
        bool match = (!is_dir && buf_path == norm_target) ||
                     (is_dir && starts_with_path(buf_path, dir_prefix));
        if (match) {
          close_buffer_at(i);
        }
      }
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
      std::string dir = to_lower_copy(trim_copy(arg));
      if (dir == "left" || dir == "l") {
        split_pane_left();
      } else if (dir == "right" || dir == "r" || dir.empty()) {
        split_pane_right();
      } else if (dir == "up" || dir == "u" || dir == "top" || dir == "t") {
        split_pane_up();
      } else if (dir == "down" || dir == "d" || dir == "bottom" || dir == "b") {
        split_pane_down();
      } else {
        set_message("Usage: :split [left|right|up|down]");
      }
    } else if (lcmd == "vsp" || lcmd == "splitv") {
      std::string dir = to_lower_copy(trim_copy(arg));
      if (dir == "left" || dir == "l") {
        split_pane_left();
      } else {
        split_pane_right();
      }
    } else if (lcmd == "splitleft" || lcmd == "spleft") {
      split_pane_left();
    } else if (lcmd == "splitright" || lcmd == "spright") {
      split_pane_right();
    } else if (lcmd == "splitup" || lcmd == "spup") {
      split_pane_up();
    } else if (lcmd == "splitdown" || lcmd == "spdown") {
      split_pane_down();
    } else if (lcmd == "bn" || lcmd == "nextpane") {
      next_pane();
    } else if (lcmd == "bp" || lcmd == "prevpane") {
      prev_pane();
    } else if (lcmd == "focusleft") {
      if (!focus_pane_direction('h')) {
        set_message("No pane to the left");
      }
    } else if (lcmd == "focusright") {
      if (!focus_pane_direction('l')) {
        set_message("No pane to the right");
      }
    } else if (lcmd == "focusup") {
      if (!focus_pane_direction('k')) {
        set_message("No pane above");
      }
    } else if (lcmd == "focusdown") {
      if (!focus_pane_direction('j')) {
        set_message("No pane below");
      }
    } else if (lcmd == "wincmd") {
      std::string dir = to_lower_copy(trim_copy(arg));
      char focus_dir = '\0';
      if (dir == "h" || dir == "left") {
        focus_dir = 'h';
      } else if (dir == "j" || dir == "down") {
        focus_dir = 'j';
      } else if (dir == "k" || dir == "up") {
        focus_dir = 'k';
      } else if (dir == "l" || dir == "right") {
        focus_dir = 'l';
      }
      if (focus_dir == '\0') {
        set_message("Usage: :wincmd h|j|k|l");
      } else if (!focus_pane_direction(focus_dir)) {
        set_message("No pane in that direction");
      }
    } else if (lcmd == "minimap") {
      toggle_minimap();
    } else if (lcmd == "term" || lcmd == "terminal") {
      toggle_integrated_terminal();
    } else if (lcmd == "termnew" || lcmd == "terminalnew") {
      create_integrated_terminal();
    } else if (lcmd == "task") {
      if (arg.empty()) {
        show_terminal_tasks();
      } else {
        run_terminal_task(arg, false);
      }
    } else if (lcmd == "tasknew") {
      if (arg.empty()) {
        set_message("Usage: :tasknew <name>");
      } else {
        run_terminal_task(arg, true);
      }
    } else if (lcmd == "taskrerun") {
      rerun_last_terminal_task();
    } else if (lcmd == "find" || lcmd == "ff") {
      std::string target = trim_copy(arg);
      if (target.empty()) {
        target = root_dir.empty() ? "." : root_dir;
      }
      telescope.open(target);
      waiting_for_space_f = false;
      close_prompt = false;
      show_command_palette = false;
      command_palette_query.clear();
      command_palette_results.clear();
      reset_completion_state();
      needs_redraw = true;
      return;
    } else if (lcmd == "mkfile") {
      if (arg.empty()) {
        set_message("Usage: :mkfile <path>");
      } else {
        std::error_code ec;
        fs::path p = resolve_path(arg);
        if (fs::exists(p, ec)) {
          set_message("File already exists: " + p.string());
        } else {
          fs::create_directories(p.parent_path(), ec);
          ec.clear();
          std::ofstream out(p.string());
          if (!out.is_open()) {
            set_message("Failed to create file: " + p.string());
          } else {
            out.close();
            open_file(p.string());
            if (show_sidebar) {
              load_file_tree(root_dir);
            }
            set_message("Created file: " + p.filename().string());
          }
        }
      }
    } else if (lcmd == "cpppair") {
      if (arg.empty()) {
        set_message("Usage: :cpppair <path>");
      } else {
        std::error_code ec;
        fs::path first = resolve_path(arg);
        fs::path header;
        fs::path source;
        if (CppAssist::is_header_path(first)) {
          header = first;
          source = CppAssist::counterpart_path_for(first);
        } else if (CppAssist::is_source_path(first)) {
          source = first;
          header = CppAssist::counterpart_path_for(first);
        } else {
          header = first;
          header.replace_extension(".hpp");
          source = first;
          source.replace_extension(".cpp");
        }
        if (!CppAssist::is_header_path(header) || !CppAssist::is_source_path(source)) {
          set_message("cpppair expects a C++ header or source path");
        } else {
          bool created_any = false;
          fs::create_directories(header.parent_path(), ec);
          fs::create_directories(source.parent_path(), ec);
          if (!fs::exists(header, ec)) {
            std::ofstream out(header.string());
            if (out.is_open()) {
              out << CppAssist::header_skeleton(header);
              created_any = true;
            }
          }
          ec.clear();
          if (!fs::exists(source, ec)) {
            std::ofstream out(source.string());
            if (out.is_open()) {
              out << CppAssist::source_skeleton(header);
              created_any = true;
            }
          }
          if (show_sidebar) {
            load_file_tree(root_dir);
          }
          open_file(header.string());
          set_message(created_any ? "Created C++ pair" : "C++ pair already exists");
        }
      }
    } else if (lcmd == "cppimpl") {
      std::error_code ec;
      fs::path target = arg.empty() ? fs::path(get_buffer().filepath) : resolve_path(arg);
      if (target.empty()) {
        set_message("Usage: :cppimpl [header-or-source]");
      } else {
        fs::path header = CppAssist::is_header_path(target)
                              ? target
                              : find_existing_header_for_source(target);
        fs::path source = CppAssist::is_source_path(target)
                              ? target
                              : CppAssist::counterpart_path_for(header);
        if (!CppAssist::is_header_path(header) || !CppAssist::is_source_path(source)) {
          set_message("cppimpl expects a C++ header or source file");
        } else if (!fs::exists(header, ec)) {
          set_message("Header not found: " + header.string());
        } else {
          std::ifstream hin(header.string());
          std::stringstream hbuf;
          hbuf << hin.rdbuf();
          std::string source_text;
          bool source_exists = fs::exists(source, ec);
          if (source_exists) {
            std::ifstream sin(source.string());
            std::stringstream sbuf;
            sbuf << sin.rdbuf();
            source_text = sbuf.str();
          }
          auto result = CppAssist::generate_missing_implementations(
              hbuf.str(), source_text, header, source, source_exists);
          if (result.generated_count == 0 && source_exists) {
            set_message("No missing implementations");
          } else {
            fs::create_directories(source.parent_path(), ec);
            std::ofstream out(source.string(), std::ios::trunc);
            if (!out.is_open()) {
              set_message("Failed to write source: " + source.string());
            } else {
              out << result.source_text;
              out.close();
              if (show_sidebar) {
                load_file_tree(root_dir);
              }
              open_file(source.string());
              set_message("Generated " + std::to_string(result.generated_count) +
                          " implementation(s)");
            }
          }
        }
      }
    } else if (lcmd == "mkdir") {
      if (arg.empty()) {
        set_message("Usage: :mkdir <path>");
      } else {
        std::error_code ec;
        fs::path p = resolve_path(arg);
        if (fs::exists(p, ec)) {
          set_message("Path already exists: " + p.string());
        } else if (fs::create_directories(p, ec)) {
          if (show_sidebar) {
            load_file_tree(root_dir);
          }
          set_message("Created folder: " + p.filename().string());
        } else {
          set_message("Failed to create folder: " + p.string());
        }
      }
    } else if (lcmd == "rename") {
      std::istringstream riss(arg);
      std::string from_raw;
      std::string to_raw;
      riss >> from_raw >> to_raw;
      if (from_raw.empty() || to_raw.empty()) {
        set_message("Usage: :rename <old_path> <new_path>");
      } else {
        std::error_code ec;
        fs::path from = resolve_path(from_raw);
        fs::path to = resolve_path(to_raw);
        if (!fs::path(to_raw).has_parent_path()) {
          to = from.parent_path() / fs::path(to_raw);
        }
        to = to.lexically_normal();

        if (!fs::exists(from, ec)) {
          set_message("Source not found: " + from.string());
        } else if (fs::exists(to, ec)) {
          set_message("Destination exists: " + to.string());
        } else {
          fs::create_directories(to.parent_path(), ec);
          ec.clear();
          fs::rename(from, to, ec);
          if (ec) {
            set_message("Rename failed: " + ec.message());
          } else {
            const std::string from_s = from.lexically_normal().string();
            const std::string to_s = to.lexically_normal().string();
            for (auto &b : buffers) {
              if (b.filepath.empty()) {
                continue;
              }
              std::error_code bec;
              fs::path bp = fs::absolute(b.filepath, bec);
              if (bec) {
                continue;
              }
              if (bp.lexically_normal().string() == from_s) {
                b.filepath = to_s;
              }
            }
            if (show_sidebar) {
              load_file_tree(root_dir);
            }
            set_message("Renamed to: " + to.filename().string());
          }
        }
      }
    } else if (lcmd == "rm") {
      if (arg.empty()) {
        set_message("Usage: :rm <path>");
      } else {
        std::error_code ec;
        fs::path p = resolve_path(arg);
        if (!fs::exists(p, ec)) {
          set_message("Path not found: " + p.string());
        } else {
          bool is_dir = fs::is_directory(p, ec);
          close_buffers_for_path(p.string(), is_dir);
          if (is_dir) {
            fs::remove_all(p, ec);
          } else {
            fs::remove(p, ec);
          }
          if (ec) {
            set_message("Delete failed: " + ec.message());
          } else {
            if (show_sidebar) {
              load_file_tree(root_dir);
            }
            set_message("Deleted: " + p.filename().string());
          }
        }
      }
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
    } else if (lcmd == "lspmanager") {
      show_lsp_manager();
    } else if (lcmd == "lspinstall") {
      if (arg.empty()) {
        set_message("Usage: :lspinstall <python|typescript|cpp|rust|go|lua|bash>");
      } else {
        install_lsp_server(arg);
      }
    } else if (lcmd == "lspremove") {
      if (arg.empty()) {
        set_message("Usage: :lspremove <python|typescript|cpp|rust|go|lua|bash>");
      } else {
        remove_lsp_server(arg);
      }
    } else if (lcmd == "gitrefresh") {
      refresh_git_status(true);
      if (has_git_repo()) {
        set_message("Git refreshed: " + git_branch + " (" +
                    std::to_string(git_dirty_count) + " changes)");
      } else {
        set_message("Git: not a repository");
      }
    } else if (lcmd == "gitstatus") {
      refresh_git_status(true);
      if (!has_git_repo()) {
        set_message("Git: not a repository");
      } else {
        std::string status = run_git_capture("status --short --branch");
        if (trim_copy(status).empty()) {
          set_message("Git status unavailable");
        } else {
          show_popup(limit_lines(status, 18), 2, tab_height + 1);
        }
      }
    } else if (lcmd == "gitdiff") {
      refresh_git_status(true);
      if (!has_git_repo()) {
        set_message("Git: not a repository");
      } else {
        auto &buf = get_buffer();
        std::string target = trim_copy(arg);
        if (target.empty()) {
          if (buf.filepath.empty()) {
            set_message("Usage: :gitdiff [file]");
          } else {
            target = to_git_relative_path(buf.filepath);
          }
        }
        if (!target.empty()) {
          std::string diff = run_git_capture("diff -- " + shell_quote(target));
          if (trim_copy(diff).empty()) {
            set_message("Git diff: no unstaged changes for " + target);
          } else {
            show_popup(limit_lines(diff, 18), 2, tab_height + 1);
          }
        }
      }
    } else if (lcmd == "gitblame") {
      refresh_git_status(true);
      if (!has_git_repo()) {
        set_message("Git: not a repository");
      } else {
        auto &buf = get_buffer();
        if (buf.filepath.empty()) {
          set_message("Git blame requires a saved file");
        } else {
          int line_no = std::max(1, buf.cursor.y + 1);
          std::string rel = to_git_relative_path(buf.filepath);
          std::string blame = run_git_capture(
              "blame -L " + std::to_string(line_no) + "," +
              std::to_string(line_no) + " -- " + shell_quote(rel));
          if (trim_copy(blame).empty()) {
            set_message("Git blame unavailable");
          } else {
            set_message(first_line_copy(blame));
          }
        }
      }
    } else if (lcmd == "recent") {
      if (recent_files.empty()) {
        set_message("Recent files: none");
      } else {
        std::string list = "Recent: ";
        int shown = std::min(8, (int)recent_files.size());
        for (int i = 0; i < shown; i++) {
          if (i > 0) {
            list += " | ";
          }
          list += std::to_string(i + 1) + ":" + get_filename(recent_files[i]);
        }
        if ((int)recent_files.size() > shown) {
          list += " | ...";
        }
        set_message(list);
      }
    } else if (lcmd == "openrecent") {
      open_recent_file(arg);
    } else if (lcmd == "reopen" || lcmd == "reopenlast") {
      reopen_last_closed_buffer();
    } else if (lcmd == "autosave") {
      if (arg.empty() || to_lower_copy(arg) == "status") {
        set_message(
            "Auto-save: " + std::string(auto_save_enabled ? "ON" : "OFF") +
            " (" + std::to_string(auto_save_interval_ms) + "ms)");
      } else {
        std::string mode = to_lower_copy(arg);
        if (mode == "on" || mode == "true" || mode == "1") {
          set_auto_save(true);
          set_message("Auto-save enabled (" +
                      std::to_string(auto_save_interval_ms) + "ms)");
        } else if (mode == "off" || mode == "false" || mode == "0") {
          set_auto_save(false);
          set_message("Auto-save disabled");
        } else if (mode == "toggle") {
          set_auto_save(!auto_save_enabled);
          set_message(
              "Auto-save: " + std::string(auto_save_enabled ? "ON" : "OFF") +
              " (" + std::to_string(auto_save_interval_ms) + "ms)");
        } else {
          bool numeric = true;
          for (char c : mode) {
            if (!std::isdigit((unsigned char)c)) {
              numeric = false;
              break;
            }
          }
          if (numeric) {
            set_auto_save_interval(std::stoi(mode));
            set_message("Auto-save interval set to " +
                        std::to_string(auto_save_interval_ms) + "ms");
          } else {
            set_message("Usage: :autosave [on|off|toggle|status|<ms>]");
          }
        }
      }
    } else if (lcmd == "search") {
      toggle_search();
    } else if (lcmd == "format") {
      format_document();
    } else if (lcmd == "trim") {
      trim_trailing_whitespace();
    } else if (lcmd == "upper") {
      transform_selection_uppercase();
    } else if (lcmd == "lower") {
      transform_selection_lowercase();
    } else if (lcmd == "sortlines") {
      sort_selected_lines();
    } else if (lcmd == "sortdesc") {
      sort_selected_lines_desc();
    } else if (lcmd == "reverselines") {
      reverse_selected_lines();
    } else if (lcmd == "uniquelines") {
      unique_selected_lines();
    } else if (lcmd == "shufflelines") {
      shuffle_selected_lines();
    } else if (lcmd == "joinlines") {
      join_lines_selection_or_current();
    } else if (lcmd == "dupe") {
      duplicate_selection_or_line();
    } else if (lcmd == "trimblank") {
      trim_blank_lines_in_selection();
    } else if (lcmd == "copypath") {
      copy_current_file_path();
    } else if (lcmd == "copyname") {
      copy_current_file_name();
    } else if (lcmd == "datetime") {
      insert_current_datetime();
    } else if (lcmd == "stats") {
      show_buffer_stats();
    } else if (lcmd == "replace" || lcmd == "replacei" ||
               lcmd == "replaceword" || lcmd == "replacere") {
      auto tokens = parse_quoted_tokens(arg);
      if (tokens.size() < 2) {
        set_message("Usage: :" + lcmd + " <from> <to> (quote spaces)");
      } else if (lcmd == "replace") {
        replace_all_text(tokens[0], tokens[1], true, false);
      } else if (lcmd == "replacei") {
        replace_all_text(tokens[0], tokens[1], false, false);
      } else if (lcmd == "replaceword") {
        replace_all_text(tokens[0], tokens[1], true, true);
      } else {
        replace_all_regex(tokens[0], tokens[1]);
      }
    } else if (lcmd == "surround") {
      auto tokens = parse_quoted_tokens(arg);
      if (tokens.empty()) {
        set_message("Usage: :surround <left> [right]");
      } else if (tokens.size() == 1) {
        std::string right = tokens[0];
        if (tokens[0] == "(")
          right = ")";
        else if (tokens[0] == "[")
          right = "]";
        else if (tokens[0] == "{")
          right = "}";
        surround_selection_or_word(tokens[0], right);
      } else {
        surround_selection_or_word(tokens[0], tokens[1]);
      }
    } else if (lcmd == "unsurround") {
      unsurround_selection_or_cursor();
    } else if (lcmd == "incnum") {
      increment_number_at_cursor(1);
    } else if (lcmd == "decnum") {
      increment_number_at_cursor(-1);
    } else if (lcmd == "line" || lcmd == "goto") {
      if (arg.empty()) {
        set_message("Usage: :line <line>[:col]");
      } else if (parse_line_col(arg, parsed_line, parsed_col)) {
        goto_line_col(parsed_line, parsed_col);
      } else {
        set_message("Invalid location: " + arg);
      }
    } else if (lcmd == "resizeleft") {
      if (!resize_current_pane_direction('h', 2)) {
        set_message("Resize left unavailable");
      } else {
        set_message("Pane resized left");
      }
    } else if (lcmd == "resizeright") {
      if (!resize_current_pane_direction('l', 2)) {
        set_message("Resize right unavailable");
      } else {
        set_message("Pane resized right");
      }
    } else if (lcmd == "resizeup") {
      if (!resize_current_pane_direction('k', 2)) {
        set_message("Resize up unavailable");
      } else {
        set_message("Pane resized up");
      }
    } else if (lcmd == "resizedown") {
      if (!resize_current_pane_direction('j', 2)) {
        set_message("Resize down unavailable");
      } else {
        set_message("Pane resized down");
      }
    } else if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo") {
      const auto themes = list_available_themes();
      if (arg.empty()) {
        if (themes.empty()) {
          set_message("No themes found");
        } else {
          std::string list = "Themes: ";
          for (size_t i = 0; i < themes.size(); i++) {
            if (i > 0)
              list += ", ";
            list += themes[i];
          }
          set_message(list);
        }
      } else {
        std::string theme = trim_copy(arg);
        std::string resolved;
        const std::string needle = to_lower_copy(theme);
        for (const auto &candidate : themes) {
          if (to_lower_copy(candidate) == needle) {
            resolved = candidate;
            break;
          }
        }
        if (resolved.empty()) {
          set_message("Unknown theme: " + arg);
        } else {
          apply_theme(resolved);
        }
      }
    } else if (lcmd == "help" || lcmd == "h") {
      const std::string topic = to_lower_copy(trim_copy(arg));
      if (topic == "commands" || topic == "cmd" || topic == "ex") {
        set_message(
            "Commands: :w :q :wq :e <file> :find [dir] :mkfile <p> :mkdir <p> "
            ":rename <old> <new> :rm <p> :cpppair <p> :cppimpl [p] "
            ":line N[:C] :bd :sp "
            "[left|right|up|down] :vsp [left|right] "
            ":splitleft/:splitright/:splitup/:splitdown :bn :bp :recent "
            ":openrecent [n] :reopen :autosave [on/off/ms] :format :trim "
            ":trimblank :upper :lower :sortlines :sortdesc :reverselines "
            ":uniquelines :shufflelines :joinlines :dupe :copypath :copyname "
            ":datetime :stats :replace :replacei :replaceword :replacere "
            ":surround :unsurround :incnum :decnum :lspstart :lspstatus "
            ":lspstop :lsprestart :task [name] :tasknew <name> :taskrerun "
            ":gitstatus :gitdiff [file] :gitblame "
            ":gitrefresh :theme <name>");
      } else {
        std::vector<std::string> lines = {
            "Jot Keybind Help",
            "",
            "General",
            "  Ctrl+S           Save file",
            "  Ctrl+Q           Close pane (quit if single pane, with prompt)",
            "  Ctrl+P           Command palette",
            "  Ctrl+F           Search panel",
            "  Ctrl+B           Toggle file explorer",
            "  Ctrl+E           Telescope file finder",
            "  Ctrl+T           Theme chooser",
            "  Ctrl+M           Toggle minimap",
            "  Ctrl+X / Ctrl+`  Open/focus/minimize terminal",
            "  :task [name]     Run local/global terminal task",
            "",
            "Editing",
            "  Ctrl+Z / Ctrl+Y  Undo / Redo",
            "  Ctrl+A           Select all",
            "  Ctrl+C/X/V       Copy / Cut / Paste",
            "  Ctrl+D           Duplicate line",
            "  Ctrl+K           Delete line",
            "  Ctrl+/           Toggle comment",
            "  Ctrl+Backspace   Delete previous word",
            "  Ctrl+Shift+U     Uppercase selection/word",
            "  Ctrl+Shift+N     Lowercase selection/word",
            "  Ctrl+Shift+O     Sort selected lines",
            "  Shift+Arrows     Expand selection",
            "  Ctrl+Space       LSP completion",
            "",
            "Tabs",
            "  Ctrl+Tab          Next tab in current pane",
            "  Ctrl+Shift+Tab    Previous tab in current pane",
            "  Alt+, / Alt+.     Previous / Next tab",
            "  Alt+1..9 / Alt+0  Go to tab 1..9 / last tab",
            "",
            "Pane & Layout",
            "  Ctrl+Alt+H/J/K/L   Split left/down/up/right",
            "  Ctrl+Alt+Arrows    Focus pane",
            "  Ctrl+Alt+Q         Close current pane",
            "  Ctrl+Shift+H/J/K/L Resize pane",
            "  Ctrl+Arrow         Resize pane",
            "",
            "Power (Alt)",
            "  Alt+W              Close file tab",
            "  Alt+N              New buffer",
            "  Alt+S              Save",
            "  Alt+F              Search",
            "  Alt+P              Command palette",
            "  Alt+B              Toggle explorer",
            "  Alt+M              Toggle minimap",
            "  Alt+T              Theme chooser",
            "  Alt+U / Alt+N      Uppercase / Lowercase",
            "  Alt+O              Sort selected lines",
            "  Alt+Up / Alt+Down  Move line up/down",
            "  Alt+H / Alt+L      Move word left/right",
            "  Alt+I / Alt+A      Smart line start / line end",
            "  Alt+G / Alt+Shift+G File start / file end",
            "",
            "Tips",
            "  :help commands     Show ex-command summary",
            "  :help              Show this keybind help",
            "  Extra commands: :sortdesc :reverselines :uniquelines",
            "                  :shufflelines :joinlines :dupe :trimblank",
            "                  :copypath :copyname :datetime :stats",
            "                  :replace :replacei :replaceword :replacere",
            "                  :surround :unsurround :incnum :decnum"};

        std::string out;
        for (size_t i = 0; i < lines.size(); i++) {
          out += lines[i];
          if (i + 1 < lines.size()) {
            out.push_back('\n');
          }
        }
        show_popup(out, 2, tab_height + 1);
      }
    } else {
      set_message("Unknown command: " + line);
    }

    if (close_prompt) {
      show_command_palette = false;
      command_palette_query.clear();
    }
    command_palette_results.clear();
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
      command_palette_results.clear();
      reset_completion_state();
    } else {
      if (command_palette_selected >= (int)command_palette_results.size()) {
        command_palette_selected = 0;
      }
      apply_selected_completion();
      if (!command_palette_results.empty()) {
        command_palette_selected =
            (command_palette_selected + 1) % (int)command_palette_results.size();
      }
    }
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      reset_completion_state();
      refresh_command_palette();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    reset_completion_state();
    refresh_command_palette();
    needs_redraw = true;
  }
}
