// Editor-side LSP attachment: resolving the server for a file, launching and
// reusing clients, and broadcasting document lifecycle notifications.
#include "editor.h"
#include "jot/editor_models.h"
#include "jot/lua/api.h"
#include "jot/lua/lua_loader.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

std::string Editor::get_buffer_text(const FileBuffer &buf) const
{
  if (buf.is_lazy())
  {
    return "";
  }
  size_t total_size = buf.lines.empty() ? 0 : buf.lines.size() - 1;
  for (const auto &line : buf.lines)
  {
    total_size += line.size();
  }

  std::string text;
  text.reserve(total_size);
  for (size_t i = 0; i < buf.line_count(); i++)
  {
    if (i > 0)
    {
      text.push_back('\n');
    }
    text.append(buf.line(i));
  }
  return text;
}

LSPClient *Editor::ensure_lsp_client_process(const std::string &server,
                                             const std::string &root_path,
                                             const std::vector<std::string> &command,
                                             const std::vector<std::string> &library_dirs,
                                             const std::string &initialization_options)
{
  size_t existing_index = lsp_clients.size();
  for (size_t i = 0; i < lsp_clients.size(); i++)
  {
    if (lsp_clients[i] && lsp_clients[i]->get_language() == server
        && lsp_clients[i]->get_root_path() == root_path)
    {
      existing_index = i;
      break;
    }
  }
  if (existing_index < lsp_clients.size())
  {
    LSPClient *existing = lsp_clients[existing_index].get();
    if (existing->is_running())
    {
      return existing;
    }
    // A dead client may hold a stale command from when the server binary was
    // not yet installed (bare name or vanished path). Restarting it would just
    // fail again, so drop it and rebuild below with a freshly resolved command.
    drop_lsp_diagnostics_for_client(server, root_path);
    unwatch_lsp_client_fds(existing);
    existing->stop();
    lsp_clients.erase(lsp_clients.begin() + (long)existing_index);
  }

  if (command.empty())
  {
    return nullptr;
  }
  auto client =
      std::make_unique<LSPClient>(server, root_path, command, library_dirs, initialization_options);
  if (!client->start())
  {
    set_message("LSP start failed for " + server + ": " + client->get_last_error());
    return nullptr;
  }

  lsp_clients.push_back(std::move(client));
  watch_lsp_client_fds(lsp_clients.back().get());
  return lsp_clients.back().get();
}

LSPClient *Editor::ensure_lsp_for_file(const std::string &filepath)
{
  if (filepath.empty())
  {
    return nullptr;
  }

  std::string language = lsp_internal::detect_lsp_language(filepath);
  if (language.empty())
  {
    return nullptr;
  }
  if (lsp_disabled_servers.count(language))
  {
    return nullptr;
  }

  const std::string root = lsp_internal::find_workspace_root(filepath, language);
  std::vector<std::string> command = lsp_internal::command_for_language(language);

  // For lua, register the bundled jot API stub (EmmyLua annotations) as a
  // server library so user scripts get completions for the whole jot.*
  // surface instead of "undefined global" warnings. The stub ships with the
  // runtime, so it resolves to the source dir, an override, or the cache.
  std::vector<std::string> library_dirs;
  if (language == "lua")
  {
    const std::filesystem::path stub = jot_lua_resolve_path("luals/jot_api.lua");
    if (!stub.empty() && !stub.parent_path().empty())
    {
      library_dirs.push_back(stub.parent_path().string());
    }
  }

  // clangd keeps deduced-type inlay hints off unless the client enables them
  // explicitly (the VS Code extension sends the same settings). Parameter
  // hints are on by default there; both follow the editor's config keys.
  std::string initialization_options;
  if (language == "cpp")
  {
    const bool parameter_hints = config.get_bool("lsp_inlay_hints", true);
    const bool type_hints = config.get_bool("lsp_inlay_type_hints", true);
    initialization_options = "\"inlayHints\":{\"parameterNames\":"
                             + std::string(parameter_hints ? "true" : "false")
                             + ",\"deducedTypes\":"
                             + std::string(type_hints ? "true" : "false") + "}";
  }

  LSPClient *primary =
      ensure_lsp_client_process(language, root, command, library_dirs, initialization_options);
  if (!primary)
  {
    return nullptr;
  }

  // Extra servers the Lua policy wants next to the primary one (web stacks:
  // tailwind / eslint beside typescript, …). One file can therefore be served
  // by several LSP clients at once; document notifications broadcast to all of
  // them (see attached_lsp_clients_for) and diagnostics merge per buffer.
  std::vector<LspPolicyExtra> extras;
  if (lua_api && lua_api->lsp_policy_extras(language, filepath, &extras))
  {
    for (const auto &extra : extras)
    {
      if (extra.server.empty() || lsp_disabled_servers.count(extra.server))
      {
        continue;
      }
      if (extra.bin.empty() || !lsp_internal::lsp_bin_available(extra.bin))
      {
        continue;
      }
      std::vector<std::string> extra_command = {lsp_internal::resolve_lsp_bin(extra.bin)};
      for (const auto &arg : extra.args)
      {
        if (!arg.empty())
        {
          extra_command.push_back(arg);
        }
      }
      ensure_lsp_client_process(extra.server, root, extra_command, {});
    }
  }
  return primary;
}

