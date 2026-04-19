#include "editor.h"
#include <algorithm>

namespace {
constexpr std::size_t kMaxUndoHistory = 500;

State capture_state(const FileBuffer &buf) {
  State s;
  s.lines = buf.lines;
  s.cursor = buf.cursor;
  s.preferred_x = buf.preferred_x;
  s.selection = buf.selection;
  s.scroll_offset = buf.scroll_offset;
  s.scroll_x = buf.scroll_x;
  s.modified = buf.modified;
  return s;
}

bool same_state(const State &a, const State &b) {
  return a.lines == b.lines && a.cursor == b.cursor &&
         a.preferred_x == b.preferred_x &&
         a.selection.start == b.selection.start &&
         a.selection.end == b.selection.end &&
         a.selection.active == b.selection.active &&
         a.scroll_offset == b.scroll_offset && a.scroll_x == b.scroll_x &&
         a.modified == b.modified;
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
} // namespace

void Editor::save_state() {
  auto &buf = get_buffer();
  if (buf.is_preview) {
    buf.is_preview = false;
    if (preview_buffer_index == current_buffer) {
      preview_buffer_index = -1;
    }
  }

  const State s = capture_state(buf);
  if (!buf.undo_stack.empty() && same_state(buf.undo_stack.top(), s)) {
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

  buf.redo_stack.push(capture_state(buf));
  trim_stack(buf.redo_stack, kMaxUndoHistory);

  State prev = std::move(buf.undo_stack.top());
  buf.undo_stack.pop();
  buf.lines = std::move(prev.lines);
  buf.cursor = prev.cursor;
  buf.preferred_x = prev.preferred_x;
  buf.selection = prev.selection;
  buf.scroll_offset = std::max(0, prev.scroll_offset);
  buf.scroll_x = std::max(0, prev.scroll_x);
  buf.modified = prev.modified;

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

  buf.undo_stack.push(capture_state(buf));
  trim_stack(buf.undo_stack, kMaxUndoHistory);

  State next = std::move(buf.redo_stack.top());
  buf.redo_stack.pop();
  buf.lines = std::move(next.lines);
  buf.cursor = next.cursor;
  buf.preferred_x = next.preferred_x;
  buf.selection = next.selection;
  buf.scroll_offset = std::max(0, next.scroll_offset);
  buf.scroll_x = std::max(0, next.scroll_x);
  buf.modified = next.modified;

  invalidate_syntax_cache(buf);
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}
