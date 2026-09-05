#include "core/app/process_job.h"
#include "editor.h"
#include "lua_bridge/api.h"
#include "tree_sitter/install.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
  std::string trim_copy(const std::string &s)
  {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a]))
    {
      ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1]))
    {
      --b;
    }
    return s.substr(a, b - a);
  }

  bool parse_tree_sitter_marker(const std::string &line, std::string &phase, std::string &language)
  {
    const std::string marker = "[jot:treesitter] ";
    // Only trust markers that start their own terminal row. The install command
    // is echoed into the terminal as typed, and that echo contains the marker
    // strings as literal text - notably the EXIT trap's "failed" echo, which
    // parses as a genuine failure marker for the language. Requiring the marker
    // at the start of the line keeps the poll from reacting to the echo.
    size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, marker.size(), marker) != 0)
    {
      return false;
    }
    std::string rest = trim_copy(line.substr(first + marker.size()));
    size_t space = rest.find(' ');
    if (space == std::string::npos)
    {
      phase = rest;
      language.clear();
      return true;
    }
    phase = rest.substr(0, space);
    language = trim_copy(rest.substr(space + 1));
    size_t exit_pos = language.find(" exit=");
    if (exit_pos != std::string::npos)
    {
      language.erase(exit_pos);
    }
    return true;
  }
} // namespace

bool Editor::install_tree_sitter_language(const std::string &language)
{
  TreeSitterInstallCommand install = TreeSitterInstall::command_for_language(language);
  if (!install.supported)
  {
    set_message(install.message);
    return false;
  }

  // Open the status modal immediately; it shows the job's live progress
  // (cloning → building → … → installed) as the background job reports it.
  show_tree_sitter_status_modal = true;
  tree_sitter_status_scroll = 0;

  auto running = std::find_if(tree_sitter_install_jobs.begin(),
                              tree_sitter_install_jobs.end(),
                              [&](const TreeSitterInstallJob &job)
                              { return job.language == install.language && job.running; });
  if (running != tree_sitter_install_jobs.end())
  {
    set_message("Tree-sitter install already running: " + install.language);
    needs_redraw = true;
    return true;
  }

  TreeSitterInstallJob job;
  job.language = install.language;
  job.progress = "starting";

  // Preferred path: a silent background job with its output streamed into a
  // log file the poll loop reads for markers - no terminal panel is opened.
  job.output_path = process_job::make_install_log_path("ts-" + install.language);
#ifndef _WIN32
  job.pid = process_job::spawn_background_shell(install.command, job.output_path);
#endif
  if (job.pid >= 0)
  {
    tree_sitter_install_jobs.erase(
        std::remove_if(tree_sitter_install_jobs.begin(),
                       tree_sitter_install_jobs.end(),
                       [&](const TreeSitterInstallJob &old)
                       { return old.language == install.language; }),
        tree_sitter_install_jobs.end());
    tree_sitter_install_jobs.push_back(std::move(job));
    set_message(install.message);
    needs_redraw = true;
    return true;
  }

  // Fallback (background spawn unavailable): run in an integrated terminal so
  // installs still work even where a detached process cannot be started.
  create_integrated_terminal("tsinstall:" + install.language);
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active())
  {
    set_message("Failed to open Tree-sitter install terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  job.terminal_index = terminal_index;
  tree_sitter_install_jobs.erase(
      std::remove_if(tree_sitter_install_jobs.begin(),
                     tree_sitter_install_jobs.end(),
                     [&](const TreeSitterInstallJob &old)
                     { return old.language == install.language; }),
      tree_sitter_install_jobs.end());
  tree_sitter_install_jobs.push_back(std::move(job));
  term->send_text(install.command + "\r");
  set_message(install.message);
  needs_redraw = true;
  return true;
}

void Editor::show_tree_sitter_status()
{
  FileBuffer &buf = get_buffer();
  if (!buf.filepath.empty() && buf.line_count() > 0)
  {
    int line_idx = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
    get_line_syntax_colors(buf, line_idx);
  }

#ifdef JOT_TREESITTER
  const std::string ext = tree_sitter_extension_for_buffer(buf);
  TreeSitterRuntimeStatus status = ts_manager_.runtime_status_for_extension(ext);
#endif
  if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER)
  {
    std::string label =
        buf.syntax_language_label.empty() ? "tree-sitter" : buf.syntax_language_label;
    std::string message = "Tree-sitter active: " + label;
#ifdef JOT_TREESITTER
    if (status.used_builtin_query
        && status.query_message.find("runtime query failed") != std::string::npos)
    {
      message += " (" + status.query_message + ")";
    }
#endif
    set_message(message);
  }
  else if (buf.syntax_engine == SYNTAX_ENGINE_REGEX)
  {
    std::string label = buf.syntax_language_label.empty() ? "regex" : buf.syntax_language_label;
    std::string message = "Tree-sitter fallback: Regex " + label;
#ifdef JOT_TREESITTER
    if (status.has_language)
    {
      message += " (" + status.language_id + ": ";
      if (!status.parser_loaded)
      {
        message += status.parser_message;
      }
      else if (!status.query_loaded)
      {
        message += status.query_message;
      }
      else
      {
        message += "Tree-sitter unavailable";
      }
      message += ")";
    }
#endif
    set_message(message);
  }
  else
  {
    set_message("Tree-sitter inactive: Syntax off");
  }
  show_tree_sitter_status_modal = true;
  tree_sitter_status_scroll = 0;
  needs_redraw = true;
}

