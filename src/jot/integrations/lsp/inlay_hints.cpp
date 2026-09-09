// Editor-side LSP inlay hints: requests textDocument/inlayHint for the
// visible range of the current buffer, caches the answers per file, and
// exposes them to the buffer renderer as parameter-name virtual text on
// already-written code (clangd-style). Hints refresh when the file changes
// (debounced through the normal did_change flow) or when scrolling leaves
// the cached range.
#include "editor.h"
#include "jot/integrations/lsp/common.h"
#include "lsp/client.h"
#include "features/column_utils.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// Lines of slack requested above and below the viewport so small scrolls do
// not immediately re-ask the server.
static constexpr int kInlayHintMarginLines = 100;

// Hint cells inserted before `byte_col` on `line` of `filepath` — the amount
// the text at that position is shifted right on screen. Shared by the buffer
// renderer's overlays, the hardware caret placement, and the mouse mapping
// so every coordinate agrees on where hints sit.
//
// Stale hints stay visible while a refresh is in flight (dirty): hiding them
// on every keystroke makes the caret snap left and right as the server
// answers, which is far worse than a few hundred ms of slightly stale labels.
int Editor::lsp_inlay_hint_cells_before(const std::string &filepath,
                                        int line,
                                        int byte_col,
                                        const std::string &line_text)
{
  if (!config.get_bool("lsp_inlay_hints", true))
  {
    return 0;
  }
  auto it = lsp_inlay_hint_caches.find(filepath);
  if (it == lsp_inlay_hint_caches.end())
  {
    return 0;
  }
  const bool type_hints_enabled = config.get_bool("lsp_inlay_type_hints", true);
  int cells = 0;
  for (const auto &h : it->second.hints)
  {
    const bool is_param = h.kind == 2;
    const bool is_type = h.kind == 1;
    if ((!is_param && !is_type) || (is_type && !type_hints_enabled) || h.label.empty()
        || h.line != line || h.character < 0 || h.character > byte_col
        || h.character > (int)line_text.size())
    {
      continue;
    }
    int w = ui_cell_count(h.label);
    if (h.padding_left)
    {
      w += 1;
    }
    if (h.padding_right)
    {
      w += 1;
    }
    cells += w;
  }
  return cells;
}

// (shifted screen column, cell width) for every hint on `line`, mirroring the
// buffer renderer's layout so click mapping agrees with what is drawn.
std::vector<std::pair<int, int>> Editor::lsp_inlay_hints_visual(const std::string &filepath,
                                                                int line,
                                                                const std::string &line_text,
                                                                int tab_size)
{
  std::vector<std::pair<int, int>> out;
  if (!config.get_bool("lsp_inlay_hints", true))
  {
    return out;
  }
  auto it = lsp_inlay_hint_caches.find(filepath);
  if (it == lsp_inlay_hint_caches.end())
  {
    return out;
  }
  const bool type_hints_enabled = config.get_bool("lsp_inlay_type_hints", true);
  int inserted = 0;
  for (const auto &h : it->second.hints)
  {
    const bool is_param = h.kind == 2;
    const bool is_type = h.kind == 1;
    if ((!is_param && !is_type) || (is_type && !type_hints_enabled) || h.label.empty()
        || h.line != line || h.character < 0 || h.character > (int)line_text.size())
    {
      continue;
    }
    int w = ui_cell_count(h.label);
    if (h.padding_left)
    {
      w += 1;
    }
    if (h.padding_right)
    {
      w += 1;
    }
    if (w > 0)
    {
      out.emplace_back(compute_visual_column(line_text, h.character, tab_size) + inserted, w);
      inserted += w;
    }
  }
  return out;
}

void Editor::mark_lsp_inlay_hints_dirty(const std::string &filepath)
{
  auto it = lsp_inlay_hint_caches.find(filepath);
  if (it == lsp_inlay_hint_caches.end())
  {
    return;
  }
  it->second.dirty = true;
  it->second.in_flight = false;
}

void Editor::handle_lsp_inlay_hints_result(const LSPInlayHintResult &result)
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  auto &cache = lsp_inlay_hint_caches[result.origin_filepath];
  cache.start_line = result.start_line;
  cache.end_line = result.end_line;
  cache.in_flight = false;
  cache.dirty = false;
  cache.hints = result.hints;
  // Servers return hints in document order, but the renderer and the
  // coordinate helpers both assume position-sorted hints (early-break
  // scans), so normalize defensively on ingest.
  std::sort(cache.hints.begin(),
            cache.hints.end(),
            [](const LSPInlayHint &a, const LSPInlayHint &b)
            {
              if (a.line != b.line)
              {
                return a.line < b.line;
              }
              return a.character < b.character;
            });
  needs_redraw = true;
}

// Asks each attached client for the buffer's visible range when the cache is
// stale (file edited), missing (first view), or no longer covers the viewport
// (scrolled). One request per file at a time.
void Editor::refresh_lsp_inlay_hints_if_needed()
{
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size()
      || panes.empty())
  {
    return;
  }
  auto &buf = get_buffer();
  if (buf.is_lazy() || buf.filepath.empty())
  {
    return;
  }
  const std::string &filepath = buf.filepath;
  auto &cache = lsp_inlay_hint_caches[filepath];
  if (cache.in_flight)
  {
    return;
  }

  // The pane the user sees is the active one; its height bounds the viewport.
  const SplitPane &pane = get_pane();
  const int viewport_h = std::max(1, pane.h - tab_height - 1);
  const int first_visible = std::max(0, buf.scroll_offset - kInlayHintMarginLines);
  const int last_visible =
      std::min((int)std::max<long long>(0, buf.line_count() - 1),
               buf.scroll_offset + viewport_h + kInlayHintMarginLines);
  if (!cache.dirty && cache.start_line >= 0 && cache.start_line <= first_visible
      && cache.end_line >= last_visible)
  {
    return; // the cache still covers everything on screen
  }

  std::string root;
  std::string primary;
  const auto clients = attached_lsp_clients_for(filepath, &root, &primary);
  bool any_sent = false;
  for (LSPClient *client : clients)
  {
    if (client && client->is_running() && client->has_open_document(filepath)
        && client->request_inlay_hints(filepath, first_visible, 0, last_visible, 0))
    {
      any_sent = true;
    }
  }
  if (any_sent)
  {
    cache.in_flight = true;
  }
}