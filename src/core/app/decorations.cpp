// Anchored decorations (extmark-style). Every decoration carries a (row, col)
// position that must survive edits: inserting text before it shifts it,
// deleting the text it sits on collapses it, and so on. Instead of hooking
// every line-mutation site, the editor snapshots the full text before each
// edit (decoration_rebase_begin, called next to the tree-sitter edit hooks)
// and lazily re-anchors by diffing the snapshot against the current text:
// common prefix/suffix over the two versions yields the single edit window,
// and every decoration is pushed through that window's transform. The render
// loop only pays for this when decoration_dirty is set, i.e. once per edit.
#include "editor.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace
{
  // Read-side theme resolution for decoration highlight groups. Mirrors the
  // write-side slot chain in api_theme.cpp (set_theme_color) for the groups
  // decorations actually use; unknown names return false and callers fall
  // back to their explicit fg/bg or the theme defaults.
  bool theme_slot(const Theme &theme, const std::string &name, int &fg, int &bg)
  {
    static const std::unordered_map<std::string, std::pair<int Theme::*, int Theme::*>> slots = {
        {"normal", {&Theme::fg_default, &Theme::bg_default}},
        {"default", {&Theme::fg_default, &Theme::bg_default}},
        {"comment", {&Theme::fg_comment, &Theme::bg_comment}},
        {"keyword", {&Theme::fg_keyword, &Theme::bg_keyword}},
        {"string", {&Theme::fg_string, &Theme::bg_string}},
        {"number", {&Theme::fg_number, &Theme::bg_number}},
        {"function", {&Theme::fg_function, &Theme::bg_function}},
        {"type", {&Theme::fg_type, &Theme::bg_type}},
        {"variable", {&Theme::fg_variable, &Theme::bg_variable}},
        {"constant", {&Theme::fg_constant, &Theme::bg_constant}},
        {"builtin", {&Theme::fg_builtin, &Theme::bg_builtin}},
        {"operator", {&Theme::fg_operator, &Theme::bg_operator}},
        {"punctuation", {&Theme::fg_punctuation, &Theme::bg_punctuation}},
        {"diagnostic_error", {&Theme::fg_diagnostic_error, nullptr}},
        {"diagnostic_warning", {&Theme::fg_diagnostic_warning, nullptr}},
        {"diagnostic_info", {&Theme::fg_diagnostic_info, nullptr}},
        {"diagnostic_hint", {&Theme::fg_diagnostic_hint, nullptr}},
        {"search_match", {&Theme::fg_search_match, &Theme::bg_search_match}},
        {"selection", {&Theme::fg_selection, &Theme::bg_selection}},
        {"bracket_match", {&Theme::fg_bracket_match, &Theme::bg_bracket_match}},
        {"status", {&Theme::fg_status, &Theme::bg_status}},
        {"status_error", {&Theme::fg_status_error, nullptr}},
        {"status_warning", {&Theme::fg_status_warning, nullptr}},
        {"status_info", {&Theme::fg_status_info, nullptr}},
        {"status_muted", {&Theme::fg_status_muted, &Theme::bg_status_muted}},
        {"line_num", {&Theme::fg_line_num, &Theme::bg_line_num}},
        {"command", {&Theme::fg_command, &Theme::bg_command}},
        {"panel_border", {&Theme::fg_panel_border, &Theme::bg_panel_border}},
        {"active_border", {&Theme::fg_active_border, &Theme::bg_active_border}},
    };
    auto it = slots.find(name);
    if (it == slots.end())
    {
      return false;
    }
    fg = it->second.first ? theme.*(it->second.first) : -1;
    bg = it->second.second ? theme.*(it->second.second) : -1;
    return true;
  }
} // namespace

namespace
{
  bool decoration_less(const Decoration &a, const Decoration &b)
  {
    if (a.row != b.row)
      return a.row < b.row;
    if (a.col != b.col)
      return a.col < b.col;
    if (a.priority != b.priority)
      return a.priority > b.priority;
    return a.id < b.id;
  }

  // Line-start byte offsets so (row, col) <-> global byte offset conversions
  // stay O(log lines) per decoration.
  std::vector<size_t> line_starts(const std::string &text)
  {
    std::vector<size_t> starts;
    starts.reserve(text.size() / 16 + 2);
    starts.push_back(0);
    for (size_t i = 0; i < text.size(); i++)
    {
      if (text[i] == '\n')
      {
        starts.push_back(i + 1);
      }
    }
    return starts;
  }

  std::pair<int, int> offset_to_rowcol(const std::string &text,
                                       const std::vector<size_t> &starts,
                                       size_t offset)
  {
    auto it = std::upper_bound(starts.begin(), starts.end(), offset);
    size_t row = (size_t)(it - starts.begin()) - 1;
    return {(int)row, (int)(offset - starts[row])};
  }
} // namespace

