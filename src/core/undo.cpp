#include "editor.h"
#include <algorithm>
#include <cstddef>
#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace {
constexpr std::size_t kMaxUndoHistory = 500;

State capture_state(const FileBuffer &buf) {
  State s;
  const int total = (int)buf.line_count();

  if (total <= kMaxFullSnapshotLines) {
    s.full_snapshot = true;
    s.start_line = 0;
    s.old_total_lines = total;
    s.old_lines.reserve(total);
    for (int i = 0; i < total; i++) {
      s.old_lines.push_back(buf.line(i));
    }
  } else {
    s.full_snapshot = false;
    int start, end;
    if (buf.selection.active) {
      int sel_lo = std::min(buf.selection.start.y, buf.selection.end.y);
      int sel_hi = std::max(buf.selection.start.y, buf.selection.end.y);
      start = std::max(0, sel_lo - kDeltaWindowHalfSize);
      end = std::min(total, sel_hi + kDeltaWindowHalfSize + 1);
    } else {
      start = std::max(0, buf.cursor.y - kDeltaWindowHalfSize);
      end = std::min(total, buf.cursor.y + kDeltaWindowHalfSize + 1);
    }
    if (end <= start) {
      start = std::max(0, buf.cursor.y);
      end = std::min(total, start + 1);
    }
    s.start_line = start;
    s.old_total_lines = total;
    s.old_lines.reserve(end - start);
    for (int i = start; i < end; i++) {
      s.old_lines.push_back(buf.line(i));
    }
  }

  s.cursor = buf.cursor;
  s.preferred_x = buf.preferred_x;
  s.selection = buf.selection;
  s.scroll_offset = buf.scroll_offset;
  s.scroll_x = buf.scroll_x;
  s.modified = buf.modified;
  return s;
}

bool same_state(const FileBuffer &buf, const State &a, const State &b) {
  if (!(a.cursor == b.cursor) || a.preferred_x != b.preferred_x ||
      !(a.selection.start == b.selection.start) ||
      !(a.selection.end == b.selection.end) ||
      a.selection.active != b.selection.active ||
      a.scroll_offset != b.scroll_offset || a.scroll_x != b.scroll_x ||
      a.modified != b.modified) {
    return false;
  }
  if (a.full_snapshot != b.full_snapshot) {
    return false;
  }
  if (a.start_line != b.start_line ||
      a.old_total_lines != b.old_total_lines ||
      a.old_lines.size() != b.old_lines.size()) {
    return false;
  }
  for (size_t i = 0; i < a.old_lines.size(); i++) {
    if (a.old_lines[i] != b.old_lines[i]) {
      return false;
    }
  }
  return true;
}

void trim_stack(std::stack<State> &stack, std::size_t max_items) {
  if (stack.size() <= max_items) {
    return;
  }
  std::vector<State> items;
  items.reserve(stack.size());
  while (!stack.empty()) {
    items.push_back(std::move(stack.top()));
    stack.pop();
  }
  std::stack<State> rebuilt;
  const std::size_t kept = std::min(max_items, items.size());
  for (std::size_t i = kept; i > 0; --i) {
    rebuilt.push(std::move(items[i - 1]));
  }
  stack = std::move(rebuilt);
}

void apply_state(FileBuffer &buf, const State &prev) {
  const int prev_total = prev.old_total_lines;
  const int curr_total = (int)buf.line_count();
  const int line_diff = curr_total - prev_total;

  if (prev.full_snapshot) {
    std::vector<std::string> saved_lines;
    saved_lines.reserve(prev.old_lines.size());
    for (const auto &l : prev.old_lines) saved_lines.push_back(l);
    if (saved_lines.empty()) saved_lines.push_back("");

    if (buf.is_lazy()) {
      buf.lazy_provider->set_all_lines(saved_lines);
      buf.lines.clear();
    } else {
      buf.lines = std::move(saved_lines);
    }
  } else {
    const int old_window_size = (int)prev.old_lines.size();
    const int curr_window_size = old_window_size + line_diff;
    int start = prev.start_line;
    int count = curr_window_size;
    if (start < 0) {
      count += start;
      start = 0;
    }
    if (start > curr_total) {
      start = curr_total;
      count = 0;
    }
    if (count < 0) count = 0;
    if (start + count > curr_total) {
      count = curr_total - start;
    }

    buf.replace_lines(start, count, prev.old_lines);
  }

  buf.cursor = prev.cursor;
  buf.preferred_x = prev.preferred_x;
  buf.selection = prev.selection;
  buf.scroll_offset = std::max(0, prev.scroll_offset);
  buf.scroll_x = std::max(0, prev.scroll_x);
  buf.modified = prev.modified;
}
} // namespace

void Editor::save_state() {
  search_results.clear();
  search_result_index = -1;

  auto &buf = get_buffer();

#ifdef JOT_TREESITTER
  if (buf.ts_tree) {
    ts_tree_delete(buf.ts_tree);
    buf.ts_tree = nullptr;
  }
#endif
  if (buf.is_preview) {
    buf.is_preview = false;
    if (preview_buffer_index == current_buffer) {
      preview_buffer_index = -1;
    }
  }

  const State s = capture_state(buf);
  if (!buf.undo_stack.empty() && same_state(buf, buf.undo_stack.top(), s)) {
    return;
  }

  buf.undo_stack.push(s);
  trim_stack(buf.undo_stack, kMaxUndoHistory);
  while (!buf.redo_stack.empty()) {
    buf.redo_stack.pop();
  }
}

void Editor::undo() {
  auto &buf = get_buffer();
  if (buf.undo_stack.empty()) {
    return;
  }

  State redo_delta = capture_state(buf);
  buf.redo_stack.push(std::move(redo_delta));
  trim_stack(buf.redo_stack, kMaxUndoHistory);

  State prev = std::move(buf.undo_stack.top());
  buf.undo_stack.pop();

  apply_state(buf, prev);

  invalidate_syntax_cache(buf);
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}

void Editor::redo() {
  auto &buf = get_buffer();
  if (buf.redo_stack.empty()) {
    return;
  }

  State undo_delta = capture_state(buf);
  buf.undo_stack.push(std::move(undo_delta));
  trim_stack(buf.undo_stack, kMaxUndoHistory);

  State next = std::move(buf.redo_stack.top());
  buf.redo_stack.pop();

  apply_state(buf, next);

  invalidate_syntax_cache(buf);
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}
