#include "editor.h"
#include "command_utils.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

using namespace CommandLineUtils;

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
  if (python_api) {
    auto py_suggestions = python_api->command_palette_suggestions(seed);
    if (!py_suggestions.empty()) {
      command_palette_results = std::move(py_suggestions);
      return;
    }
  }
  if (seed.empty()) {
    for (const auto &c : ex_commands()) {
      command_palette_results.push_back(c);
    }
    for (const auto &custom : custom_commands) {
      command_palette_results.push_back(custom.name);
    }
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
    for (const auto &theme : list_available_themes()) {
      if (arg.empty() || starts_with_icase(theme, arg)) {
        command_palette_results.push_back(theme);
      }
    }
  } else if (lcmd == "openrecent") {
    for (int i = 0; i < (int)recent_files.size() && i < 12; i++) {
      std::string idx = std::to_string(i + 1);
      if (arg.empty() || starts_with_icase(idx, arg)) {
        command_palette_results.push_back(idx);
      }

      std::string recent_name = get_filename(recent_files[i]);
      if (!recent_name.empty() &&
          (arg.empty() || starts_with_icase(recent_name, arg))) {
        command_palette_results.push_back(recent_name);
      }
    }
  } else if (lcmd == "autosave") {
    const std::vector<std::string> opts = {"on", "off", "toggle", "status",
                                           "250", "500", "1000", "2000",
                                           "5000", "10000"};
    for (const auto &opt : opts) {
      if (arg.empty() || starts_with_icase(opt, arg)) {
        command_palette_results.push_back(opt);
      }
    }
  } else if (lcmd == "gitdiff") {
    auto &buf = get_buffer();
    if (!buf.filepath.empty()) {
      std::string rel = to_git_relative_path(buf.filepath);
      if (!rel.empty() && (arg.empty() || starts_with_icase(rel, arg))) {
        command_palette_results.push_back(rel);
      }
    }
    auto paths = complete_path_argument(arg);
    command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                   paths.end());
  } else if (lcmd == "find" || lcmd == "ff") {
    auto paths = complete_path_argument(arg);
    command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                   paths.end());
  } else if (lcmd == "mkfile" || lcmd == "mkdir" || lcmd == "rm") {
    auto paths = complete_path_argument(arg);
    command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                   paths.end());
  } else if (lcmd == "rename") {
    size_t split = arg.find_first_of(" \t");
    if (split == std::string::npos) {
      auto paths = complete_path_argument(arg);
      command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                     paths.end());
    } else {
      std::string right = trim_copy(arg.substr(split + 1));
      auto paths = complete_path_argument(right);
      command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                     paths.end());
    }
  } else if (lcmd == "line" || lcmd == "goto") {
    auto &buf = get_buffer();
    int cur_line = std::max(1, buf.cursor.y + 1);
    int cur_col = std::max(1, buf.cursor.x + 1);
    int last_line = std::max(1, (int)buf.lines.size());
    std::vector<std::string> opts = {
        std::to_string(cur_line),
        std::to_string(cur_line) + ":" + std::to_string(cur_col),
        std::to_string(std::max(1, cur_line - 10)),
        std::to_string(std::min(last_line, cur_line + 10)),
        std::to_string(last_line)};
    for (const auto &opt : opts) {
      if (arg.empty() || starts_with_icase(opt, arg)) {
        command_palette_results.push_back(opt);
      }
    }
  } else if (lcmd == "help" || lcmd == "h") {
    for (const auto &c : ex_commands()) {
      if (arg.empty() || starts_with_icase(c, arg)) {
        command_palette_results.push_back(c);
      }
    }
    for (const auto &custom : custom_commands) {
      if (arg.empty() || starts_with_icase(custom.name, arg)) {
        command_palette_results.push_back(custom.name);
      }
    }
  } else if (lcmd == "e" || lcmd == "edit" || lcmd == "open" || lcmd == "w" ||
             lcmd == "write" || lcmd == "wq" || lcmd == "x" ||
             lcmd == "xit") {
    auto paths = complete_path_argument(arg);
    command_palette_results.insert(command_palette_results.end(), paths.begin(),
                                   paths.end());
  }

  std::sort(command_palette_results.begin(), command_palette_results.end(),
            [](const std::string &a, const std::string &b) {
              return to_lower_copy(a) < to_lower_copy(b);
            });
  command_palette_results.erase(
      std::unique(command_palette_results.begin(), command_palette_results.end()),
      command_palette_results.end());
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
    bool switched_to_argument_completion = false;
    if (completing_command) {
      next_body = chosen;
      if (command_takes_argument(chosen)) {
        next_body += " ";
        switched_to_argument_completion = true;
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
    }
  };

  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == 1008) { // Up
    if (!command_palette_results.empty()) {
      command_palette_selected =
          (command_palette_selected - 1 + (int)command_palette_results.size()) %
          (int)command_palette_results.size();
      needs_redraw = true;
    }
  } else if (ch == 1009) { // Down
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

    bool skip_python_dispatch = false;
    if (!line.empty()) {
      std::string probe_cmd;
      std::string probe_arg;
      std::istringstream pss(line);
      pss >> probe_cmd;
      std::getline(pss, probe_arg);
      const std::string probe_lcmd = to_lower_copy(probe_cmd);
      skip_python_dispatch = (probe_lcmd == "lspstart" ||
                              probe_lcmd == "lspstatus" ||
                              probe_lcmd == "lspstop" ||
                              probe_lcmd == "lsprestart" ||
                              probe_lcmd == "lspmanager" ||
                              probe_lcmd == "lspinstall" ||
                              probe_lcmd == "lspremove" ||
                              probe_lcmd == "help" ||
                              probe_lcmd == "h");
    }

    if (!line.empty() && python_api && !skip_python_dispatch) {
      bool handled_by_python = false;
      if (python_api->command_palette_execute(line, &handled_by_python) &&
          handled_by_python) {
        show_command_palette = false;
        command_palette_query.clear();
        command_palette_results.clear();
        reset_completion_state();
        needs_redraw = true;
        return;
      }
    }

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
    auto resolve_path = [&](const std::string &raw) -> fs::path {
      std::error_code ec;
      fs::path p(raw);
      if (p.is_relative()) {
        fs::path base = root_dir.empty() ? fs::current_path(ec) : fs::path(root_dir);
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
    auto starts_with_path = [&](const std::string &child, const std::string &parent) {
      if (child.size() < parent.size()) {
        return false;
      }
      if (child.compare(0, parent.size(), parent) != 0) {
        return false;
      }
      return child.size() == parent.size() ||
             child[parent.size()] == '/' || child[parent.size()] == '\\';
    };
    auto close_buffers_for_path = [&](const std::string &target_abs, bool is_dir) {
      std::error_code ec;
      const std::string norm_target = fs::path(target_abs).lexically_normal().string();
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
      } else if (dir == "down" || dir == "d" || dir == "bottom" ||
                 dir == "b") {
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
    } else if (lcmd == "minimap") {
      toggle_minimap();
    } else if (lcmd == "term" || lcmd == "terminal") {
      toggle_integrated_terminal();
    } else if (lcmd == "termnew" || lcmd == "terminalnew") {
      create_integrated_terminal();
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
        set_message("Usage: :lspinstall <python|typescript|cpp>");
      } else {
        install_lsp_server(arg);
      }
    } else if (lcmd == "lspremove") {
      if (arg.empty()) {
        set_message("Usage: :lspremove <python|typescript|cpp>");
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
          std::string diff =
              run_git_capture("diff -- " + shell_quote(target));
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
          std::string blame = run_git_capture("blame -L " +
                                              std::to_string(line_no) + "," +
                                              std::to_string(line_no) + " -- " +
                                              shell_quote(rel));
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
        set_message("Auto-save: " +
                    std::string(auto_save_enabled ? "ON" : "OFF") + " (" +
                    std::to_string(auto_save_interval_ms) + "ms)");
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
          set_message("Auto-save: " +
                      std::string(auto_save_enabled ? "ON" : "OFF") + " (" +
                      std::to_string(auto_save_interval_ms) + "ms)");
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
        set_message("Commands: :w :q :wq :e <file> :find [dir] :mkfile <p> :mkdir <p> :rename <old> <new> :rm <p> :line N[:C] :bd :sp [left|right|up|down] :vsp [left|right] :splitleft/:splitright/:splitup/:splitdown :bn :bp :recent :openrecent [n] :reopen :autosave [on/off/ms] :format :trim :trimblank :upper :lower :sortlines :sortdesc :reverselines :uniquelines :shufflelines :joinlines :dupe :copypath :copyname :datetime :stats :lspstart :lspstatus :lspstop :lsprestart :gitstatus :gitdiff [file] :gitblame :gitrefresh :theme <name>");
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
            "  Ctrl+X / Ctrl+`  Toggle integrated terminal",
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
            "                  :copypath :copyname :datetime :stats"};

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
      command_palette_selected =
          (command_palette_selected + 1) % (int)command_palette_results.size();
    }
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      reset_completion_state();
      command_palette_results.clear();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    reset_completion_state();
    command_palette_results.clear();
    needs_redraw = true;
  }
}
