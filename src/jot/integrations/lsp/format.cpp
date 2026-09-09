// Editor-side LSP formatting: requesting a format and applying the returned
// text edits to the buffer (highest-first so earlier positions stay valid).
#include "editor.h"
#include "lsp/client.h"
#include "jot/integrations/lsp/common.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

bool Editor::lsp_format_active_buffer()
{
  auto &buf = get_buffer();
  if (buf.filepath.empty() || buf.is_lazy())
  {
    return false;
  }
  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client || !client->is_running() || !client->is_initialized()
      || !client->has_open_document(buf.filepath))
  {
    return false;
  }
  if (!client->request_format(buf.filepath, std::max(1, tab_size)))
  {
    return false;
  }
  set_message("Formatting via " + client->describe());
  needs_redraw = true;
  return true;
}

void Editor::apply_lsp_text_edits(const std::string &filepath,
                                  const std::vector<LSPTextEdit> &edits)
{
  if (edits.empty())
  {
    return;
  }
  FileBuffer *target = nullptr;
  for (auto &buf : buffers)
  {
    if (buf.filepath == filepath && !buf.is_lazy())
    {
      target = &buf;
      break;
    }
  }
  if (!target)
  {
    return;
  }

  // Apply highest-first so earlier positions stay valid.
  std::vector<LSPTextEdit> sorted = edits;
  std::sort(sorted.begin(),
            sorted.end(),
            [](const LSPTextEdit &a, const LSPTextEdit &b)
            {
              if (a.start_line != b.start_line)
              {
                return a.start_line > b.start_line;
              }
              return a.start_char > b.start_char;
            });

  bool applied_any = false;
  for (const auto &edit : sorted)
  {
    const int line_count = (int)target->lines.size();
    const int sl = std::clamp(edit.start_line, 0, std::max(0, line_count - 1));
    const int el = std::clamp(edit.end_line, 0, std::max(0, line_count - 1));
    if (sl > el)
    {
      continue;
    }
    int sc = std::clamp(edit.start_char, 0, (int)target->lines[(size_t)sl].size());
    int ec = std::clamp(edit.end_char, 0, (int)target->lines[(size_t)el].size());
    if (sl == el && sc > ec)
    {
      std::swap(sc, ec); // degenerate reversed range on one line
    }
    if (sl == el && sc == ec && edit.new_text.empty())
    {
      continue; // empty no-op edit
    }

    if (!applied_any)
    {
      save_state();
      applied_any = true;
    }

    // Rebuild the row list for the edited span. Row sl keeps its head up to
    // sc, row el keeps its tail from ec; the replacement text is spliced
    // between them (its own newlines become new rows).
    const std::string head = target->lines[(size_t)sl].substr(0, (size_t)sc);
    const std::string tail = target->lines[(size_t)el].substr((size_t)ec);

    std::vector<std::string> parts; // newText split on '\n'
    {
      size_t pos = 0;
      while (pos <= edit.new_text.size())
      {
        const size_t nl = edit.new_text.find('\n', pos);
        if (nl == std::string::npos)
        {
          parts.push_back(edit.new_text.substr(pos));
          break;
        }
        parts.push_back(edit.new_text.substr(pos, nl - pos));
        pos = nl + 1;
      }
      if (parts.empty())
      {
        parts.push_back("");
      }
    }

    std::vector<std::string> out;
    out.reserve(target->lines.size() + parts.size());
    for (int i = 0; i < sl; i++)
    {
      out.push_back(target->lines[(size_t)i]);
    }
    const size_t last = parts.size() - 1;
    for (size_t j = 0; j < parts.size(); j++)
    {
      std::string row = (j == 0 ? head : "");
      row += parts[j];
      if (j == last)
      {
        row += tail;
      }
      out.push_back(std::move(row));
    }
    for (int i = el + 1; i < line_count; i++)
    {
      out.push_back(target->lines[(size_t)i]);
    }
    target->lines = std::move(out);
  }

  if (applied_any)
  {
    target->modified = true;
    invalidate_syntax_cache(*target);
    if (target->cursor.y >= (int)target->lines.size())
    {
      target->cursor.y = std::max(0, (int)target->lines.size() - 1);
    }
    target->cursor.x =
        std::clamp(target->cursor.x, 0, (int)target->lines[(size_t)target->cursor.y].size());
    needs_redraw = true;
    if (!target->filepath.empty())
    {
      notify_lsp_change(target->filepath);
    }
  }
}