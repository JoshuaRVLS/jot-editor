#include "command_utils.h"
#include "cpp_assist.h"
#include "editor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace CommandLineUtils;

namespace {
namespace fs = std::filesystem;

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
} // namespace

bool Editor::execute_ex_command(const std::string &input_line) {
  std::string line = trim_copy(input_line);
  if (!line.empty() && line[0] == ':') {
    line.erase(0, 1);
    line = trim_copy(line);
  }

  auto has_unsaved_buffers = [&]() {
    for (const auto &b : buffers) {
      if (b.modified)
        return true;
    }
    return false;
  };

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
  } else if (lcmd == "debugpanel") {
    toggle_debugger_panel();
  } else if (lcmd == "debug" || lcmd == "debuggdb") {
    start_debugger_command("gdb", arg);
  } else if (lcmd == "debuglldb") {
    start_debugger_command("lldb", arg);
  } else if (lcmd == "debugconfig") {
    run_debugger_config(arg);
  } else if (lcmd == "debugattach") {
    attach_debugger_command("gdb", arg);
  } else if (lcmd == "debugstop") {
    stop_debugger_session();
  } else if (lcmd == "debugrestart") {
    restart_debugger_session();
  } else if (lcmd == "debugcontinue") {
    continue_debugger_session();
  } else if (lcmd == "debugpause") {
    pause_debugger_session();
  } else if (lcmd == "debugstep") {
    step_debugger_in();
  } else if (lcmd == "debugnext") {
    step_debugger_next();
  } else if (lcmd == "debugout") {
    step_debugger_out();
  } else if (lcmd == "debugthreads") {
    show_debugger_threads();
  } else if (lcmd == "debugmemory") {
    request_debugger_memory(arg, 128);
  } else if (lcmd == "debugdisasm") {
    request_debugger_disassembly(arg);
  } else if (lcmd == "find" || lcmd == "ff") {
    std::string target = trim_copy(arg);
    if (target.empty()) {
      target = root_dir.empty() ? "." : root_dir;
    }
    telescope.open(target);
    waiting_for_space_f = false;
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    command_palette_selected = 0;
    needs_redraw = true;
    return false;
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
  } else if (lcmd == "hover" || lcmd == "lsphover") {
    request_lsp_hover();
  } else if (lcmd == "definition" || lcmd == "lspdefinition" ||
             lcmd == "lspdef" || lcmd == "gd") {
    request_lsp_definition();
  } else if (lcmd == "lspback") {
    return_from_lsp_definition();
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
  } else if (lcmd == "tsinstall" || lcmd == "treesitterinstall") {
    if (arg.empty()) {
      set_message("Usage: :tsinstall <language>");
    } else {
      install_tree_sitter_language(arg);
    }
  } else if (lcmd == "tsstatus") {
    show_tree_sitter_status();
  } else if (lcmd == "tsreload" || lcmd == "treesitterreload") {
    reload_tree_sitter();
  } else if (lcmd == "gitrefresh") {
    refresh_git_status(true);
    if (has_git_repo()) {
      set_message("Git refreshed: " + git_branch + " (+" +
                  std::to_string(git_staged_count) + " ~" +
                  std::to_string(git_unstaged_count) + " ?" +
                  std::to_string(git_untracked_count) + ")");
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
  } else if (lcmd == "gitstage") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else {
      auto &buf = get_buffer();
      std::string target = trim_copy(arg);
      if (target.empty() && !buf.filepath.empty()) {
        target = buf.filepath;
      }
      if (target.empty()) {
        set_message("Usage: :gitstage [file]");
      } else if (git_stage_path(target)) {
        fs::path p(target);
        std::string shown =
            p.is_absolute() ? to_git_relative_path(target) : p.generic_string();
        set_message("Git staged: " + shown);
      } else {
        set_message("Git stage failed: " + target);
      }
    }
  } else if (lcmd == "gitunstage") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else {
      auto &buf = get_buffer();
      std::string target = trim_copy(arg);
      if (target.empty() && !buf.filepath.empty()) {
        target = buf.filepath;
      }
      if (target.empty()) {
        set_message("Usage: :gitunstage [file]");
      } else if (git_unstage_path(target)) {
        fs::path p(target);
        std::string shown =
            p.is_absolute() ? to_git_relative_path(target) : p.generic_string();
        set_message("Git unstaged: " + shown);
      } else {
        set_message("Git unstage failed: " + target);
      }
    }
  } else if (lcmd == "gitstageall") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else if (git_stage_all()) {
      set_message("Git staged all changes");
    } else {
      set_message("Git stage all failed");
    }
  } else if (lcmd == "gitunstageall") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else if (git_unstage_all()) {
      set_message("Git unstaged all changes");
    } else {
      set_message("Git unstage all failed");
    }
  } else if (lcmd == "gitcommit") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else {
      std::string message = trim_copy(arg);
      if (message.empty()) {
        set_message("Usage: :gitcommit <message>");
      } else if (git_commit_message(message)) {
        set_message("Git commit created");
      } else {
        set_message("Git commit failed");
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
  } else if (lcmd == "gitdiffstaged") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else {
      auto &buf = get_buffer();
      std::string target = trim_copy(arg);
      if (target.empty()) {
        if (buf.filepath.empty()) {
          set_message("Usage: :gitdiffstaged [file]");
        } else {
          target = to_git_relative_path(buf.filepath);
        }
      }
      if (!target.empty()) {
        std::string diff =
            run_git_capture("diff --staged -- " + shell_quote(target));
        if (trim_copy(diff).empty()) {
          set_message("Git diff: no staged changes for " + target);
        } else {
          show_popup(limit_lines(diff, 18), 2, tab_height + 1);
        }
      }
    }
  } else if (lcmd == "gitlog") {
    refresh_git_status(true);
    if (!has_git_repo()) {
      set_message("Git: not a repository");
    } else {
      std::string log = run_git_capture("log --oneline --decorate -n 30");
      if (trim_copy(log).empty()) {
        set_message("Git log unavailable");
      } else {
        show_popup(limit_lines(log, 18), 2, tab_height + 1);
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
  } else if (lcmd == "home") {
    set_home_menu_visible(true);
    set_message("Home");
  } else if (lcmd == "resume") {
    resume_last_workspace_session();
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
  } else if (lcmd == "fold" || lcmd == "collapse") {
    fold_at_cursor();
  } else if (lcmd == "unfold" || lcmd == "expand") {
    unfold_at_cursor();
  } else if (lcmd == "togglefold") {
    toggle_fold_at_cursor();
  } else if (lcmd == "foldall") {
    fold_all();
  } else if (lcmd == "unfoldall") {
    unfold_all();
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
      show_command_help(arg);
    } else {
      set_message("Unknown command: " + line);
    }
  return true;
}
