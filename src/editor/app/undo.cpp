#include "editor.h"
#include "lua/api.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace
{
  // One line of a full snapshot: the previous state's copy of it when the text
  // has not changed, so an untouched line costs a pointer instead of a copy.
  // `prev` is the state the new one is about to sit on top of, or null when
  // there is none.
  //
  // The lines only line up when `prev` is a full snapshot of the same buffer;
  // when it is a window the indices mean different lines. That costs a hit
  // rather than correctness, because a line is only ever shared when its text is
  // the same, and the windowed capture below holds too few lines to be worth the
  // same treatment.
  SnapshotLine snapshot_line(const FileBuffer &buf, const State *prev, int index)
  {
    if (prev != nullptr && index < (int)prev->old_lines.size())
    {
      const SnapshotLine &shared = prev->old_lines[index];
      if (shared && *shared == buf.line(index))
      {
        return shared;
      }
    }
    return std::make_shared<const std::string>(buf.line(index));
  }

  // The text a snapshot restores, as a plain vector the buffer can take: the
  // snapshot holds its lines by reference, so restoring is where they are
  // copied out again.
  std::vector<std::string> materialize_lines(const std::vector<SnapshotLine> &lines)
  {
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (const SnapshotLine &line : lines)
    {
      out.push_back(line ? *line : std::string());
    }
    return out;
  }

  State capture_state(const FileBuffer &buf, const State *prev)
  {
    State s;
    const int total = (int)buf.line_count();

    if (total <= kMaxFullSnapshotLines)
    {
      s.full_snapshot = true;
      s.start_line = 0;
      s.old_total_lines = total;
      s.old_lines.reserve(total);
      for (int i = 0; i < total; i++)
      {
        s.old_lines.push_back(snapshot_line(buf, prev, i));
      }
    }
    else
    {
      s.full_snapshot = false;
      int start, end;
      int lo = buf.cursor.y;
      int hi = buf.cursor.y;
      if (buf.selection.active)
      {
        lo = std::min(buf.selection.start.y, buf.selection.end.y);
        hi = std::max(buf.selection.start.y, buf.selection.end.y);
      }
      for (const auto &caret : buf.extra_carets)
      {
        if (caret.active)
        {
          lo = std::min(lo, std::min(caret.start.y, caret.end.y));
          hi = std::max(hi, std::max(caret.start.y, caret.end.y));
        }
        else
        {
          lo = std::min(lo, caret.start.y);
          hi = std::max(hi, caret.start.y);
        }
      }
      start = std::max(0, lo - kDeltaWindowHalfSize);
      end = std::min(total, hi + kDeltaWindowHalfSize + 1);
      if (end <= start)
      {
        start = std::max(0, buf.cursor.y);
        end = std::min(total, start + 1);
      }
      s.start_line = start;
      s.old_total_lines = total;
      s.old_lines.reserve(end - start);
      for (int i = start; i < end; i++)
      {
        s.old_lines.push_back(std::make_shared<const std::string>(buf.line(i)));
      }
    }

    s.cursor = buf.cursor;
    s.preferred_x = buf.preferred_x;
    s.selection = buf.selection;
    s.extra_carets = buf.extra_carets;
    s.scroll_offset = buf.scroll_offset;
    s.scroll_x = buf.scroll_x;
    s.modified = buf.modified;
    s.is_placeholder = buf.is_placeholder;
    return s;
  }

  bool same_state(const FileBuffer &buf, const State &a, const State &b)
  {
    if (!(a.cursor == b.cursor) || a.preferred_x != b.preferred_x
        || !(a.selection.start == b.selection.start) || !(a.selection.end == b.selection.end)
        || a.selection.active != b.selection.active
        || a.extra_carets.size() != b.extra_carets.size() || a.scroll_offset != b.scroll_offset
        || a.scroll_x != b.scroll_x || a.modified != b.modified
        || a.is_placeholder != b.is_placeholder)
    {
      return false;
    }
    for (size_t i = 0; i < a.extra_carets.size(); i++)
    {
      if (!(a.extra_carets[i].start == b.extra_carets[i].start)
          || !(a.extra_carets[i].end == b.extra_carets[i].end)
          || a.extra_carets[i].active != b.extra_carets[i].active)
      {
        return false;
      }
    }
    if (a.full_snapshot != b.full_snapshot)
    {
      return false;
    }
    if (a.start_line != b.start_line || a.old_total_lines != b.old_total_lines
        || a.old_lines.size() != b.old_lines.size())
    {
      return false;
    }
    for (size_t i = 0; i < a.old_lines.size(); i++)
    {
      const SnapshotLine &left = a.old_lines[i];
      const SnapshotLine &right = b.old_lines[i];
      // The same line object cannot differ; a shared line answers here, which
      // is every line of a snapshot that only the edit touched.
      if (left == right)
      {
        continue;
      }
      if (!left || !right || *left != *right)
      {
        return false;
      }
    }
    return true;
  }

  // True when applying this state would put the buffer back exactly where it
  // already is. The fields compared are the ones apply_state writes, so a true
  // answer means the restore would be a no-op in every respect.
  bool state_matches_buffer(const FileBuffer &buf, const State &s)
  {
    if (!(s.cursor == buf.cursor) || s.preferred_x != buf.preferred_x
        || !(s.selection.start == buf.selection.start) || !(s.selection.end == buf.selection.end)
        || s.selection.active != buf.selection.active
        || s.extra_carets.size() != buf.extra_carets.size() || s.scroll_offset != buf.scroll_offset
        || s.scroll_x != buf.scroll_x || s.modified != buf.modified
        || s.is_placeholder != buf.is_placeholder)
    {
      return false;
    }
    for (std::size_t i = 0; i < s.extra_carets.size(); i++)
    {
      const Selection &a = s.extra_carets[i];
      const Selection &b = buf.extra_carets[i];
      if (!(a.start == b.start) || !(a.end == b.end) || a.active != b.active)
      {
        return false;
      }
    }
    if (s.old_total_lines != (int)buf.line_count())
    {
      return false;
    }

    // A full snapshot holds every line; a windowed one only the rows around the
    // carets, which is all applying it would replace.
    const int from = s.full_snapshot ? 0 : s.start_line;
    if (from < 0 || from + (int)s.old_lines.size() > (int)buf.line_count())
    {
      return false;
    }
    for (std::size_t i = 0; i < s.old_lines.size(); i++)
    {
      const std::string &line = buf.line(from + (int)i);
      if (s.old_lines[i] ? *s.old_lines[i] != line : !line.empty())
      {
        return false;
      }
    }
    return true;
  }

  // What pushing `s` costs the history: its own line-pointer array plus the
  // text of every line it created rather than shared with `prev`. Lines shared
  // with the state underneath cost nothing, which is the whole point of the
  // sharing in capture_state.
  std::size_t push_cost(const State &s, const State *prev)
  {
    std::size_t total = s.old_lines.size() * sizeof(SnapshotLine);
    for (std::size_t i = 0; i < s.old_lines.size(); i++)
    {
      const SnapshotLine &line = s.old_lines[i];
      const bool shared = prev != nullptr && i < prev->old_lines.size()
                          && prev->old_lines[i] == line;
      if (!shared && line)
      {
        total += sizeof(std::string) + line->size();
      }
    }
    return total;
  }

  // What dropping `s` releases: its pointer array, plus the text of the lines
  // nothing else holds any more (use_count() == 1 is this state's own
  // reference). A line a newer state still shares was paid for once, by the
  // state that created it; releasing the creator keeps it charged, so the
  // accounting can only overstate what is held, never understate it.
  std::size_t release_cost(const State &s)
  {
    std::size_t total = s.old_lines.size() * sizeof(SnapshotLine);
    for (const SnapshotLine &line : s.old_lines)
    {
      if (line && line.use_count() == 1)
      {
        total += sizeof(std::string) + line->size();
      }
    }
    return total;
  }

  void subtract_bytes(std::size_t &bytes, std::size_t amount)
  {
    bytes = amount >= bytes ? 0 : bytes - amount;
  }

  // Drops the oldest states until the stack fits both the state cap and the
  // byte budget, never going below kMinUndoStates. Undo pops the top, so the
  // oldest state is the one to give up: the depth it costs is the one nobody
  // walks to, and the newest states -- what undo actually reaches for -- stay.
  void trim_stack(std::stack<State> &stack, std::size_t &bytes)
  {
    if (stack.size() <= kMaxUndoHistory
        && (bytes <= kUndoHistoryBytes || stack.size() <= kMinUndoStates))
    {
      return;
    }
    std::vector<State> items;
    items.reserve(stack.size());
    while (!stack.empty())
    {
      items.push_back(std::move(stack.top()));
      stack.pop();
    }
    // items is top-first: the newest state is items[0], the oldest is back().
    // The drop walks the oldest states and releases each as it credits it: a
    // line shared only by states of this same drop is credited when the last
    // of them goes, so what the stacks hold stays an exact count rather than
    // one inflated by states this pass is already letting go -- an inflation
    // the state or byte cap could never spend its way back out of.
    std::size_t keep = items.size();
    while (keep > kMinUndoStates && (keep > kMaxUndoHistory || bytes > kUndoHistoryBytes))
    {
      keep--;
      subtract_bytes(bytes, release_cost(items[keep]));
      items[keep] = State();
    }
    std::stack<State> rebuilt;
    for (std::size_t i = keep; i > 0; --i)
    {
      rebuilt.push(std::move(items[i - 1]));
    }
    stack = std::move(rebuilt);
  }

  // Pushes a state and charges the history for what it holds.
  void push_state(std::stack<State> &stack, std::size_t &bytes, State s, const State *prev)
  {
    bytes += push_cost(s, prev);
    stack.push(std::move(s));
    trim_stack(stack, bytes);
  }

  // Pops the newest state, crediting the history for what it releases. The
  // moved-out state still holds the lines, so the credit is measured on it
  // after the pop (a line another state shares is not released, and stays
  // charged).
  State pop_state(std::stack<State> &stack, std::size_t &bytes)
  {
    State s = std::move(stack.top());
    stack.pop();
    subtract_bytes(bytes, release_cost(s));
    return s;
  }

  void apply_state(FileBuffer &buf, const State &prev)
  {
    const int prev_total = prev.old_total_lines;
    const int curr_total = (int)buf.line_count();
    const int line_diff = curr_total - prev_total;

    if (prev.full_snapshot)
    {
      std::vector<std::string> saved_lines = materialize_lines(prev.old_lines);
      if (saved_lines.empty())
        saved_lines.push_back("");

      if (buf.is_lazy())
      {
        buf.lazy_provider->set_all_lines(saved_lines);
        buf.lines.clear();
      }
      else
      {
        buf.lines = std::move(saved_lines);
      }
    }
    else
    {
      const int old_window_size = (int)prev.old_lines.size();
      const int curr_window_size = old_window_size + line_diff;
      int start = prev.start_line;
      int count = curr_window_size;
      if (start < 0)
      {
        count += start;
        start = 0;
      }
      if (start > curr_total)
      {
        start = curr_total;
        count = 0;
      }
      if (count < 0)
        count = 0;
      if (start + count > curr_total)
      {
        count = curr_total - start;
      }

      const std::vector<std::string> window = materialize_lines(prev.old_lines);
      buf.replace_lines(start, count, window);
    }

    buf.cursor = prev.cursor;
    buf.preferred_x = prev.preferred_x;
    buf.selection = prev.selection;
    buf.extra_carets = prev.extra_carets;
    buf.scroll_offset = std::max(0, prev.scroll_offset);
    buf.scroll_x = std::max(0, prev.scroll_x);
    buf.modified = prev.modified;
    buf.is_placeholder = prev.is_placeholder;
  }
} // namespace

