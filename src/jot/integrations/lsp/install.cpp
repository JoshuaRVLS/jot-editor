// Editor-side LSP install/remove: background jobs (silent log-file transport
// with an integrated-terminal fallback), live progress polling, and the
// server-id usage hint built from the Lua registry.
#include "editor.h"
#include "jot/app/process_job.h"
#include "jot/editor_models.h"
#include "jot/lua/api.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include "lsp/install.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lsp_internal
{
  // "id1|id2|..." hint for the unknown-server message, from the Lua registry.
  std::string lsp_server_usage_hint(LuaAPI *api)
  {
    std::vector<LspServerSpec> servers;
    if (api)
    {
      api->lsp_install_list(&servers);
    }
    std::string out;
    for (const auto &spec : servers)
    {
      if (!out.empty())
      {
        out += "|";
      }
      out += spec.id;
    }
    return out.empty() ? "see :help lspinstall" : out;
  }
} // namespace lsp_internal

void Editor::poll_lsp_installs()
{
  bool changed = false;
  for (auto &job : lsp_install_jobs)
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
      lines = term->get_recent_lines(80);
      if (!term->is_active())
      {
        transport_dead = true;
      }
    }

    bool resolved = false;
    std::string tail_line;
    for (const auto &line : lines)
    {
      LspInstall::Marker marker;
      if (!LspInstall::parse_marker(line, marker) || marker.server != job.server)
      {
        // Not a completion marker: remember the newest tool output row so the
        // status view can show live progress while the script runs.
        if (job.running)
        {
          tail_line = line;
        }
        continue;
      }
      if (marker.phase == "start")
      {
        const std::string next = job.removing ? "removing" : "downloading";
        if (job.progress != next)
        {
          job.progress = next;
          set_message("LSP " + std::string(job.removing ? "remove started: " : "install started: ")
                      + job.server);
        }
      }
      else if (marker.phase == "success" && marker.exit_code == 0)
      {
        job.progress = job.removing ? "removed" : "installed";
        job.running = false;
        job.succeeded = true;
        job.failed = false;
        resolved = true;
        set_message("LSP " + std::string(job.removing ? "remove OK: " : "install OK: ")
                    + job.server);
        if (!job.removing)
        {
          // A server that was installed while its files were already open
          // never got notified (attach only fires on open/restart/enable):
          // attach those buffers now so "installed but inactive" heals itself.
          heal_lsp_attach_for(job.server);
        }
      }
      else if (marker.phase == "failed")
      {
        job.progress = marker.exit_code >= 0
                           ? "failed (exit " + std::to_string(marker.exit_code) + ")"
                           : "failed";
        job.running = false;
        job.succeeded = false;
        job.failed = true;
        resolved = true;
        set_message("LSP " + std::string(job.removing ? "remove failed: " : "install failed: ")
                    + job.server);
      }
      changed = true;
    }

    // Live progress: mirror the newest tool output row into the job state so
    // the status view moves while the script is still running.
    if (job.running && !tail_line.empty())
    {
      std::string clean = tail_line;
      auto strip = [](std::string &s)
      {
        // Drop ANSI escapes and carriage returns (curl progress rewrites rows).
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
          if (s[i] == '\x1b')
          {
            i++;
            if (i < s.size() && s[i] == '[')
            {
              i++;
              while (i < s.size() && !(s[i] >= '@' && s[i] <= '~'))
                i++;
              if (i < s.size())
                i++;
            }
            continue;
          }
          if (s[i] != '\r')
          {
            out.push_back(s[i]);
          }
          i++;
        }
        s = out;
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
          s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
          s.erase(s.begin());
        if (s.size() > 60)
        {
          s.resize(60);
          s += "…";
        }
      };
      strip(clean);
      if (!clean.empty() && clean != job.progress)
      {
        job.progress = std::move(clean);
        changed = true;
      }
    }

    // The transport ended without the script reporting start/success/failure:
    // treat the job as failed (a successful script always prints a marker).
    if (job.running && transport_dead && !resolved)
    {
      job.running = false;
      job.succeeded = false;
      job.failed = true;
      job.progress = "process exited";
      set_message("LSP " + std::string(job.removing ? "remove failed: " : "install failed: ")
                  + job.server);
      changed = true;
    }
  }
  if (changed)
  {
    needs_redraw = true;
  }
}

void Editor::install_web_toolchain()
{
  std::vector<LspPolicyTool> tools;
  if (!lua_api || !lua_api->lsp_policy_preset("web", &tools))
  {
    set_message("Web toolkit preset unavailable (Lua policy not loaded)");
    needs_redraw = true;
    return;
  }
  int servers = 0;
  int parsers = 0;
  for (const auto &tool : tools)
  {
    if (tool.kind == "lsp")
    {
      if (install_lsp_server(tool.name))
      {
        servers++;
      }
    }
    else if (tool.kind == "parser")
    {
      if (install_tree_sitter_language(tool.name))
      {
        parsers++;
      }
    }
  }
  set_message("Web toolkit queued: " + std::to_string(servers) + " LSP server(s) + "
              + std::to_string(parsers) + " parser(s)");
  needs_redraw = true;
}

