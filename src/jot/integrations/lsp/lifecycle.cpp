// Editor-side LSP client lifecycle: the poll loop that pumps every client and
// fans results out to the feature modules, fd watching, and stop / restart /
// enable / disable.
#include "editor.h"
#include "jot/lua/api.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

void Editor::poll_lsp_clients()
{
  const long long current_time = lsp_internal::now_ms();
  std::vector<std::string> ready_changes;
  ready_changes.reserve(lsp_pending_changes.size());
  for (const auto &entry : lsp_pending_changes)
  {
    if (entry.second <= current_time)
    {
      ready_changes.push_back(entry.first);
    }
  }

  for (const auto &filepath : ready_changes)
  {
    lsp_pending_changes.erase(filepath);
    // Attach the primary server (and any policy extras) before broadcasting.
    if (!ensure_lsp_for_file(filepath))
    {
      continue;
    }
    std::string root;
    std::string primary;
    const auto clients = attached_lsp_clients_for(filepath, &root, &primary);
    for (const auto &buf : buffers)
    {
      if (buf.filepath != filepath)
      {
        continue;
      }
      for (LSPClient *client : clients)
      {
        client->did_change(filepath, get_buffer_text(buf));
      }
      mark_lsp_inlay_hints_dirty(filepath);
      break;
    }
  }

  for (auto &client : lsp_clients)
  {
    const int stdout_fd = client ? client->get_stdout_fd() : -1;
    const int stderr_fd = client ? client->get_stderr_fd() : -1;
    if (client && client->poll())
    {
      needs_redraw = true;
    }
    if (client && !client->is_running())
    {
      if (stdout_fd >= 0)
        event_loop_.unwatch_fd(stdout_fd);
      if (stderr_fd >= 0)
        event_loop_.unwatch_fd(stderr_fd);
    }
    if (!client)
    {
      continue;
    }
    // Diagnostics are stored per (server, file) and merged on refresh so two
    // servers attached to one buffer never clobber each other's findings.
    auto published = client->consume_published_diagnostics();
    if (!published.empty())
    {
      const std::string client_key =
          client->get_language() + "|" + client->get_root_path();
      for (auto &entry : published)
      {
        lsp_diag_slices_[client_key][entry.first] = std::move(entry.second);
        refresh_lsp_diagnostics_for(entry.first);
      }
    }

    auto formats = client->consume_format_results();
    for (auto &entry : formats)
    {
      apply_lsp_text_edits(entry.first, entry.second);
    }

    auto renames = client->consume_rename_results();
    for (auto &entry : renames)
    {
      apply_lsp_text_edits(entry.first, entry.second);
    }

    auto completions = client->consume_completion_items();
    for (auto &entry : completions)
    {
      if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
      {
        continue;
      }

      auto &buf = get_buffer();
      if (!lsp_internal::same_path(entry.first, buf.filepath))
      {
        continue;
      }

      // A Lua one-shot sink (jot.lsp.request_completion) consumes the items
      // first; the native completion popup only shows when Lua did not take
      // them.
      if (lua_api && lua_api->try_deliver_lsp_completion(entry.first, entry.second))
      {
        lsp_completion_manual_request = false;
        continue;
      }

      if (!lsp_completion_manual_request
          && (buf.cursor.y != lsp_completion_anchor.y
              || std::abs(buf.cursor.x - lsp_completion_anchor.x) > 4))
      {
        continue;
      }

      if (lua_api && lua_api->has_event_subscribers("lsp.completion"))
      {
        lua_api->emit_lsp_completion(entry.first, entry.second);
      }

      lsp_completion_all_items = std::move(entry.second);

      if (lsp_internal::is_html_filepath(entry.first))
      {
        lsp_internal::append_html_builtin_completions(lsp_completion_all_items);
      }

      lsp_completion_filepath = entry.first;
      bool visible = refresh_lsp_completion_filter();
      if (lsp_completion_manual_request && !visible)
      {
        set_message("No suggestions");
      }
      lsp_completion_manual_request = false;
      needs_redraw = true;
    }
    auto hovers = client->consume_hover_results();
    for (const auto &hover : hovers)
    {
      if (lua_api)
        lua_api->emit_lsp_hover(hover);
      handle_lsp_hover_result(hover);
    }

    auto signature_results = client->consume_signature_results();
    for (const auto &signature_help : signature_results)
    {
      handle_lsp_signature_result(signature_help);
    }

    auto inlay_hints = client->consume_inlay_hint_results();
    for (const auto &result : inlay_hints)
    {
      handle_lsp_inlay_hints_result(result);
    }

    auto definitions = client->consume_definition_results();
    for (const auto &definition : definitions)
    {
      if (lua_api)
        lua_api->emit_lsp_definition(definition);
      handle_lsp_definition_result(definition);
    }

    auto document_symbols = client->consume_document_symbol_results();
    for (const auto &symbols : document_symbols)
    {
      if (lua_api)
        lua_api->emit_lsp_symbols(symbols);
      handle_document_symbols_result(symbols);
    }
  }
  handle_lsp_references_results();
  refresh_lsp_inlay_hints_if_needed();
}

