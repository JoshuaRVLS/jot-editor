// Tree-sitter textobjects: expand/shrink the selection to a syntax node, select
// the inside/around of a function, class or argument, and jump between functions.
//
// Helix's signature feature, and jot had the parsers but never asked them
// anything: highlights were the only consumer of the tree. The walking here is
// deliberately stateless -- expand looks for the smallest node that strictly
// contains the selection, shrink for the largest node strictly inside it -- so
// there is no stack to keep in sync with edits, cursor moves or undo.
#include "editor.h"
#include "features/textobjects.h"

#include <algorithm>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace
{
#ifdef JOT_TREESITTER
  // Byte offset of a cursor position, via the buffer's lazily built line table
  // (the same invariant syntax.cpp uses: offsets[i] is where line i starts, and
  // every edit clears it).
  uint32_t byte_for_cursor(FileBuffer &buf, Cursor cursor)
  {
    auto &offsets = buf.ts_line_offsets;
    const int line = std::clamp(cursor.y, 0, std::max(0, (int)buf.line_count() - 1));
    while ((int)offsets.size() <= line)
    {
      const int next = (int)offsets.size();
      if (next == 0)
      {
        offsets.push_back(0);
        continue;
      }
      offsets.push_back(offsets[(size_t)next - 1] + (uint32_t)buf.line(next - 1).size() + 1);
    }
    const uint32_t start = offsets.empty() ? 0 : offsets[(size_t)line];
    const int col = std::clamp(cursor.x, 0, (int)buf.line(line).size());
    return start + (uint32_t)col;
  }

  // Inverse of byte_for_cursor: the cursor that points at `byte`.
  Cursor cursor_for_byte(FileBuffer &buf, uint32_t byte)
  {
    auto &offsets = buf.ts_line_offsets;
    while ((int)offsets.size() < buf.line_count())
    {
      const int next = (int)offsets.size();
      if (next == 0)
      {
        offsets.push_back(0);
        continue;
      }
      offsets.push_back(offsets[(size_t)next - 1] + (uint32_t)buf.line(next - 1).size() + 1);
    }
    int line = 0;
    for (int i = 0; i < (int)offsets.size(); i++)
    {
      if (offsets[(size_t)i] <= byte)
      {
        line = i;
      }
      else
      {
        break;
      }
    }
    const std::string &text = buf.line(line);
    const uint32_t start = offsets.empty() ? 0 : offsets[(size_t)line];
    int col = (int)(byte - start);
    col = std::clamp(col, 0, (int)text.size());
    return {col, line};
  }

  bool node_range(TSNode node, uint32_t &start, uint32_t &end)
  {
    if (ts_node_is_null(node))
    {
      return false;
    }
    start = ts_node_start_byte(node);
    end = ts_node_end_byte(node);
    return end > start;
  }

  // True when `type` is the requested kind of syntax object.
  bool type_matches(const jot_textobjects::Names &names,
                    const std::string &kind,
                    const std::string &type)
  {
    if (kind == "function")
    {
      return names.is_function(type);
    }
    if (kind == "class")
    {
      return names.is_class(type);
    }
    if (kind == "argument")
    {
      return names.is_argument(type);
    }
    if (kind == "comment")
    {
      return names.is_comment(type);
    }
    return false;
  }

  // C and C++ wrap the identifier in a function_declarator inside the
  // function_definition, so the innermost match is the declarator: climb while
  // the parent is the same kind of object to land on the definition itself. Both
  // are in the table, and the definitions are what a user means by "the
  // function".
  //
  // Only for functions: the other kinds have no such wrapper, and climbing would
  // merge an item with its container (a parameter with its parameter_list).
  TSNode climb_same_kind(TSNode node, const jot_textobjects::Names &names, const std::string &kind)
  {
    for (;;)
    {
      const TSNode parent = ts_node_parent(node);
      if (ts_node_is_null(parent) || !type_matches(names, kind, ts_node_type(parent)))
      {
        return node;
      }
      node = parent;
    }
  }

  // The named node covering a byte, or the deepest one containing the range.
  TSNode node_for_range(TSNode root, uint32_t start, uint32_t end)
  {
    if (end > start)
    {
      return ts_node_named_descendant_for_byte_range(root, start, end);
    }
    return ts_node_named_descendant_for_byte_range(root, start, start);
  }
#endif
} // namespace