void Editor::save_state()
{
  search.clear_results();

  auto &buf = get_buffer();
  // Anchor the bracket-depth prefix invalidation at the first line the
  // pending command can touch (cursor, or the selection span when one is
  // active). Prefix entries below this stay valid, so typing deep in a file
  // only re-scans the affected tail instead of the whole prefix.
  int edit_anchor = buf.cursor.y;
  if (buf.selection.active)
  {
    edit_anchor = std::min(edit_anchor, std::min(buf.selection.start.y, buf.selection.end.y));
  }
  for (const auto &caret : buf.extra_carets)
  {
    edit_anchor =
        std::min(edit_anchor, caret.active ? std::min(caret.start.y, caret.end.y) : caret.start.y);
  }
  buf.mark_edited(edit_anchor);

  // Keep the tree-sitter tree alive across edits and reparse incrementally on
  // the next paint: deleting it here forced a whole-file parse per keystroke.
#ifdef JOT_TREESITTER
  ts_begin_edit(buf);
#endif
  decoration_rebase_begin(buf);

  if (buf.is_preview)
  {
    buf.is_preview = false;
    if (preview_buffer_index == current_buffer)
    {
      preview_buffer_index = -1;
    }
  }

  // The snapshot shares its unchanged lines with the state it is pushed on
  // top of, so it is taken against the live top of the stack.
  const State *prev = buf.undo_stack.empty() ? nullptr : &buf.undo_stack.top();
  State s = capture_state(buf, prev);
  if (prev != nullptr && same_state(buf, *prev, s))
  {
    return;
  }

  push_state(buf.undo_stack, buf.undo_stack_bytes, std::move(s), prev);
  // A new edit forks the history: whatever the redo branch held is released.
  while (!buf.redo_stack.empty())
  {
    subtract_bytes(buf.redo_stack_bytes, release_cost(buf.redo_stack.top()));
    buf.redo_stack.pop();
  }
}