// Pure function, unit-testable without an Editor: shifts every decoration
// from `base`-space into `current`-space assuming at most one contiguous edit
// between them (exactly what the rebase hooks guarantee).
//
// Semantics match the usual anchored-mark behavior: marks strictly before the
// edit keep their byte offset; marks strictly after it keep their offset
// relative to the (identical) suffix, i.e. shift by the byte delta; marks
// inside the replaced window collapse to the window start (left gravity) or
// end (right gravity), and to the start when the edit deleted everything in
// the window. A mark exactly at the window start follows gravity.
void decoration_anchor_transform(std::vector<Decoration> &decorations,
                                 const std::string &base,
                                 const std::string &current)
{
  if (decorations.empty())
    return;
  const size_t common = std::min(base.size(), current.size());
  size_t prefix = 0;
  while (prefix < common && base[prefix] == current[prefix])
  {
    prefix++;
  }
  size_t suffix = 0;
  while (suffix < base.size() - prefix && suffix < current.size() - prefix
         && base[base.size() - 1 - suffix] == current[current.size() - 1 - suffix])
  {
    suffix++;
  }

  // Window: base [prefix, base.size()-suffix) -> current [prefix, cur.size()-suffix).
  const size_t old_bytes = base.size() - prefix - suffix;
  const size_t new_bytes = current.size() - prefix - suffix;
  if (old_bytes == 0 && new_bytes == 0)
  {
    return;
  }

  const std::vector<size_t> base_starts = line_starts(base);
  const std::vector<size_t> cur_starts = line_starts(current);
  const size_t end_base = base.size() - suffix; // window end offset, exclusive
  const size_t end_cur = current.size() - suffix;
  const int delta = (int)new_bytes - (int)old_bytes;

  auto global_of = [&](const Decoration &d) -> size_t
  {
    size_t row = (size_t)std::max(0, d.row);
    if (row >= base_starts.size())
    {
      row = base_starts.size() - 1;
    }
    return base_starts[row] + (size_t)std::max(0, d.col);
  };

  for (auto &d : decorations)
  {
    const size_t g = global_of(d);
    if (g < prefix)
    {
      continue; // strictly before the edit: byte offset preserved
    }
    if (g == prefix && !d.right_gravity)
    {
      continue; // left gravity at the window start: stays before inserted text
    }
    if (g >= end_base)
    {
      // After the edit: the suffix is identical, so the global offset shifts
      // by exactly the byte delta.
      const auto rc = offset_to_rowcol(current, cur_starts, g + (size_t)delta);
      d.row = rc.first;
      d.col = rc.second;
      continue;
    }
    // Inside the replaced window: collapse by gravity.
    const size_t target = d.right_gravity ? end_cur : prefix;
    const auto rc = offset_to_rowcol(current, cur_starts, target);
    d.row = rc.first;
    d.col = rc.second;
  }
}

void Editor::ensure_decorations_anchored(FileBuffer &buf)
{
  if (buf.decorations.empty())
  {
    buf.decoration_base_valid = false;
    buf.decoration_dirty = false;
    return;
  }
  if (buf.is_lazy())
  {
    // Lazy (disk-backed) buffers never snapshot text: decorations are dropped
    // and consumers re-apply them on their own events.
    buf.decorations.clear();
    buf.decoration_base_valid = false;
    buf.decoration_dirty = false;
    return;
  }
  if (!buf.decoration_base_valid)
  {
    // Decorations placed in current-text space before any edit was rebased
    // (e.g. boot-time Lua, or additions between file loads): adopt the
    // current text as the base instead of treating the positions as stale.
    // Wholesale content replacement clears the vector at the load site, so
    // this branch never has to guess about replaced text.
    buf.decoration_base = get_buffer_text(buf);
    buf.decoration_base_valid = true;
    buf.decoration_dirty = false;
    return;
  }
  if (!buf.decoration_dirty)
  {
    return;
  }
  const std::string current = get_buffer_text(buf);
  decoration_anchor_transform(buf.decorations, buf.decoration_base, current);
  buf.decoration_base = std::move(current);
  buf.decoration_dirty = false;
}

void Editor::decoration_rebase_begin(FileBuffer &buf)
{
  if (buf.decorations.empty() || buf.is_lazy())
  {
    return;
  }
  // Absorb the previous pending edit (if any) so the snapshot below is taken
  // against the text the next mutation will edit; render then diffs against
  // exactly one edit window.
  ensure_decorations_anchored(buf);
  buf.decoration_base = get_buffer_text(buf);
  buf.decoration_base_valid = true;
  buf.decoration_dirty = true;
}

// Inserts a decoration keeping the vector sorted by (row, col, priority, id).
void decoration_insert(FileBuffer &buf, Decoration decoration)
{
  auto it = std::lower_bound(buf.decorations.begin(),
                             buf.decorations.end(),
                             decoration,
                             [](const Decoration &a, const Decoration &b)
                             {
                               return decoration_less(a, b);
                             });
  buf.decorations.insert(it, std::move(decoration));
  buf.decoration_dirty = true;
}

bool decoration_erase(FileBuffer &buf, std::uint64_t id)
{
  for (auto it = buf.decorations.begin(); it != buf.decorations.end(); ++it)
  {
    if (it->id == id)
    {
      buf.decorations.erase(it);
      buf.decoration_dirty = true;
      return true;
    }
  }
  return false;
}

bool Editor::theme_group_color(const std::string &name_in, int &fg, int &bg) const
{
  std::string name = name_in;
  if (!name.empty() && name[0] == '@')
  {
    name = name.substr(1);
  }
  for (char &c : name)
  {
    c = (char)std::tolower((unsigned char)c);
  }
  if (theme_slot(theme, name, fg, bg))
  {
    return true;
  }
  // "@keyword.control" style captures: match on the dotted prefix so a
  // decoration can reuse tree-sitter capture names without a theme slot.
  const size_t dot = name.find('.');
  if (dot != std::string::npos)
  {
    return theme_slot(theme, name.substr(0, dot), fg, bg);
  }
  return false;
}