#ifdef JOT_TREESITTER
// Selects `start`..`end` (byte offsets) as the primary selection, leaving any
// extra carets alone: textobjects act on the primary selection, the way helix's
// do on every selection at once but without needing the multi-caret bookkeeping
// to be rewritten for it.
bool Editor::select_byte_range(uint32_t start, uint32_t end)
{
  auto &buf = get_buffer();
  const Cursor from = cursor_for_byte(buf, start);
  const Cursor to = cursor_for_byte(buf, end);
  buf.selection.start = from;
  buf.selection.end = to;
  buf.selection.active = end > start;
  buf.cursor = to;
  buf.preferred_x = to.x;
  restart_blink();
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

bool Editor::syntax_tree_ready(FileBuffer &buf)
{
  if (buf.filepath.empty())
  {
    set_message("No file to take a syntax object from");
    return false;
  }
  // The tree is built on first highlight and can be pending for large files; ask
  // for it, then refuse rather than guess.
  init_ts_for_buffer(buf);
  if (!buf.ts_tree || !buf.ts_tree_in_sync)
  {
    set_message(buf.ts_parse_pending ? "Syntax tree still parsing"
                                     : "No syntax tree for this file");
    return false;
  }
  return true;
}
#endif

bool Editor::expand_selection_to_node()
{
#ifdef JOT_TREESITTER
  auto &buf = get_buffer();
  if (!syntax_tree_ready(buf))
  {
    return false;
  }
  Cursor from = buf.cursor;
  Cursor to = buf.cursor;
  if (buf.selection.active)
  {
    from = buf.selection.start;
    to = buf.selection.end;
    if (from.y > to.y || (from.y == to.y && from.x > to.x))
    {
      std::swap(from, to);
    }
  }
  const uint32_t sel_start = byte_for_cursor(buf, from);
  const uint32_t sel_end = byte_for_cursor(buf, to);

  TSNode node = ts_tree_root_node(buf.ts_tree);
  uint32_t start = 0;
  uint32_t end = 0;
  if (sel_end > sel_start)
  {
    node = node_for_range(node, sel_start, sel_end);
  }
  else
  {
    node = node_for_range(node, sel_start, sel_start);
  }
  // Walk out to the first node that is strictly bigger than what is selected.
  while (!ts_node_is_null(node)
         && (!node_range(node, start, end) || (start == sel_start && end == sel_end)))
  {
    node = ts_node_parent(node);
  }
  if (ts_node_is_null(node))
  {
    set_message("Nothing bigger to select");
    return false;
  }
  if (!node_range(node, start, end))
  {
    return false;
  }
  if (start == sel_start && end == sel_end)
  {
    set_message("Selection is the whole node");
    return false;
  }
  return select_byte_range(start, end);
#else
  set_message("Built without tree-sitter");
  return false;
#endif
}

bool Editor::shrink_selection_to_node()
{
#ifdef JOT_TREESITTER
  auto &buf = get_buffer();
  if (!syntax_tree_ready(buf))
  {
    return false;
  }
  if (!buf.selection.active)
  {
    set_message("Nothing to shrink");
    return false;
  }
  Cursor from = buf.selection.start;
  Cursor to = buf.selection.end;
  if (from.y > to.y || (from.y == to.y && from.x > to.x))
  {
    std::swap(from, to);
  }
  const uint32_t sel_start = byte_for_cursor(buf, from);
  const uint32_t sel_end = byte_for_cursor(buf, to);

  // One level in, toward the cursor: the immediate child that still contains it
  // and stays inside the selection. Stepping straight to the deepest node would
  // skip levels, and a walk that starts at the root has to be careful to skip the
  // node equal to the selection rather than settle on it (that bug selected the
  // whole file).
  const uint32_t at = byte_for_cursor(buf, buf.cursor);
  TSNode node = node_for_range(ts_tree_root_node(buf.ts_tree), sel_start, sel_end);
  uint32_t node_start = 0;
  uint32_t node_end = 0;
  if (!node_range(node, node_start, node_end))
  {
    set_message("No smaller node inside the selection");
    return false;
  }
  TSNode next = {};
  uint32_t next_start = 0;
  uint32_t next_end = 0;
  bool have_next = false;
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; i++)
  {
    const TSNode child = ts_node_named_child(node, i);
    uint32_t cs = 0;
    uint32_t ce = 0;
    if (!node_range(child, cs, ce))
    {
      continue;
    }
    if (cs < sel_start || ce > sel_end)
    {
      continue; // outside what is selected: shrinking must not grow the selection
    }
    if (cs <= at && at <= ce)
    {
      next = child;
      next_start = cs;
      next_end = ce;
      have_next = true;
      break;
    }
  }
  if (!have_next || (next_start == sel_start && next_end == sel_end))
  {
    set_message("No smaller node inside the selection");
    return false;
  }
  return select_byte_range(next_start, next_end);
#else
  set_message("Built without tree-sitter");
  return false;
#endif
}

