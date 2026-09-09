// Editor-side LSP code actions: asks the server for quick fixes and
// refactors at the cursor (textDocument/codeAction), passing the
// diagnostics on the cursor line so servers can offer fixes, then shows the
// action titles in the quick pick. Selecting one applies its WorkspaceEdit
// through the shared text-edit applier.
#include "editor.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>

namespace
{
  // Diagnostics overlapping the cursor position; the request context uses
  // these so servers know which problems to fix. Falls back to the whole
  // line when no diagnostic touches the cursor exactly.
  std::vector<Diagnostic> diagnostics_at_cursor(const FileBuffer &buf,
                                                int line,
                                                int character)
  {
    std::vector<Diagnostic> out;
    for (const auto &diag : buf.diagnostics)
    {
      if (diag.line == line)
      {
        if (character >= diag.col && character <= std::max(diag.col, diag.end_col))
        {
          out.push_back(diag);
        }
      }
    }
    return out;
  }
} // namespace

void Editor::request_lsp_code_actions()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  const auto diags = diagnostics_at_cursor(buf, buf.cursor.y, buf.cursor.x);
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_code_actions(buf.filepath, buf.cursor.y, buf.cursor.x, diags))
  {
    set_message("LSP code action request failed");
    return;
  }
  set_message("LSP code actions requested");
}

void Editor::handle_lsp_code_action_results()
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  auto &buf = buffers[(size_t)current_buffer];
  if (buf.filepath.empty())
  {
    return;
  }
  std::vector<LSPCodeAction> actions;
  bool got_response = false;
  std::string root;
  std::string primary;
  const auto clients = attached_lsp_clients_for(buf.filepath, &root, &primary);
  for (LSPClient *client : clients)
  {
    if (!client)
    {
      continue;
    }
    auto results = client->consume_code_action_results();
    for (auto &result : results)
    {
      if (!lsp_internal::same_path(result.origin_filepath, buf.filepath))
      {
        continue;
      }
      got_response = true;
      for (auto &action : result.actions)
      {
        actions.push_back(std::move(action));
      }
    }
  }
  // The handler runs on every poll; only report when a request actually
  // completed (a real response with zero actions), never when nothing was
  // pending — otherwise the message spams on every poll.
  if (!got_response)
  {
    return;
  }
  if (actions.empty())
  {
    set_message("No code actions available");
    return;
  }

  // Stable: server order (quick fixes first, then refactors) is worth
  // keeping even when titles collide.
  std::vector<QuickPickItem> items;
  items.reserve(actions.size());
  for (size_t i = 0; i < actions.size(); i++)
  {
    const auto &action = actions[i];
    QuickPickItem item;
    item.label = action.title;
    std::string kind = action.kind;
    const size_t dot = kind.rfind('.');
    if (dot != std::string::npos)
    {
      kind = kind.substr(dot + 1);
    }
    item.detail = kind.empty() ? get_filename(buf.filepath) : kind;
    item.preview = get_filename(buf.filepath) + ":" + std::to_string(buf.cursor.y + 1);
    items.push_back(std::move(item));
  }

  lsp_code_actions_pending = std::move(actions);
  open_quick_pick(QUICK_PICK_CODE_ACTIONS, "Code Actions", std::move(items));
}

bool Editor::apply_selected_lsp_code_action()
{
  if (lsp_code_actions_pending.empty() || quick_pick_items.empty())
  {
    return false;
  }
  const int idx = std::clamp(quick_pick_selected, 0, (int)lsp_code_actions_pending.size() - 1);
  LSPCodeAction action = std::move(lsp_code_actions_pending[(size_t)idx]);
  lsp_code_actions_pending.clear();
  for (auto &entry : action.edits)
  {
    apply_lsp_text_edits(entry.first, entry.second);
  }
  set_message("Applied: " + action.title);
  refresh_git_status(true);
  return true;
}