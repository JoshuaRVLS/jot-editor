// Editor-side LSP references: asks the server for all usages of the symbol
// under the cursor (textDocument/references) and shows them in the quick
// pick, one entry per location with the source line as preview. Selecting an
// entry jumps to it via the generic quick-pick handler.
#include "editor.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <fstream>

namespace
{
  // Best-effort source line for a location: prefers the open buffer, falls
  // back to reading the file from disk. Returns an empty string when the
  // file cannot be read.
  std::string line_text_for(const std::vector<FileBuffer> &buffers,
                            const std::string &filepath,
                            int line)
  {
    for (const auto &buf : buffers)
    {
      if (buf.filepath == filepath && !buf.is_lazy() && line >= 0
          && line < (int)buf.line_count())
      {
        return buf.line(line);
      }
    }
    std::ifstream in(filepath, std::ios::binary);
    if (!in)
    {
      return "";
    }
    std::string text;
    for (int i = 0; i <= line && std::getline(in, text); i++)
    {
      if (i == line)
      {
        break;
      }
    }
    return text;
  }

  std::string preview_line(std::string s)
  {
    for (char &c : s)
    {
      if (c == '\t' || c == '\r' || c == '\n')
      {
        c = ' ';
      }
    }
    size_t start = s.find_first_not_of(' ');
    if (start == std::string::npos)
    {
      return "";
    }
    s.erase(0, start);
    if (s.size() > 220)
    {
      s.resize(220);
    }
    return s;
  }
} // namespace

void Editor::request_lsp_references()
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

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_references(buf.filepath, buf.cursor.y, buf.cursor.x))
  {
    set_message("LSP references request failed");
    return;
  }
  set_message("LSP references requested");
}

void Editor::handle_lsp_references_results()
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
  std::vector<QuickPickItem> items;
  int total = 0;
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
    auto results = client->consume_reference_results();
    for (auto &result : results)
    {
      // Only surface results requested from the active buffer.
      if (!lsp_internal::same_path(result.origin_filepath, buf.filepath))
      {
        continue;
      }
      got_response = true;
      for (const auto &location : result.locations)
      {
        QuickPickItem item;
        item.filepath = location.filepath;
        item.line = std::max(0, location.line);
        item.col = std::max(0, location.character);
        item.label = get_filename(location.filepath) + ":" + std::to_string(item.line + 1)
                     + ":" + std::to_string(item.col + 1);
        item.detail = location.filepath;
        item.preview = preview_line(line_text_for(buffers, location.filepath, item.line));
        items.push_back(std::move(item));
        total++;
      }
    }
  }
  // The handler runs on every poll; only report when a request actually
  // completed (a real response with zero locations), never when nothing was
  // pending — otherwise the message spams on every poll.
  if (!got_response)
  {
    return;
  }
  if (total == 0)
  {
    set_message("No references found");
    return;
  }
  open_quick_pick(QUICK_PICK_REFERENCES, "References", std::move(items));
  set_message("References: " + std::to_string(total));
}