void Editor::reload_tree_sitter()
{
#ifdef JOT_TREESITTER
  for (auto &buf : buffers)
  {
    if (buf.ts_tree)
    {
      ts_tree_delete(buf.ts_tree);
      buf.ts_tree = nullptr;
    }
    if (buf.ts_parser)
    {
      ts_parser_delete(buf.ts_parser);
      buf.ts_parser = nullptr;
    }
    buf.ts_language_id.clear();
    invalidate_syntax_cache(buf);
  }
  if (lua_api && lua_api->reload_treesitter_runtime())
    set_message("Tree-sitter reloaded");
  else
    set_message("Tree-sitter Lua runtime unavailable");
#else
  set_message("Tree-sitter inactive: runtime not available");
#endif
}

void Editor::poll_tree_sitter_installs()
{
  bool changed = false;
  for (auto &job : tree_sitter_install_jobs)
  {
    if (!job.running)
    {
      continue;
    }

    // Gather new output lines from the active transport: the silent job's log
    // file when a background process is running, otherwise the fallback
    // integrated terminal.
    std::vector<std::string> lines;
    bool transport_dead = false;
    if (job.pid >= 0)
    {
      std::string text;
      job.output_offset = process_job::read_appended(job.output_path, job.output_offset, text);
      if (!text.empty())
      {
        // Only complete rows are parsed; a trailing unterminated line is not
        // yielded by getline and arrives on a later poll.
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
          lines.push_back(line);
        }
      }
      if (process_job::reap_child(job.pid) >= 0)
      {
        transport_dead = true;
      }
    }
    else
    {
      IntegratedTerminal *term = get_integrated_terminal(job.terminal_index);
      if (!term)
      {
        job.running = false;
        job.failed = true;
        job.progress = "terminal closed";
        changed = true;
        continue;
      }
      lines = term->get_recent_lines(200);
      if (!term->is_active())
      {
        transport_dead = true;
      }
    }

    bool saw_success = false;
    bool saw_failed = false;
    for (const auto &line : lines)
    {
      std::string phase;
      std::string lang;
      if (!parse_tree_sitter_marker(line, phase, lang))
      {
        continue;
      }
      if (phase == "prefix")
      {
        // The script reports the install root it actually used (its shell
        // environment may differ from the editor's). Remember it so the
        // parser can be found during verification.
        job.install_prefix = lang;
        changed = true;
        continue;
      }
      if (!lang.empty() && lang != job.language)
      {
        continue;
      }
      if (phase == "start")
      {
        job.progress = "starting";
      }
      else if (phase == "clone")
      {
        job.progress = "cloning";
      }
      else if (phase == "build")
      {
        job.progress = "building";
      }
      else if (phase == "link")
      {
        job.progress = "linking parser";
      }
      else if (phase == "query")
      {
        job.progress = "installing queries";
      }
      else if (phase == "success")
      {
        saw_success = true;
      }
      else if (phase == "failed")
      {
        saw_failed = true;
      }
      changed = true;
    }
    if (!job.install_prefix.empty() && !job.prefix_applied)
    {
      // Make the installed root searchable even when it is outside the
      // editor's default paths (e.g. JOT_TREESITTER_PREFIX or shell-only
      // XDG variables).
      ts_manager_.set_runtime_options(
          {job.install_prefix + "/parsers"}, {job.install_prefix + "/queries"}, {});
      job.prefix_applied = true;
    }
    if (saw_success && !job.succeeded)
    {
#ifdef JOT_TREESITTER
      auto status = ts_manager_.runtime_status_for_language(job.language);
      if (status.parser_loaded)
      {
        job.progress = "installed";
        job.running = false;
        job.succeeded = true;
        job.failed = false;
        job.verify_attempts = 0;
        reload_tree_sitter();
        for (auto &buf : buffers)
        {
          if (!buf.filepath.empty())
          {
            init_ts_for_buffer(buf);
          }
        }
        set_message("Tree-sitter installed: " + job.language);
      }
      else
      {
        job.verify_attempts++;
        if (job.verify_attempts < 4)
        {
          job.progress = "installed — verifying…";
          // Keep running to re-check next poll (filesystem / dlopen may need a moment).
        }
        else
        {
          job.progress = "installed — parser not found: " + status.parser_message;
          job.running = false;
          job.succeeded = false;
          job.failed = true;
          set_message("Tree-sitter installed but parser not found: " + job.language + " ("
                      + status.parser_message + ")");
        }
      }
#else
      job.progress = "installed";
      job.running = false;
      job.succeeded = true;
      job.failed = false;
      set_message("Tree-sitter installed: " + job.language);
#endif
      changed = true;
    }
    else if (saw_failed && !saw_success)
    {
      // Ignore transient failed markers that appear before clone (e.g. stale work dir)
      // Only treat as real failure if we have not yet seen success and job has been running a bit.
      // The shell script's trap prints failed only on non-zero exit, so respect it, but debounce.
      job.progress = "failed";
      job.running = false;
      job.failed = true;
      job.succeeded = false;
      set_message("Tree-sitter install failed: " + job.language);
      changed = true;
    }

    // The transport ended without the script reporting a result: treat the
    // install as failed (success leaves the job verifying for a few polls, so
    // it is exempt here).
    if (job.running && transport_dead && !saw_success)
    {
      job.running = false;
      if (!job.succeeded)
      {
        job.failed = true;
        job.progress = "failed";
      }
      changed = true;
    }
  }

  if (changed || show_tree_sitter_status_modal)
  {
    needs_redraw = true;
  }
}

bool Editor::handle_tree_sitter_status_input(int ch)
{
  if (!show_tree_sitter_status_modal)
  {
    return false;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q')
  {
    show_tree_sitter_status_modal = false;
    needs_redraw = true;
    return true;
  }
  int delta = 0;
  if (ch == 1008 || ch == 'k' || ch == 'K')
  {
    delta = -1;
  }
  else if (ch == 1009 || ch == 'j' || ch == 'J')
  {
    delta = 1;
  }
  else if (ch == 1001)
  {
    delta = 8;
  }
  else if (ch == 1012)
  {
    tree_sitter_status_scroll = 0;
    needs_redraw = true;
    return true;
  }
  else if (ch == 1013)
  {
    tree_sitter_status_scroll = 1000000;
    needs_redraw = true;
    return true;
  }
  if (delta != 0)
  {
    tree_sitter_status_scroll = std::max(0, tree_sitter_status_scroll + delta);
    needs_redraw = true;
    return true;
  }
  return true;
}
