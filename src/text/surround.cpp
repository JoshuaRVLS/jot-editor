#include "bracket_text_object.h"
#include "core/editor.h"
#include "lua/api.h"
#include "quote_text_object.h"
#include <algorithm>
#include <cctype>

namespace
{
  // How far a closing bracket is looked for. A call or literal that closes
  // further than this is not one the caret can plausibly be inside, and the
  // walk must stay bounded on a large file.
  constexpr int kBracketPairScanLines = 500;

  bool is_word_char(char c)
  {
    const unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) || c == '_';
  }

  bool pair_match(char left, char right)
  {
    return (left == '(' && right == ')') || (left == '[' && right == ']')
           || (left == '{' && right == '}') || (left == '"' && right == '"')
           || (left == '\'' && right == '\'') || (left == '`' && right == '`');
  }
} // namespace

bool Editor::surround_selection_or_word(const std::string &left, const std::string &right)
{
  if (left.empty() || right.empty())
  {
    set_message("Usage: :surround <left> <right>");
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  save_state();

  if (buf.selection.active)
  {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x))
    {
      std::swap(s, e);
    }

    s.y = std::clamp(s.y, 0, (int)buf.lines.size() - 1);
    e.y = std::clamp(e.y, 0, (int)buf.lines.size() - 1);
    s.x = std::clamp(s.x, 0, (int)buf.lines[s.y].size());
    e.x = std::clamp(e.x, 0, (int)buf.lines[e.y].size());

    buf.lines[e.y].insert((size_t)e.x, right);
    buf.lines[s.y].insert((size_t)s.x, left);
    buf.selection.start = {s.x + (int)left.size(), s.y};
    buf.selection.end = {e.x + (int)left.size(), e.y};
    buf.cursor = buf.selection.end;
  }
  else
  {
    int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
    std::string &line = buf.lines[y];
    int x = std::clamp(buf.cursor.x, 0, (int)line.size());
    int start = x;
    int end = x;
    while (start > 0 && is_word_char(line[start - 1]))
    {
      start--;
    }
    while (end < (int)line.size() && is_word_char(line[end]))
    {
      end++;
    }
    if (start == end)
    {
      set_message("No selection/word to surround");
      return false;
    }
    line.insert((size_t)end, right);
    line.insert((size_t)start, left);
    buf.cursor.y = y;
    buf.cursor.x = end + (int)left.size() + (int)right.size();
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Surrounded selection/word");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
  return true;
}

bool Editor::unsurround_selection_or_cursor()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.empty())
  {
    set_message("Nothing to unsurround");
    return false;
  }

  save_state();
  int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  std::string &line = buf.lines[y];
  if (line.size() < 2)
  {
    set_message("Nothing to unsurround");
    return false;
  }

  int x = std::clamp(buf.cursor.x, 0, (int)line.size());
  int left_idx = std::clamp(x - 1, 0, (int)line.size() - 1);
  int right_idx = std::clamp(x, 0, (int)line.size() - 1);

  bool done = false;
  if (left_idx >= 0 && right_idx >= 0 && left_idx < (int)line.size() && right_idx < (int)line.size()
      && pair_match(line[left_idx], line[right_idx]))
  {
    line.erase((size_t)right_idx, 1);
    line.erase((size_t)left_idx, 1);
    buf.cursor.x = left_idx;
    done = true;
  }
  else
  {
    int start = x;
    int end = x;
    while (start > 0 && is_word_char(line[start - 1]))
    {
      start--;
    }
    while (end < (int)line.size() && is_word_char(line[end]))
    {
      end++;
    }
    if (start > 0 && end < (int)line.size() && pair_match(line[start - 1], line[end]))
    {
      line.erase((size_t)end, 1);
      line.erase((size_t)(start - 1), 1);
      buf.cursor.x = start - 1;
      done = true;
    }
  }

  if (!done)
  {
    set_message("No surrounding pair found");
    return false;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed surrounding pair");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
  return true;
}

bool Editor::change_inside_quote(char quote)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.empty())
  {
    set_message("No quote pair found");
    return false;
  }

  int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  std::string &line = buf.lines[y];
  QuoteTextObject::Range range = QuoteTextObject::find_inner_range(line, buf.cursor.x, quote);
  if (!range.found)
  {
    set_message("No quote pair found");
    return false;
  }

  save_state();
  if (range.inner_end > range.inner_start)
  {
    line.erase((size_t)range.inner_start, (size_t)(range.inner_end - range.inner_start));
    buf.modified = true;
    buf.is_placeholder = false;
    if (lua_api)
    {
      lua_api->on_buffer_change(buf.filepath, "");
    }
    if (!buf.filepath.empty())
    {
      notify_lsp_change(buf.filepath);
    }
  }

  buf.selection.active = false;
  buf.cursor.y = y;
  buf.cursor.x = range.inner_start;
  buf.preferred_x = buf.cursor.x;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

// Which of the three literal kinds the caret is inside. The quote character is
// not part of the request ("clear the string I am in"), so the innermost
// containing pair wins and the caller is spared a key per kind.
bool Editor::change_inside_any_quote()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.empty())
  {
    set_message("No string pair found");
    return false;
  }

  int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  const std::string &line = buf.lines[(size_t)y];
  char best = 0;
  int best_span = 0;
  for (char quote : {'"', '\'', '`'})
  {
    QuoteTextObject::Range range = QuoteTextObject::find_inner_range(line, buf.cursor.x, quote);
    // Outside the pair is outside the string: a caret parked past both quotes
    // must not wipe the literal it just left.
    if (!range.found || buf.cursor.x < range.open || buf.cursor.x > range.close)
    {
      continue;
    }
    const int span = range.close - range.open;
    if (best == 0 || span < best_span)
    {
      best = quote;
      best_span = span;
    }
  }
  if (best == 0)
  {
    set_message("No string pair found");
    return false;
  }
  return change_inside_quote(best);
}

// Clears everything between the innermost brackets around the caret. Deleting
// the interior as a selection is what lets one call span lines: the selection
// is the pair's inside, wherever the closing bracket ended up.
bool Editor::change_inside_bracket()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.empty())
  {
    set_message("No bracket pair found");
    return false;
  }

  int y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  BracketTextObject::Range range =
      BracketTextObject::find_inner_range(buf.lines, y, buf.cursor.x, kBracketPairScanLines);
  if (!range.found)
  {
    set_message("No bracket pair found");
    return false;
  }
  if (range.open_line == range.close_line && range.close_col <= range.open_col + 1)
  {
    set_message("Nothing inside the brackets");
    return false;
  }

  // The pair was found for the primary caret, and delete_selection would take
  // each extra caret's own selection with it: those were not this pair.
  buf.extra_carets.clear();
  buf.selection.start = {range.open_col + 1, range.open_line};
  buf.selection.end = {range.close_col, range.close_line};
  buf.selection.active = true;
  delete_selection();
  return true;
}