bool Editor::install_lsp_server(const std::string &name)
{
  // The Lua installer registry owns server resolution (ids + aliases) and
  // the per-manager install scripts; the native side only reports progress
  // through the background-job poll loop.
  std::string server, script, message;
  const bool known = lua_api && lua_api->lsp_install_plan(name, &server, &script, &message);
  if (!known || server.empty())
  {
    set_message("Unknown LSP server: " + name + " (use "
                + lsp_internal::lsp_server_usage_hint(lua_api) + ")");
    return false;
  }
  if (script.empty())
  {
    set_message(message);
    needs_redraw = true;
    return false;
  }

  auto active_install =
      std::find_if(lsp_install_jobs.begin(),
                   lsp_install_jobs.end(),
                   [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (active_install != lsp_install_jobs.end())
  {
    set_message("LSP install/remove already running: " + server);
    open_lsp_status_modal();
    return true;
  }

  lsp_install_jobs.erase(std::remove_if(lsp_install_jobs.begin(),
                                        lsp_install_jobs.end(),
                                        [&](const LspInstallJob &job)
                                        { return job.server == server && !job.running; }),
                         lsp_install_jobs.end());

  LspInstallJob job;
  job.server = server;
  job.removing = false;
  job.progress = "starting";

  // Preferred path: a silent background job - no terminal panel opens, the
  // output streams into a log file and the poll loop reports progress.
  job.output_path = process_job::make_install_log_path("lsp-" + server);
#ifndef _WIN32
  job.pid = process_job::spawn_background_shell(LspInstall::wrap_script(server, script),
                                                job.output_path);
#endif
  if (job.pid >= 0)
  {
    lsp_install_jobs.push_back(std::move(job));
    set_message(message);
    open_lsp_status_modal();
    return true;
  }

  // Fallback (background spawn unavailable): run in an integrated terminal so
  // installs still work even where a detached process cannot be started.
  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspinstall:" + server);
  if (integrated_terminals.size() == terminal_count)
  {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active())
  {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  job.terminal_index = terminal_index;
  lsp_install_jobs.push_back(std::move(job));
  term->send_text(LspInstall::wrap_script(server, script) + "\r");
  set_message(message + " (terminal " + std::to_string(terminal_index + 1) + ")");
  open_lsp_status_modal();
  return true;
}

bool Editor::remove_lsp_server(const std::string &name)
{
  std::string server, script, message;
  const bool known = lua_api && lua_api->lsp_remove_plan(name, &server, &script, &message);
  if (!known || server.empty())
  {
    set_message("Unknown LSP server: " + name + " (use "
                + lsp_internal::lsp_server_usage_hint(lua_api) + ")");
    return false;
  }
  if (script.empty())
  {
    set_message(message);
    needs_redraw = true;
    return false;
  }

  auto active_remove =
      std::find_if(lsp_install_jobs.begin(),
                   lsp_install_jobs.end(),
                   [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (active_remove != lsp_install_jobs.end())
  {
    set_message("LSP install/remove already running: " + server);
    open_lsp_status_modal();
    return true;
  }

  lsp_install_jobs.erase(std::remove_if(lsp_install_jobs.begin(),
                                        lsp_install_jobs.end(),
                                        [&](const LspInstallJob &job)
                                        { return job.server == server && !job.running; }),
                         lsp_install_jobs.end());

  // Stop any running client so the uninstall never leaves a live process
  // behind, but do NOT mark the language disabled: that flag is persisted
  // as workspace state and would silently block re-attach forever after a
  // reinstall (the server is disabled only when the user says so).
  for (auto &client : lsp_clients)
  {
    if (client && client->get_language() == server)
    {
      drop_lsp_diagnostics_for_client(server, client->get_root_path());
      unwatch_lsp_client_fds(client.get());
      client->stop();
    }
  }
  for (auto &buf : buffers)
  {
    if (lsp_internal::detect_lsp_language(buf.filepath) == server)
    {
      buf.diagnostics.clear();
      lsp_pending_changes.erase(buf.filepath);
    }
  }
  invalidate_sidebar_diagnostics_cache();
  needs_redraw = true;

  LspInstallJob job;
  job.server = server;
  job.removing = true;
  job.progress = "starting";

  // Preferred path: a silent background job - no terminal panel opens.
  job.output_path = process_job::make_install_log_path("lsp-" + server);
#ifndef _WIN32
  job.pid = process_job::spawn_background_shell(LspInstall::wrap_script(server, script),
                                                job.output_path);
#endif
  if (job.pid >= 0)
  {
    lsp_install_jobs.push_back(std::move(job));
    set_message(message);
    open_lsp_status_modal();
    return true;
  }

  // Fallback (background spawn unavailable): run in an integrated terminal.
  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspremove:" + server);
  if (integrated_terminals.size() == terminal_count)
  {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active())
  {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  job.terminal_index = terminal_index;
  lsp_install_jobs.push_back(std::move(job));
  term->send_text(LspInstall::wrap_script(server, script) + "\r");
  set_message(message + " (terminal " + std::to_string(terminal_index + 1) + ")");
  open_lsp_status_modal();
  return true;
}