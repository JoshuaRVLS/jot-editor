// Editor-side LSP rename: asks the server to rename the symbol under the
// cursor (textDocument/rename) and applies the returned WorkspaceEdit to
// every open buffer it touches, reusing the shared LSPTextEdit applier.
// Results are drained in lifecycle.cpp's poll loop alongside format results.
#include "editor.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"

void Editor::lsp_rename_symbol(const std::string &new_name)
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
  if (new_name.empty())
  {
    set_message("Usage: :lsprename <new_name>");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_rename(buf.filepath, buf.cursor.y, buf.cursor.x, new_name))
  {
    set_message("LSP rename request failed");
    return;
  }
  set_message("LSP rename requested");
}