// Buffer close / reopen lifecycle: close_buffer_at with unsaved-change
// prompts, close_buffer, and reopening the last closed buffer.
#include "editor.h"
#include "folding.h"
#include "jot/app/file_internal.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

using namespace file_internal;


void Editor::close_buffer_at(int index)
{
  if (index < 0 || index >= (int)buffers.size())
    return;

  if (!buffers[index].filepath.empty())
  {
    save_file_fold_state(buffers[index]);
    notify_lsp_close(buffers[index].filepath);
  }

  const FileBuffer &snapshot_source = buffers[index];
  invalidate_sidebar_diagnostics_cache();

#ifdef JOT_TREESITTER
  {
    FileBuffer &mut_buf = buffers[index];
    if (mut_buf.ts_tree)
    {
      ts_tree_delete(mut_buf.ts_tree);
      mut_buf.ts_tree = nullptr;
    }
    if (mut_buf.ts_parser)
    {
      ts_parser_delete(mut_buf.ts_parser);
      mut_buf.ts_parser = nullptr;
    }
  }
#endif

  if (closed_buffer_history.size() >= kMaxClosedBufferHistory)
  {
    closed_buffer_history.erase(closed_buffer_history.begin());
  }
  if (!snapshot_source.is_lazy()
      && (!snapshot_source.filepath.empty()
          || (snapshot_source.modified && !snapshot_source.lines.empty())))
  {
    closed_buffer_history.push_back({snapshot_source.filepath,
                                     snapshot_source.lines,
                                     snapshot_source.cursor,
                                     snapshot_source.selection,
                                     snapshot_source.scroll_offset,
                                     snapshot_source.scroll_x,
                                     snapshot_source.modified,
                                     snapshot_source.fold_ranges});
  }

  if (buffers.size() == 1)
  {
    FileBuffer &buf = buffers[0];
    if (buf.is_lazy())
      buf.materialize();
    buf.filepath.clear();
    buf.lines.clear();
    buf.lazy_provider.reset();
    buf.lines.push_back("");
    buf.cursor = {0, 0};
    buf.preferred_x = 0;
    buf.selection = {{0, 0}, {0, 0}, false};
    buf.scroll_offset = 0;
    buf.scroll_x = 0;
    buf.modified = false;
    buf.is_preview = false;
    buf.is_placeholder = true;
    buf.undo_stack = std::stack<State>();
    buf.redo_stack = std::stack<State>();
    buf.bookmarks.clear();
    buf.diagnostics.clear();
    buf.diag_severity_dirty = true;
    // Content is replaced wholesale: decoration positions are meaningless
    // against the new text, so drop them (consumers re-apply on their own
    // events, e.g. DiagnosticChanged after the reload).
    buf.decorations.clear();
    buf.decoration_base.clear();
    buf.decoration_base_valid = false;
    buf.decoration_dirty = false;
    buf.fold_ranges.clear();
    invalidate_sidebar_diagnostics_cache();
#ifdef JOT_TREESITTER
    if (buf.ts_tree)
    {
      ts_tree_delete(buf.ts_tree);
      buf.ts_tree = nullptr;
    }
    if (buf.ts_parser)
    {
      ts_parser_delete(buf.ts_parser);
      buf.ts_parser = nullptr;
    }
#endif
    current_buffer = 0;
    tab_scroll_index = 0;
    preview_buffer_index = -1;
    for (auto &pane : panes)
    {
      pane.buffer_id = 0;
      pane.tab_buffer_ids.clear();
      pane.tab_buffer_ids.push_back(0);
      pane.tab_scroll_index = 0;
    }
    message = "Closed file";
    needs_redraw = true;
    return;
  }

  int removed = index;

  if (preview_buffer_index == removed)
  {
    preview_buffer_index = -1;
  }
  buffers.erase(buffers.begin() + removed);

  if (current_buffer > removed)
  {
    current_buffer--;
  }
  else if (current_buffer >= (int)buffers.size())
  {
    current_buffer = (int)buffers.size() - 1;
  }

  if (tab_scroll_index > removed)
  {
    tab_scroll_index--;
  }
  tab_scroll_index = std::clamp(tab_scroll_index, 0, std::max(0, (int)buffers.size() - 1));
  if (preview_buffer_index > removed)
  {
    preview_buffer_index--;
  }
  if (preview_buffer_index < 0 || preview_buffer_index >= (int)buffers.size()
      || (preview_buffer_index >= 0 && !buffers[preview_buffer_index].is_preview))
  {
    preview_buffer_index = -1;
  }

  for (auto &pane : panes)
  {
    for (auto &id : pane.tab_buffer_ids)
    {
      if (id > removed)
      {
        id--;
      }
    }
    pane.tab_buffer_ids.erase(
        std::remove(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), removed),
        pane.tab_buffer_ids.end());

    if (pane.buffer_id == removed)
    {
      if (!pane.tab_buffer_ids.empty())
      {
        pane.buffer_id = pane.tab_buffer_ids.front();
      }
      else
      {
        pane.buffer_id = std::clamp(current_buffer, 0, std::max(0, (int)buffers.size() - 1));
        pane.tab_buffer_ids.push_back(pane.buffer_id);
      }
    }
    else if (pane.buffer_id > removed)
    {
      pane.buffer_id--;
    }

    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), pane.buffer_id)
        == pane.tab_buffer_ids.end())
    {
      pane.tab_buffer_ids.push_back(pane.buffer_id);
    }
    clamp_tab_scroll(pane);
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, pane.buffer_id), draw_w);
  }

  if (!panes.empty())
  {
    auto &pane = get_pane();
    capture_pane_view(current_pane);
    pane.buffer_id = current_buffer;
    restore_pane_view(current_pane);
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
        == pane.tab_buffer_ids.end())
    {
      pane.tab_buffer_ids.push_back(current_buffer);
    }
    clamp_tab_scroll(pane);
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  }
  message = "Closed file";
  needs_redraw = true;
}
void Editor::close_buffer()
{
  close_buffer_at(current_buffer);
}
void Editor::reopen_last_closed_buffer()
{
  if (closed_buffer_history.empty())
  {
    set_message("No recently closed buffer");
    return;
  }

  ClosedBufferSnapshot snap = closed_buffer_history.back();
  closed_buffer_history.pop_back();

  FileBuffer fb;
  fb.filepath = snap.filepath;
  fb.lines = snap.lines;
  if (fb.line_count() == 0)
  {
    fb.lines.push_back("");
  }
  fb.cursor = snap.cursor;
  fb.preferred_x = snap.cursor.x;
  fb.selection = snap.selection;
  fb.scroll_offset = std::max(0, snap.scroll_offset);
  fb.scroll_x = std::max(0, snap.scroll_x);
  fb.modified = snap.modified;
  fb.fold_ranges = snap.collapsed_folds;
  fb.is_preview = false;
  fb.is_placeholder = false;

  buffers.push_back(std::move(fb));
  current_buffer = (int)buffers.size() - 1;
  tab_scroll_index = std::min(tab_scroll_index, current_buffer);
  preview_buffer_index = -1;
  auto &pane = get_pane();
  capture_pane_view(current_pane);
  pane.buffer_id = current_buffer;
  restore_pane_view(current_pane);
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
      == pane.tab_buffer_ids.end())
  {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  clamp_cursor(current_buffer);
  ensure_cursor_visible();

  if (!buffers[current_buffer].filepath.empty())
  {
    track_recent_file(buffers[current_buffer].filepath);
    highlighter.set_language(get_file_extension(buffers[current_buffer].filepath));
    notify_lsp_open(buffers[current_buffer].filepath);
  }

  set_message("Reopened closed buffer");
}