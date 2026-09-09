// Ex-command dispatch for the advanced commands (git, LSP, debugger, tasks,
// terminals, ...): the second half of the :command table that
// execute_ex_command hands off to.
#include "commands/utils.h"
#include "editor.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/workspace/git_run.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace CommandLineUtils;

namespace
{
namespace
{
  namespace fs = std::filesystem;

  auto parse_quoted_tokens = [](const std::string &text)
  {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;
    char quote_char = '\0';
    bool escape = false;
    for (char c : text)
    {
      if (escape)
      {
        current.push_back(c);
        escape = false;
        continue;
      }
      if (c == '\\')
      {
        escape = true;
        continue;
      }
      if (in_quote)
      {
        if (c == quote_char)
        {
          in_quote = false;
        }
        else
        {
          current.push_back(c);
        }
        continue;
      }
      if (c == '"' || c == '\'')
      {
        in_quote = true;
        quote_char = c;
        continue;
      }
      if (std::isspace((unsigned char)c))
      {
        if (!current.empty())
        {
          tokens.push_back(current);
          current.clear();
        }
        continue;
      }
      current.push_back(c);
    }
    if (!current.empty())
    {
      tokens.push_back(current);
    }
    return tokens;
  };
} // namespace
} // namespace

bool Editor::execute_ex_command_tail(const std::string &lcmd,
                                     const std::string &arg,
                                     const std::string &line)
{
  auto goto_line_col = [&](int line_1based, int col_1based)
  {
    auto &buf = get_buffer();
    if (buf.line_count() == 0)
    {
      return;
    }
    buf.cursor.y = std::clamp(line_1based - 1, 0, (int)buf.line_count() - 1);
    int line_len = (int)buf.line(buf.cursor.y).length();
    buf.cursor.x = std::clamp(col_1based - 1, 0, line_len);
    clear_selection();
    ensure_cursor_visible();
    set_message("Jumped to line " + std::to_string(buf.cursor.y + 1) + ", col "
                + std::to_string(buf.cursor.x + 1));
  };

  int parsed_line = 0, parsed_col = 1;
  if (lcmd == "lspinstall" || lcmd == "lspremove")
  {
    if (arg.empty())
    {
      set_message("Usage: :" + lcmd + " <" + lsp_install_usage_hint() + ">");
    }
    else if (lcmd == "lspinstall" && (arg == "web" || arg == "fullstack"))
    {
      install_web_toolchain();
    }
    else if (lcmd == "lspinstall")
    {
      install_lsp_server(arg);
    }
    else
    {
      remove_lsp_server(arg);
    }
  }
  else if (lcmd == "hover" || lcmd == "lsphover")
  {
    request_lsp_hover();
  }
  else if (lcmd == "definition" || lcmd == "lspdefinition" || lcmd == "lspdef" || lcmd == "gd")
  {
    request_lsp_definition();
  }
  else if (lcmd == "lspback")
  {
    return_from_lsp_definition();
  }
  else if (lcmd == "lsprename" || lcmd == "lspren")
  {
    lsp_rename_symbol(trim_copy(arg));
  }
  else if (lcmd == "diagnostics" || lcmd == "problems")
  {
    show_diagnostics_picker();
    return false;
  }
  else if (lcmd == "diagnext" || lcmd == "diagnosticnext")
  {
    goto_next_diagnostic(1);
  }
  else if (lcmd == "diagprev")
  {
    goto_next_diagnostic(-1);
  }
  else if (lcmd == "symbols")
  {
    show_symbol_picker();
    return false;
  }
  else if (lcmd == "outline" || lcmd == "outlinepanel")
  {
    toggle_outline_panel();
    return false;
  }
  else if (lcmd == "tsinstall" || lcmd == "treesitterinstall")
  {
    if (arg.empty())
    {
      set_message("Usage: :tsinstall <language>");
    }
    else if (arg == "web" || arg == "fullstack")
    {
      install_web_toolchain();
    }
    else
    {
      install_tree_sitter_language(arg);
    }
  }
  else if (lcmd == "tsstatus")
  {
    show_tree_sitter_status();
  }
  else if (lcmd == "lspstatus")
  {
    show_lsp_status();
  }
  else if (lcmd == "tsreload" || lcmd == "treesitterreload")
  {
    reload_tree_sitter();
  }
  else if (lcmd == "gitrefresh")
  {
    refresh_git_status(true);
    if (has_git_repo())
    {
      set_message("Git refreshed: " + git_branch + " (+" + std::to_string(git_staged_count) + " ~"
                  + std::to_string(git_unstaged_count) + " ?" + std::to_string(git_untracked_count)
                  + ")");
    }
    else
    {
      set_message("Git: not a repository");
    }
  }
  else if (lcmd == "lazygit")
  {
    open_git_client();
  }
  else if (lcmd == "gitstatus")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      std::string status = run_git_capture("status --short --branch");
      if (trim_copy(status).empty())
      {
        set_message("Git status unavailable");
      }
      else
      {
        show_popup(limit_lines(status, 18), "Git Status");
      }
    }
  }
  else if (lcmd == "gitstage")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      auto &buf = get_buffer();
      std::string target = trim_copy(arg);
      if (target.empty() && !buf.filepath.empty())
      {
        target = buf.filepath;
      }
      if (target.empty())
      {
        set_message("Usage: :gitstage [file]");
      }
      else if (git_stage_path(target))
      {
        fs::path p(target);
        std::string shown = p.is_absolute() ? to_git_relative_path(target) : p.generic_string();
        set_message("Git staged: " + shown);
      }
      else
      {
        set_message("Git stage failed: " + target);
      }
    }
  }
  else if (lcmd == "gitunstage")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      auto &buf = get_buffer();
      std::string target = trim_copy(arg);
      if (target.empty() && !buf.filepath.empty())
      {
        target = buf.filepath;
      }
      if (target.empty())
      {
        set_message("Usage: :gitunstage [file]");
      }
      else if (git_unstage_path(target))
      {
        fs::path p(target);
        std::string shown = p.is_absolute() ? to_git_relative_path(target) : p.generic_string();
        set_message("Git unstaged: " + shown);
      }
      else
      {
        set_message("Git unstage failed: " + target);
      }
    }
  }
  else if (lcmd == "gitstageall")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else if (git_stage_all())
    {
      set_message("Git staged all changes");
    }
    else
    {
      set_message("Git stage all failed");
    }
  }
  else if (lcmd == "gitunstageall")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else if (git_unstage_all())
    {
      set_message("Git unstaged all changes");
    }
    else
    {
      set_message("Git unstage all failed");
    }
  }
  else if (lcmd == "gitcommit")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      std::string message = trim_copy(arg);
      if (message.empty())
      {
        set_message("Usage: :gitcommit <message>");
      }
      else
      {
        const std::string err = git_commit_message(message);
        if (err.empty())
        {
          set_message("Git commit created");
        }
        else
        {
          set_message("Git commit failed: " + err);
        }
      }
      git_panel_refresh();
    }
  }
  else if (lcmd == "gitpanel")
  {
    toggle_git_panel();
  }
  else if (lcmd == "gitcheckout")
  {
    std::string a = trim_copy(arg);
    if (a.empty())
    {
      set_message("Usage: :gitcheckout <branch|hash> | -b <new-branch>");
    }
    else if (git_root.empty())
    {
      set_message("Git: not a repository");
    }
    else
    {
      std::string cmd = "checkout ";
      if (a.rfind("-b ", 0) == 0)
      {
        std::string name = trim_copy(a.substr(3));
        if (name.empty())
        {
          set_message("Usage: :gitcheckout -b <new-branch>");
        }
        else if (jot_git::run_ok(git_root, "checkout -b " + shell_util::shell_quote(name)))
        {
          set_message("Created and checked out: " + name);
        }
        else
        {
          set_message("Branch creation failed");
        }
      }
      else if (jot_git::run_ok(git_root, cmd + shell_util::shell_quote(a)))
      {
        set_message("Checked out: " + a);
      }
      else
      {
        set_message("Checkout failed: " + a);
      }
      refresh_git_status(true);
      git_panel_refresh();
    }
  }
  else if (lcmd == "gitmerge")
  {
    std::string a = trim_copy(arg);
    if (a.empty())
    {
      set_message("Usage: :gitmerge <branch>");
    }
    else if (git_root.empty())
    {
      set_message("Git: not a repository");
    }
    else if (jot_git::run_ok(git_root, "merge --no-edit " + shell_util::shell_quote(a)))
    {
      set_message("Merged " + a + " into " + git_branch);
    }
    else
    {
      set_message("Merge failed — conflicts? (resolve, then :gitmerge --continue)");
    }
    refresh_git_status(true);
    git_panel_refresh();
  }
  else if (lcmd == "gitdiff")
  {
    open_git_diff_panel(trim_copy(arg), false);
  }
  else if (lcmd == "gitdiffstaged")
  {
    open_git_diff_panel(trim_copy(arg), true);
  }
  else if (lcmd == "gitdiffclose")
  {
    close_git_diff_panel();
    set_message("Git Diff closed");
  }
  else if (lcmd == "gitdiffrefresh")
  {
    if (git_diff_panel.visible)
    {
      open_git_diff_panel(git_diff_panel.path, git_diff_panel.staged);
    }
    else
    {
      set_message("Git diff: no open diff");
    }
  }
  else if (lcmd == "gitlog")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      std::string log = run_git_capture("log --oneline --decorate -n 30");
      if (trim_copy(log).empty())
      {
        set_message("Git log unavailable");
      }
      else
      {
        show_popup(limit_lines(log, 18), "Git Log");
      }
    }
  }
  else if (lcmd == "gitblame")
  {
    refresh_git_status(true);
    if (!has_git_repo())
    {
      set_message("Git: not a repository");
    }
    else
    {
      auto &buf = get_buffer();
      if (buf.filepath.empty())
      {
        set_message("Git blame requires a saved file");
      }
      else
      {
        int line_no = std::max(1, buf.cursor.y + 1);
        std::string rel = to_git_relative_path(buf.filepath);
        std::string blame = run_git_capture("blame -L " + std::to_string(line_no) + ","
                                            + std::to_string(line_no) + " -- " + shell_quote(rel));
        if (trim_copy(blame).empty())
        {
          set_message("Git blame unavailable");
        }
        else
        {
          set_message(first_line_copy(blame));
        }
      }
    }
  }
  else if (lcmd == "home")
  {
    set_home_menu_visible(true);
    set_message("Home");
  }
  else if (lcmd == "resume")
  {
    resume_last_workspace_session();
  }
  else if (lcmd == "recent")
  {
    if (recent_files.empty())
    {
      set_message("Recent files: none");
    }
    else
    {
      std::string list = "Recent: ";
      int shown = std::min(8, (int)recent_files.size());
      for (int i = 0; i < shown; i++)
      {
        if (i > 0)
        {
          list += " | ";
        }
        list += std::to_string(i + 1) + ":" + get_filename(recent_files[i]);
      }
      if ((int)recent_files.size() > shown)
      {
        list += " | ...";
      }
      set_message(list);
    }
  }
  else if (lcmd == "openrecent")
  {
    open_recent_file(arg);
  }
  else if (lcmd == "reopen" || lcmd == "reopenlast")
  {
    reopen_last_closed_buffer();
  }
  else if (lcmd == "autosave")
  {
    if (arg.empty() || to_lower_copy(arg) == "status")
    {
      set_message("Auto-save: " + std::string(auto_save_enabled ? "ON" : "OFF") + " ("
                  + std::to_string(auto_save_interval_ms) + "ms)");
    }
    else
    {
      std::string mode = to_lower_copy(arg);
      if (mode == "on" || mode == "true" || mode == "1")
      {
        set_auto_save(true);
        set_message("Auto-save enabled (" + std::to_string(auto_save_interval_ms) + "ms)");
      }
      else if (mode == "off" || mode == "false" || mode == "0")
      {
        set_auto_save(false);
        set_message("Auto-save disabled");
      }
      else if (mode == "toggle")
      {
        set_auto_save(!auto_save_enabled);
        set_message("Auto-save: " + std::string(auto_save_enabled ? "ON" : "OFF") + " ("
                    + std::to_string(auto_save_interval_ms) + "ms)");
      }
      else
      {
        bool numeric = true;
        for (char c : mode)
        {
          if (!std::isdigit((unsigned char)c))
          {
            numeric = false;
            break;
          }
        }
        if (numeric)
        {
          set_auto_save_interval(std::stoi(mode));
          set_message("Auto-save interval set to " + std::to_string(auto_save_interval_ms) + "ms");
        }
        else
        {
          set_message("Usage: :autosave [on|off|toggle|status|<ms>]");
        }
      }
    }
  }
  else if (lcmd == "search")
  {
    toggle_search();
  }
  else if (lcmd == "format")
  {
    format_document();
  }
  else if (lcmd == "trim")
  {
    trim_trailing_whitespace();
  }
  else if (lcmd == "upper")
  {
    transform_selection_uppercase();
  }
  else if (lcmd == "lower")
  {
    transform_selection_lowercase();
  }
  else if (lcmd == "sortlines")
  {
    sort_selected_lines();
  }
  else if (lcmd == "sortdesc")
  {
    sort_selected_lines_desc();
  }
  else if (lcmd == "reverselines")
  {
    reverse_selected_lines();
  }
  else if (lcmd == "uniquelines")
  {
    unique_selected_lines();
  }
  else if (lcmd == "shufflelines")
  {
    shuffle_selected_lines();
  }
  else if (lcmd == "joinlines")
  {
    join_lines_selection_or_current();
  }
  else if (lcmd == "dupe")
  {
    duplicate_selection_or_line();
  }
  else if (lcmd == "trimblank")
  {
    trim_blank_lines_in_selection();
  }
  else if (lcmd == "copypath")
  {
    copy_current_file_path();
  }
  else if (lcmd == "copyname")
  {
    copy_current_file_name();
  }
  else if (lcmd == "datetime")
  {
    insert_current_datetime();
  }
  else if (lcmd == "stats")
  {
    show_buffer_stats();
  }
  else if (lcmd == "replace" || lcmd == "replacei" || lcmd == "replaceword" || lcmd == "replacere")
  {
    auto tokens = parse_quoted_tokens(arg);
    if (tokens.size() < 2)
    {
      set_message("Usage: :" + lcmd + " <from> <to> (quote spaces)");
    }
    else if (lcmd == "replace")
    {
      replace_all_text(tokens[0], tokens[1], true, false);
    }
    else if (lcmd == "replacei")
    {
      replace_all_text(tokens[0], tokens[1], false, false);
    }
    else if (lcmd == "replaceword")
    {
      replace_all_text(tokens[0], tokens[1], true, true);
    }
    else
    {
      replace_all_regex(tokens[0], tokens[1]);
    }
  }
  else if (lcmd == "surround")
  {
    auto tokens = parse_quoted_tokens(arg);
    if (tokens.empty())
    {
      set_message("Usage: :surround <left> [right]");
    }
    else if (tokens.size() == 1)
    {
      std::string right = tokens[0];
      if (tokens[0] == "(")
        right = ")";
      else if (tokens[0] == "[")
        right = "]";
      else if (tokens[0] == "{")
        right = "}";
      surround_selection_or_word(tokens[0], right);
    }
    else
    {
      surround_selection_or_word(tokens[0], tokens[1]);
    }
  }
  else if (lcmd == "unsurround")
  {
    unsurround_selection_or_cursor();
  }
  else if (lcmd == "fold" || lcmd == "collapse")
  {
    fold_at_cursor();
  }
  else if (lcmd == "unfold" || lcmd == "expand")
  {
    unfold_at_cursor();
  }
  else if (lcmd == "togglefold")
  {
    toggle_fold_at_cursor();
  }
  else if (lcmd == "foldall")
  {
    fold_all();
  }
  else if (lcmd == "unfoldall")
  {
    unfold_all();
  }
  else if (lcmd == "incnum")
  {
    increment_number_at_cursor(1);
  }
  else if (lcmd == "decnum")
  {
    increment_number_at_cursor(-1);
  }
  else if (lcmd == "line" || lcmd == "goto")
  {
    if (arg.empty())
    {
      set_message("Usage: :line <line>[:col]");
    }
    else if (parse_line_col(arg, parsed_line, parsed_col))
    {
      goto_line_col(parsed_line, parsed_col);
    }
    else
    {
      set_message("Invalid location: " + arg);
    }
  }
  else if (lcmd == "resizeleft")
  {
    if (!resize_current_pane_direction('h', 2))
    {
      set_message("Resize left unavailable");
    }
    else
    {
      set_message("Pane resized left");
    }
  }
  else if (lcmd == "resizeright")
  {
    if (!resize_current_pane_direction('l', 2))
    {
      set_message("Resize right unavailable");
    }
    else
    {
      set_message("Pane resized right");
    }
  }
  else if (lcmd == "resizeup")
  {
    if (!resize_current_pane_direction('k', 2))
    {
      set_message("Resize up unavailable");
    }
    else
    {
      set_message("Pane resized up");
    }
  }
  else if (lcmd == "resizedown")
  {
    if (!resize_current_pane_direction('j', 2))
    {
      set_message("Resize down unavailable");
    }
    else
    {
      set_message("Pane resized down");
    }
  }
  else if (lcmd == "theme" || lcmd == "colorscheme" || lcmd == "colo")
  {
    const auto themes = list_available_themes();
    if (arg.empty())
    {
      if (themes.empty())
      {
        set_message("No themes found");
      }
      else
      {
        std::string list = "Themes: ";
        for (size_t i = 0; i < themes.size(); i++)
        {
          if (i > 0)
            list += ", ";
          list += themes[i];
        }
        set_message(list);
      }
    }
    else
    {
      std::string theme = trim_copy(arg);
      std::string resolved;
      const std::string needle = to_lower_copy(theme);
      for (const auto &candidate : themes)
      {
        if (to_lower_copy(candidate) == needle)
        {
          resolved = candidate;
          break;
        }
      }
      if (resolved.empty())
      {
        set_message("Unknown theme: " + arg);
      }
      else
      {
        apply_theme(resolved);
      }
    }
  }
  else if (lcmd == "help" || lcmd == "h")
  {
    show_command_help(arg);
  }
  else if (lcmd == "update")
  {
    // Self-update (:update / :update run). The logic lives in Lua
    // (features/update.lua): this branch only forwards the typed command so
    // it reaches the Lua module like any native command.
    if (lua_api && lua_api->run_update_command(arg))
    {
      needs_redraw = true;
    }
    else
    {
      set_message("Update runtime unavailable (features/update.lua not loaded)");
    }
  }
  else if (lua_api && lua_api->run_plugin_command(lcmd, arg))
  {
    needs_redraw = true;
  }
  else
  {
    set_message("Unknown command: " + line);
  }
  return true;
}
