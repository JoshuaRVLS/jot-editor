// Editor-side LSP diagnostics: merging per-server slices into per-buffer
// diagnostics and dropping a server's contributions when it goes away.
#include "editor.h"
#include "jot/integrations/lsp/common.h"
#include <string>
#include <utility>
#include <vector>

void Editor::refresh_lsp_diagnostics_for(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  std::vector<Diagnostic> merged;
  for (const auto &by_client : lsp_diag_slices_)
  {
    const auto it = by_client.second.find(filepath);
    if (it == by_client.second.end())
    {
      continue;
    }
    for (const auto &diag : it->second)
    {
      merged.push_back(diag);
    }
  }
  // set_diagnostics normalizes the path, dedupes buffer hits, bumps the
  // sidebar cache and fires DiagnosticChanged exactly once per refresh.
  set_diagnostics(filepath, merged);
}

void Editor::drop_lsp_diagnostics_for_client(const std::string &server,
                                             const std::string &root)
{
  const std::string client_key = server + "|" + root;
  auto it = lsp_diag_slices_.find(client_key);
  if (it == lsp_diag_slices_.end())
  {
    return;
  }
  std::vector<std::string> affected;
  affected.reserve(it->second.size());
  for (const auto &entry : it->second)
  {
    affected.push_back(entry.first);
  }
  lsp_diag_slices_.erase(it);
  for (const auto &filepath : affected)
  {
    refresh_lsp_diagnostics_for(filepath);
  }
}