void Editor::undo()
{
  if (!edit_allowed())
    return;
  auto &buf = get_buffer();

  // A command that ends up changing nothing still takes its snapshot before it
  // runs (save_state cannot know the outcome), and that snapshot describes the
  // buffer as it already is. Undoing onto such a state changes nothing at all,
  // which reads as a dead keypress, so the states are dropped here and the
  // press reaches the previous real edit. A state whose restore would move the
  // caret, the selection or the view is a real step and is left alone.
  // The redo stack needs no such pass: its states are captured from the live
  // buffer by undo() itself, after this loop, so by construction they differ
  // from what the restore leaves behind.
  while (!buf.undo_stack.empty() && state_matches_buffer(buf, buf.undo_stack.top()))
  {
    pop_state(buf.undo_stack, buf.undo_stack_bytes);
  }
  if (buf.undo_stack.empty())
  {
    return;
  }

  const State *redo_prev = buf.redo_stack.empty() ? nullptr : &buf.redo_stack.top();
  State redo_delta = capture_state(buf, redo_prev);
  push_state(buf.redo_stack, buf.redo_stack_bytes, std::move(redo_delta), redo_prev);

  State prev = pop_state(buf.undo_stack, buf.undo_stack_bytes);

#ifdef JOT_TREESITTER
  ts_begin_edit(buf);
#endif
  decoration_rebase_begin(buf);
  apply_state(buf, prev);
  // Full-snapshot restores replace buf.lines directly and would otherwise skip
  // mark_edited (stale fold ranges / tree-sitter line offsets / bracket-depth
  // prefix); incremental undo/redo restores went through replace_lines,
  // which already anchors at prev.start_line. Anchoring at 0 here is safe:
  // undo is discrete, so the prefix re-extends once on the next render.
  buf.mark_edited(prev.full_snapshot ? 0 : prev.start_line);

  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}

void Editor::redo()
{
  if (!edit_allowed())
    return;
  auto &buf = get_buffer();
  if (buf.redo_stack.empty())
  {
    return;
  }

  const State *undo_prev = buf.undo_stack.empty() ? nullptr : &buf.undo_stack.top();
  State undo_delta = capture_state(buf, undo_prev);
  push_state(buf.undo_stack, buf.undo_stack_bytes, std::move(undo_delta), undo_prev);

  State next = pop_state(buf.redo_stack, buf.redo_stack_bytes);

#ifdef JOT_TREESITTER
  ts_begin_edit(buf);
#endif
  decoration_rebase_begin(buf);
  apply_state(buf, next);
  buf.mark_edited(next.full_snapshot ? 0 : next.start_line);

  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
}