LSPClient *Editor::find_lsp_client(const std::string &language, const std::string &root_path)
{
  for (auto &client : lsp_clients)
  {
    if (client && client->get_language() == language && client->get_root_path() == root_path)
    {
      return client.get();
    }
  }
  return nullptr;
}

std::vector<LSPClient *> Editor::attached_lsp_clients_for(const std::string &filepath,
                                                          std::string *root_out,
                                                          std::string *primary_out)
{
  std::vector<LSPClient *> out;
  if (filepath.empty())
  {
    return out;
  }
  const std::string primary = lsp_internal::detect_lsp_language(filepath);
  if (primary.empty())
  {
    return out;
  }
  const std::string root = lsp_internal::find_workspace_root(filepath, primary);
  if (root_out)
  {
    *root_out = root;
  }
  if (primary_out)
  {
    *primary_out = primary;
  }

  std::vector<std::string> extra_servers;
  std::vector<LspPolicyExtra> extras;
  if (lua_api && lua_api->lsp_policy_extras(primary, filepath, &extras))
  {
    for (const auto &extra : extras)
    {
      extra_servers.push_back(extra.server);
    }
  }

  auto covers = [&](const LSPClient *client)
  {
    if (!client || !client->is_running() || client->get_root_path() != root)
    {
      return false;
    }
    if (client->get_language() == primary)
    {
      return true;
    }
    return std::find(extra_servers.begin(), extra_servers.end(), client->get_language())
           != extra_servers.end();
  };
  for (auto &client : lsp_clients)
  {
    if (covers(client.get()))
    {
      out.push_back(client.get());
    }
  }
  return out;
}

void Editor::notify_lsp_open(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  // A fresh open re-derives diagnostics: clear every server's slices for this
  // file so a publish from one server never re-mixes stale ones.
  for (auto &by_client : lsp_diag_slices_)
  {
    by_client.second.erase(filepath);
  }
  refresh_lsp_diagnostics_for(filepath);

  if (!ensure_lsp_for_file(filepath))
  {
    return;
  }

  std::string root;
  std::string primary;
  const auto clients = attached_lsp_clients_for(filepath, &root, &primary);
  if (clients.empty())
  {
    return;
  }
  const std::string language_id = lsp_internal::language_id_for(primary, filepath);
  for (const auto &buf : buffers)
  {
    if (buf.filepath == filepath)
    {
      if (buf.is_lazy())
        return;
      for (LSPClient *client : clients)
      {
        client->did_open(filepath, language_id, get_buffer_text(buf));
      }
      break;
    }
  }
}

void Editor::heal_lsp_attach_for(const std::string &language)
{
  if (language.empty())
  {
    return;
  }
  for (auto &buf : buffers)
  {
    if (buf.filepath.empty() || buf.is_lazy())
    {
      continue;
    }
    if (lsp_internal::detect_lsp_language(buf.filepath) != language)
    {
      continue;
    }
    if (!ensure_lsp_for_file(buf.filepath))
    {
      continue;
    }
    std::string root;
    std::string primary;
    const auto clients = attached_lsp_clients_for(buf.filepath, &root, &primary);
    const std::string language_id = lsp_internal::language_id_for(primary, buf.filepath);
    for (LSPClient *client : clients)
    {
      // Safe when the document is already open on this client: LSPClient turns
      // a duplicate did_open into a full-text didChange, and after a restart
      // its version table is empty so this sends a real didOpen.
      client->did_open(buf.filepath, language_id, get_buffer_text(buf));
    }
  }
}

void Editor::notify_lsp_change(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes[filepath] = lsp_internal::now_ms() + lsp_change_debounce_ms;
}

void Editor::notify_lsp_save(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes.erase(filepath);
  if (!ensure_lsp_for_file(filepath))
  {
    return;
  }
  std::string root;
  std::string primary;
  const auto clients = attached_lsp_clients_for(filepath, &root, &primary);
  for (const auto &buf : buffers)
  {
    if (buf.filepath == filepath)
    {
      for (LSPClient *client : clients)
      {
        client->did_save(filepath, get_buffer_text(buf));
      }
      break;
    }
  }
}

void Editor::notify_lsp_close(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes.erase(filepath);
  // Tell every server that still has the document open, then forget our
  // diagnostic slices for it.
  for (auto &client : lsp_clients)
  {
    if (client && client->has_open_document(filepath))
    {
      client->did_close(filepath);
    }
  }
  // The diagnostics the servers already published for this file are kept, not
  // dropped: they are what the workspace diagnostics picker shows, and a file
  // being closed says nothing about whether the problem in it is still there.
  // Re-opening the file re-derives them (notify_lsp_open clears the slices
  // first), so nothing goes stale once the server looks again.
}