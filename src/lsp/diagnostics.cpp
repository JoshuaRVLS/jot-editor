// Editor-side diagnostics: merging per-server slices -- and the C++ definition
// checks, which publish in the same per-file shape -- into per-buffer
// diagnostics, dropping a server's contributions when it goes away, and holding
// fresh findings back while the user is typing in the file they describe.
#include "core/editor.h"
#include "lsp/common.h"
#include <iterator>
#include <string>
#include <utility>
#include <vector>

void Editor::refresh_lsp_diagnostics_for(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  // Findings the user is about to be told about while their hands are still on
  // the keys: hold them. The slices already hold whatever the servers sent, so
  // nothing is lost -- the paint (squiggle, row band, inline message, gutter
  // colour) waits for the pause instead of moving under the typing, and the
  // flush re-merges whatever arrived meanwhile.
  if (holds_live_diagnostics(filepath, lsp_internal::now_ms()))
  {
    lsp_diagnostics_held_.insert(filepath);
    return;
  }
  lsp_diagnostics_held_.erase(filepath);
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
  // The C++ definition checks are not a language server, but their rows live in
  // the same list: a declaration with no body is as real a diagnostic as a
  // compiler's, and the Problems list, the explorer's badges and the gutter all
  // read this one merge.
  const auto cpp = cpp_def_diags.find(filepath);
  if (cpp != cpp_def_diags.end())
  {
    for (const auto &diag : cpp->second)
    {
      merged.push_back(diag);
    }
  }
  // set_diagnostics normalizes the path, dedupes buffer hits, bumps the
  // sidebar cache and fires DiagnosticChanged exactly once per refresh.
  set_diagnostics(filepath, merged);
}

bool Editor::holds_live_diagnostics(const std::string &filepath, long long now_ms) const
{
  if (lsp_diagnostics_quiet_ms <= 0)
  {
    return false; // the setting asks for findings the moment they arrive
  }
  const auto edit = lsp_last_edit_ms_.find(filepath);
  if (edit == lsp_last_edit_ms_.end())
  {
    return false; // nobody has typed in this file: a background publish paints
  }
  if (now_ms - edit->second >= lsp_diagnostics_quiet_ms)
  {
    return false; // the pause has already lasted long enough
  }
  const auto save = lsp_last_save_ms_.find(filepath);
  if (save != lsp_last_save_ms_.end() && save->second >= edit->second)
  {
    return false; // saved since the last edit: the user asked for the truth
  }
  return true;
}

void Editor::maybe_paint_held_lsp_diagnostics()
{
  if (lsp_diagnostics_held_.empty() && lsp_last_edit_ms_.empty())
  {
    return; // the idle case: two empty-map checks per tick
  }
  const long long now = lsp_internal::now_ms();
  // A stamp means something only for one quiet window's length, and after that
  // it can hold nothing. Dropping the old ones keeps a long session over many
  // files from carrying a map of every file ever typed in.
  const long long stale_before = now - lsp_diagnostics_quiet_ms;
  for (auto it = lsp_last_edit_ms_.begin(); it != lsp_last_edit_ms_.end();)
  {
    it = (it->second <= stale_before) ? lsp_last_edit_ms_.erase(it) : std::next(it);
  }
  for (auto it = lsp_last_save_ms_.begin(); it != lsp_last_save_ms_.end();)
  {
    it = (it->second <= stale_before) ? lsp_last_save_ms_.erase(it) : std::next(it);
  }
  if (lsp_diagnostics_held_.empty())
  {
    return;
  }
  std::vector<std::string> released;
  for (const auto &filepath : lsp_diagnostics_held_)
  {
    if (!holds_live_diagnostics(filepath, now))
    {
      released.push_back(filepath);
    }
  }
  for (const auto &filepath : released)
  {
    refresh_lsp_diagnostics_for(filepath);
  }
}

void Editor::paint_held_lsp_diagnostics_for(const std::string &filepath)
{
  if (lsp_diagnostics_held_.erase(filepath) > 0)
  {
    // The slices hold whatever the servers published while the findings were
    // held, so the merge below paints the freshest answer there is.
    refresh_lsp_diagnostics_for(filepath);
  }
}

void Editor::drop_lsp_diagnostics_for_client(const std::string &server, const std::string &root)
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