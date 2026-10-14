#include "core/editor.h"
#include "lua/api/api.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
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

  // Whether a state holds the same text the buffer does now. Only the lines the
  // state covers are compared: the whole file for a full snapshot, the window
  // around the carets for a delta one.
  bool same_content(const FileBuffer &buf, const State &s)
  {
    if (s.old_total_lines != (int)buf.line_count())
    {
      return false;
    }
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

  // What capturing `s` costs the history: its own line-pointer array plus the
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

  // The state a capture shares lines with: the snapshot the command took, which
  // came from this very text, or the closest node behind the one in front.
  const State *share_source(const FileBuffer &buf)
  {
    if (buf.undo_pending_valid)
    {
      return &buf.undo_pending;
    }
    for (int n = buf.undo_current; n >= 0; n = buf.undo_nodes[(size_t)n].parent)
    {
      if (buf.undo_nodes[(size_t)n].state_known)
      {
        return &buf.undo_nodes[(size_t)n].state;
      }
    }
    return nullptr;
  }

  // The state behind `node`, which is what push_cost measures sharing against.
  const State *behind_state(const FileBuffer &buf, int node)
  {
    if (node < 0)
    {
      return nullptr;
    }
    const int parent = buf.undo_nodes[(size_t)node].parent;
    if (parent >= 0 && buf.undo_nodes[(size_t)parent].state_known)
    {
      return &buf.undo_nodes[(size_t)parent].state;
    }
    return nullptr;
  }

  int create_node(FileBuffer &buf, int parent)
  {
    FileBuffer::UndoNode node;
    node.parent = parent;
    node.seq = ++buf.undo_seq_last;
    node.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    buf.undo_nodes.push_back(std::move(node));
    return (int)buf.undo_nodes.size() - 1;
  }

  // Stamps a state as reached with its own text, charging the history for it.
  void store_state(FileBuffer &buf, int node, State state, const State *prev)
  {
    FileBuffer::UndoNode &target = buf.undo_nodes[(size_t)node];
    buf.undo_tree_bytes += push_cost(state, prev);
    target.state = std::move(state);
    target.state_known = true;
  }

  bool node_on_path(const FileBuffer &buf, int node, int ancestor)
  {
    for (int n = node; n >= 0; n = buf.undo_nodes[(size_t)n].parent)
    {
      if (n == ancestor)
      {
        return true;
      }
    }
    return false;
  }

  int find_node(const FileBuffer &buf, int seq)
  {
    for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
    {
      if (buf.undo_nodes[(size_t)i].seq == seq)
      {
        return i;
      }
    }
    return -1;
  }

  void drop_nodes(FileBuffer &buf, const std::vector<char> &drop)
  {
    std::vector<int> remap(buf.undo_nodes.size(), -1);
    std::vector<FileBuffer::UndoNode> kept;
    kept.reserve(buf.undo_nodes.size());
    for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
    {
      if (drop[(size_t)i])
      {
        subtract_bytes(buf.undo_tree_bytes, release_cost(buf.undo_nodes[(size_t)i].state));
        continue;
      }
      remap[(size_t)i] = (int)kept.size();
      kept.push_back(std::move(buf.undo_nodes[(size_t)i]));
    }
    for (FileBuffer::UndoNode &node : kept)
    {
      // A node whose parent was dropped becomes a root: what undo stops at.
      node.parent = node.parent >= 0 ? remap[(size_t)node.parent] : -1;
    }
    buf.undo_current = buf.undo_current >= 0 ? remap[(size_t)buf.undo_current] : -1;
    buf.undo_nodes = std::move(kept);
  }

  // Drops states until the tree fits the state cap and the byte budget, never
  // below kMinUndoStates. A branch nothing can walk to any more goes first, and
  // when only the path in front is left its oldest end goes instead, so the
  // states a user reaches for by undoing are the ones that stay.
  void trim_undo_tree(FileBuffer &buf)
  {
    while (((int)buf.undo_nodes.size() > kMaxUndoHistory || buf.undo_tree_bytes > kUndoHistoryBytes)
           && (int)buf.undo_nodes.size() > kMinUndoStates)
    {
      int victim = -1;
      for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
      {
        if (node_on_path(buf, buf.undo_current, i))
        {
          continue;
        }
        if (victim < 0 || buf.undo_nodes[(size_t)i].seq < buf.undo_nodes[(size_t)victim].seq)
        {
          victim = i;
        }
      }
      std::vector<char> drop(buf.undo_nodes.size(), 0);
      if (victim >= 0)
      {
        for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
        {
          if (node_on_path(buf, i, victim))
          {
            drop[(size_t)i] = 1;
          }
        }
      }
      else
      {
        int oldest = buf.undo_current;
        for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
        {
          if (buf.undo_nodes[(size_t)i].seq < buf.undo_nodes[(size_t)oldest].seq)
          {
            oldest = i;
          }
        }
        if (oldest == buf.undo_current)
        {
          break;
        }
        drop[(size_t)oldest] = 1;
      }
      drop_nodes(buf, drop);
    }
  }

  // The lines a restore replaces, and with what: the window a delta state
  // covers, or every line for a full snapshot. A window is anchored on the
  // capture's own line count, so the count is corrected for what the buffer
  // holds now.
  struct Patch
  {
    int start = 0;
    int count = 0;
    std::vector<std::string> lines;
  };

  Patch patch_for(const State &s, int curr_total)
  {
    Patch patch;
    if (s.full_snapshot)
    {
      patch.start = 0;
      patch.count = curr_total;
      patch.lines = materialize_lines(s.old_lines);
      if (patch.lines.empty())
      {
        patch.lines.push_back("");
      }
      return patch;
    }
    const int line_diff = curr_total - s.old_total_lines;
    int start = s.start_line;
    int count = (int)s.old_lines.size() + line_diff;
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
    {
      count = 0;
    }
    if (start + count > curr_total)
    {
      count = curr_total - start;
    }
    patch.start = start;
    patch.count = count;
    patch.lines = materialize_lines(s.old_lines);
    return patch;
  }

  void apply_state(FileBuffer &buf, const State &prev)
  {
    const Patch patch = patch_for(prev, (int)buf.line_count());
    if (prev.full_snapshot)
    {
      if (buf.is_lazy())
      {
        buf.lazy_provider->set_all_lines(patch.lines);
        buf.lines.clear();
      }
      else
      {
        buf.lines = patch.lines;
      }
    }
    else
    {
      buf.replace_lines(patch.start, patch.count, patch.lines);
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

  // The states to restore, in order, to stand on `target` instead of `from`:
  // the ones above the shared fork, then the ones below it on the other side.
  // Empty when there is nowhere to walk.
  std::vector<int> path_between(const FileBuffer &buf, int from, int target)
  {
    std::vector<int> from_chain, target_chain;
    for (int n = from; n >= 0; n = buf.undo_nodes[(size_t)n].parent)
    {
      from_chain.push_back(n);
    }
    for (int n = target; n >= 0; n = buf.undo_nodes[(size_t)n].parent)
    {
      target_chain.push_back(n);
    }
    while (!from_chain.empty() && !target_chain.empty() && from_chain.back() == target_chain.back())
    {
      from_chain.pop_back();
      target_chain.pop_back();
    }
    std::vector<int> enter;
    enter.reserve(from_chain.size() + target_chain.size());
    for (int n : from_chain)
    {
      const int parent = buf.undo_nodes[(size_t)n].parent;
      if (parent < 0)
      {
        break;
      }
      enter.push_back(parent);
    }
    for (auto it = target_chain.rbegin(); it != target_chain.rend(); ++it)
    {
      enter.push_back(*it);
    }
    return enter;
  }

  // The same walk on a copy of the text, for the panel's preview: the buffer
  // stays where it is while a state is looked at.
  void patch_lines(std::vector<std::string> &lines, const State &s)
  {
    Patch patch = patch_for(s, (int)lines.size());
    std::vector<std::string> spliced;
    spliced.reserve((size_t)patch.start + patch.lines.size()
                    + (lines.size() > (size_t)(patch.start + patch.count)
                           ? lines.size() - (size_t)(patch.start + patch.count)
                           : 0));
    for (int i = 0; i < patch.start; i++)
    {
      spliced.push_back(std::move(lines[(size_t)i]));
    }
    for (std::string &line : patch.lines)
    {
      spliced.push_back(std::move(line));
    }
    for (std::size_t i = (std::size_t)(patch.start + patch.count); i < lines.size(); i++)
    {
      spliced.push_back(std::move(lines[i]));
    }
    lines = std::move(spliced);
  }
} // namespace

// The node in front holds the live buffer, so its content is captured the
// moment something else becomes the state to go back to. Every other node
// carries the text it reached.
void Editor::undo_reconcile(FileBuffer &buf)
{
  if (!buf.undo_pending_valid)
  {
    return;
  }
  // The edit generation is the cheap answer for the paths that bump it, and the
  // comparison is the one that catches the rest: setting a buffer's content
  // replaces the lines without going through mark_edited, and a command that
  // ends where it started has to add no state at all.
  const bool changed = buf.edit_generation != buf.undo_pending_generation
                       || !same_content(buf, buf.undo_pending);
  if (!changed)
  {
    buf.undo_pending = State();
    buf.undo_pending_valid = false;
    return;
  }

  if (buf.undo_current < 0)
  {
    // The first change: the snapshot is the file as it was, and it roots the
    // tree everything after it grows from.
    const int root = create_node(buf, -1);
    store_state(buf, root, std::move(buf.undo_pending), nullptr);
    buf.undo_current = root;
  }
  else
  {
    FileBuffer::UndoNode &outgoing = buf.undo_nodes[(size_t)buf.undo_current];
    if (!outgoing.state_known)
    {
      // The snapshot was taken from the text this node holds, so it is this
      // node's own content, and the node is about to stop being the one in
      // front.
      State state = std::move(buf.undo_pending);
      const std::size_t bytes = push_cost(state, behind_state(buf, buf.undo_current));
      outgoing.state = std::move(state);
      outgoing.state_known = true;
      buf.undo_tree_bytes += bytes;
    }
  }
  buf.undo_pending = State();
  buf.undo_pending_valid = false;
  buf.undo_current = create_node(buf, buf.undo_current);
  trim_undo_tree(buf);
}

void Editor::save_state()
{
  auto &buf = get_buffer();
  // A preview stops being one the moment it is edited (typing into the picker's
  // buffer makes it a real one). The flag and the pane's record of it live on
  // the buffer in front, so the bookkeeping stays here rather than in
  // save_state_for, which also serves buffers no pane is showing.
  if (buf.is_preview)
  {
    buf.is_preview = false;
    if (preview_buffer_index == current_buffer)
    {
      preview_buffer_index = -1;
    }
  }
  save_state_for(buf);
}

void Editor::save_state_for(FileBuffer &buf, bool as_modified)
{
  search.clear_results();

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

  // The command has not run yet, so this is the text of the state it starts
  // from: the snapshot is kept and turned into a node once the buffer says the
  // command changed something (undo_reconcile).
  undo_reconcile(buf);
  const State *prev = share_source(buf);
  State s = capture_state(buf, prev);
  if (as_modified)
  {
    s.modified = true;
  }
  buf.undo_pending = std::move(s);
  buf.undo_pending_generation = buf.edit_generation;
  buf.undo_pending_valid = true;
}

// Walks the buffer to `target`, one state at a time. Each step is the restore
// an undo or a redo performs, so a jump over several states lands on the same
// text walking there would.
bool Editor::undo_walk_to(FileBuffer &buf, int target)
{
  if (target < 0 || target >= (int)buf.undo_nodes.size() || target == buf.undo_current)
  {
    return false;
  }
  if (!buf.undo_nodes[(size_t)target].state_known)
  {
    return false;
  }
  const std::vector<int> enter = path_between(buf, buf.undo_current, target);
  if (enter.empty())
  {
    return false;
  }

  const int from = buf.undo_current;
  if (from >= 0 && !buf.undo_nodes[(size_t)from].state_known)
  {
    State state = capture_state(buf, share_source(buf));
    const std::size_t bytes = push_cost(state, behind_state(buf, from));
    buf.undo_nodes[(size_t)from].state = std::move(state);
    buf.undo_nodes[(size_t)from].state_known = true;
    buf.undo_tree_bytes += bytes;
  }

#ifdef JOT_TREESITTER
  ts_begin_edit(buf);
#endif
  decoration_rebase_begin(buf);
  for (int node : enter)
  {
    if (!buf.undo_nodes[(size_t)node].state_known)
    {
      continue;
    }
    apply_state(buf, buf.undo_nodes[(size_t)node].state);
  }
  buf.undo_current = target;
  buf.mark_edited(0);

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
  trim_undo_tree(buf);
  needs_redraw = true;
  return true;
}

void Editor::undo()
{
  if (!edit_allowed())
  {
    return;
  }
  auto &buf = get_buffer();
  undo_reconcile(buf);
  if (buf.undo_current < 0)
  {
    return;
  }

  // A command that ends where it started still gets a node, and undoing onto it
  // would leave the text alone: the walk skips those, so one press reaches the
  // previous real edit. The oldest state kept is never skipped, or the press
  // would have nowhere to land.
  int target = buf.undo_nodes[(size_t)buf.undo_current].parent;
  while (target >= 0 && buf.undo_nodes[(size_t)target].parent >= 0
         && buf.undo_nodes[(size_t)target].state_known
         && same_content(buf, buf.undo_nodes[(size_t)target].state))
  {
    target = buf.undo_nodes[(size_t)target].parent;
  }
  if (target < 0)
  {
    return;
  }
  undo_walk_to(buf, target);
}

void Editor::redo()
{
  if (!edit_allowed())
  {
    return;
  }
  auto &buf = get_buffer();
  undo_reconcile(buf);
  if (buf.undo_current < 0)
  {
    return;
  }

  // Redo takes the newest child: where the last edit on this state went, not
  // the first branch it grew.
  int child = -1;
  for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
  {
    if (buf.undo_nodes[(size_t)i].parent != buf.undo_current)
    {
      continue;
    }
    if (child < 0 || buf.undo_nodes[(size_t)i].seq > buf.undo_nodes[(size_t)child].seq)
    {
      child = i;
    }
  }
  if (child < 0)
  {
    return;
  }
  undo_walk_to(buf, child);
}

bool Editor::undo_tree_goto(int seq)
{
  if (!edit_allowed())
  {
    return false;
  }
  auto &buf = get_buffer();
  undo_reconcile(buf);
  const int target = find_node(buf, seq);
  if (target < 0 || target == buf.undo_current)
  {
    return false;
  }
  return undo_walk_to(buf, target);
}

bool Editor::undo_state_lines(FileBuffer &buf, int seq, std::vector<std::string> &out)
{
  undo_reconcile(buf);
  const int target = find_node(buf, seq);
  if (target < 0 || buf.is_lazy())
  {
    return false;
  }

  const int lines_total = (int)buf.line_count();
  out.clear();
  out.reserve((size_t)lines_total);
  for (int i = 0; i < lines_total; i++)
  {
    out.push_back(buf.line(i));
  }
  if (target == buf.undo_current)
  {
    // The state in front is the buffer itself; nothing to walk.
    return true;
  }

  const std::vector<int> enter = path_between(buf, buf.undo_current, target);
  if (enter.empty())
  {
    return false;
  }
  for (int node : enter)
  {
    if (!buf.undo_nodes[(size_t)node].state_known)
    {
      return false;
    }
    patch_lines(out, buf.undo_nodes[(size_t)node].state);
  }
  return true;
}

void Editor::undo_mark_saved(FileBuffer &buf)
{
  undo_reconcile(buf);
  ++buf.undo_writes;
  if (buf.undo_current >= 0 && buf.undo_current < (int)buf.undo_nodes.size())
  {
    buf.undo_nodes[(size_t)buf.undo_current].saved_at = buf.undo_writes;
  }
}