void Editor::watch_lsp_client_fds(LSPClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else

  auto watch_read = [this](int fd)
  {
    if (fd < 0 || event_loop_.is_watching_fd(fd))
    {
      return;
    }
    event_loop_.watch_fd(fd,
                         true,
                         false,
                         [this, fd]
                         {
                           bool found = false;
                           for (auto &client : lsp_clients)
                           {
                             if (!client)
                             {
                               continue;
                             }
                             if (client->get_stdout_fd() == fd || client->get_stderr_fd() == fd)
                             {
                               found = true;
                               break;
                             }
                           }
                           if (!found)
                           {
                             event_loop_.unwatch_fd(fd);
                             return;
                           }
                           poll_lsp_clients();
                         });
  };

  watch_read(client->get_stdout_fd());
  watch_read(client->get_stderr_fd());
#endif
}

void Editor::unwatch_lsp_client_fds(LSPClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else
  if (client->get_stdout_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stdout_fd());
  }
  if (client->get_stderr_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stderr_fd());
  }
#endif
}

void Editor::stop_all_lsp_clients()
{
  int stopped = 0;
  lsp_pending_changes.clear();
  lsp_diag_slices_.clear();
  for (auto &buf : buffers)
  {
    if (!buf.filepath.empty())
    {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  for (auto &client : lsp_clients)
  {
    if (client)
    {
      unwatch_lsp_client_fds(client.get());
    }
    if (client && client->is_running())
    {
      client->stop();
      stopped++;
    }
  }
  lsp_clients.clear();
  set_message("LSP stopped: " + std::to_string(stopped) + " client(s)");
}

void Editor::restart_all_lsp_clients()
{
  lsp_pending_changes.clear();
  // Old diagnostics belong to the pre-restart documents; refresh re-opens
  // every file below and servers re-publish fresh ones.
  lsp_diag_slices_.clear();
  for (auto &buf : buffers)
  {
    if (!buf.filepath.empty())
    {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  int restarted = 0;
  for (auto &client : lsp_clients)
  {
    if (!client)
    {
      continue;
    }
    unwatch_lsp_client_fds(client.get());
    if (client->restart())
    {
      watch_lsp_client_fds(client.get());
      restarted++;
    }
  }
  for (const auto &buf : buffers)
  {
    if (!buf.filepath.empty() && !buf.is_lazy())
    {
      notify_lsp_open(buf.filepath);
    }
  }
  set_message("LSP restarted: " + std::to_string(restarted) + " client(s)");
}

void Editor::set_lsp_server_enabled(const std::string &server, bool enabled)
{
  if (enabled)
  {
    lsp_disabled_servers.erase(server);
    if (!buffers.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size()
        && lsp_internal::detect_lsp_language(get_buffer().filepath) == server)
    {
      notify_lsp_open(get_buffer().filepath);
    }
  }
  else
  {
    lsp_disabled_servers.insert(server);
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
  }
  save_workspace_session();
  needs_redraw = true;
}