bool Editor::select_textobject(const std::string &kind, bool inner)
{
#ifdef JOT_TREESITTER
  auto &buf = get_buffer();
  if (!syntax_tree_ready(buf))
  {
    return false;
  }
  const jot_textobjects::Names names =
      jot_textobjects::names_for_extension(tree_sitter_extension_for_buffer(buf));
  if (names.empty())
  {
    set_message("No syntax objects for this file type");
    return false;
  }
  const uint32_t at = byte_for_cursor(buf, buf.cursor);
  // The selection as byte offsets, so "is this already selected?" is a range
  // comparison rather than a guess.
  uint32_t sel_start = at;
  uint32_t sel_end = at;
  if (buf.selection.active)
  {
    Cursor from = buf.selection.start;
    Cursor to = buf.selection.end;
    if (from.y > to.y || (from.y == to.y && from.x > to.x))
    {
      std::swap(from, to);
    }
    sel_start = byte_for_cursor(buf, from);
    sel_end = byte_for_cursor(buf, to);
  }

  TSNode node = node_for_range(ts_tree_root_node(buf.ts_tree), at, at);
  while (!ts_node_is_null(node))
  {
    if (type_matches(names, kind, ts_node_type(node)))
    {
      if (kind == "function")
      {
        node = climb_same_kind(node, names, kind);
      }
      // What this command would select for that node: "inside" is the node's own
      // contents (a body for a function or class, the declared name for a
      // parameter), "around" is the node itself. Nothing to take inside means the
      // node is already the smallest useful thing.
      uint32_t start = 0;
      uint32_t end = 0;
      bool have_range = false;
      if (inner)
      {
        const auto &fields = kind == "argument" ? jot_textobjects::argument_inner_fields()
                                                : jot_textobjects::body_fields();
        for (const auto &field : fields)
        {
          const TSNode child =
              ts_node_child_by_field_name(node, field.c_str(), (uint32_t)field.size());
          if (node_range(child, start, end))
          {
            have_range = true;
            break;
          }
        }
      }
      if (!have_range && node_range(node, start, end))
      {
        have_range = true;
      }
      if (!have_range)
      {
        return false;
      }
      // Already selected: a second press walks out to the next object of the same
      // kind instead of repeating itself. (Checked against the range this command
      // would produce, so "inside" after "around" still works.)
      if (buf.selection.active && start == sel_start && end == sel_end)
      {
        node = ts_node_parent(node);
        continue;
      }
      return select_byte_range(start, end);
    }
    node = ts_node_parent(node);
  }
  set_message("No " + kind + " here");
  return false;
#else
  (void)kind;
  (void)inner;
  set_message("Built without tree-sitter");
  return false;
#endif
}

bool Editor::goto_relative_function(int direction)
{
  return goto_relative_object("function", direction);
}

bool Editor::goto_relative_object(const std::string &kind, int direction)
{
#ifdef JOT_TREESITTER
  auto &buf = get_buffer();
  if (!syntax_tree_ready(buf))
  {
    return false;
  }
  const jot_textobjects::Names names =
      jot_textobjects::names_for_extension(tree_sitter_extension_for_buffer(buf));
  const uint32_t from = byte_for_cursor(buf, buf.cursor);

  // A flat scan of the whole tree, in document order, filtering to the kind asked
  // for: cheap enough to run on a keystroke and it cannot miss a definition
  // nested in a class or a namespace the way a sibling walk would.
  TSNode found = {};
  bool have_found = false;
  std::vector<TSNode> stack{ts_tree_root_node(buf.ts_tree)};
  while (!stack.empty())
  {
    const TSNode node = stack.back();
    stack.pop_back();
    uint32_t start = 0;
    uint32_t end = 0;
    if (node_range(node, start, end) && type_matches(names, kind, ts_node_type(node)))
    {
      // The declarator/definition trap again: "int add(...)" starts four columns
      // in, so land on the enclosing definition when there is one.
      const TSNode outer = kind == "function" ? climb_same_kind(node, names, kind) : node;
      start = ts_node_start_byte(outer);
      end = ts_node_end_byte(outer);
      const bool after = start > from;
      if ((direction > 0 && after) || (direction < 0 && start < from))
      {
        if (!have_found || (direction > 0 && start < ts_node_start_byte(found))
            || (direction < 0 && start > ts_node_start_byte(found)))
        {
          found = outer;
          have_found = true;
        }
      }
    }
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = count; i > 0; i--)
    {
      stack.push_back(ts_node_named_child(node, i - 1));
    }
  }
  if (!have_found)
  {
    set_message(direction > 0 ? "Nothing after the cursor" : "Nothing before the cursor");
    return false;
  }
  const uint32_t start = ts_node_start_byte(found);
  return select_byte_range(start, start);
#else
  (void)kind;
  (void)direction;
  set_message("Built without tree-sitter");
  return false;
#endif
}
