// Bracket-pair helpers for buffer rendering: rainbow depth scanning, pair
// matching at the cursor, and the active bracket guide.
#include "column_utils.h"
#include "editor.h"
#include "render/buffer_internal.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>

using namespace buffer_internal;

namespace buffer_internal
{
bool is_open_bracket(char c)
{
  return c == '(' || c == '[' || c == '{';
}
bool is_close_bracket(char c)
{
  return c == ')' || c == ']' || c == '}';
}
int rainbow_bracket_color(const Theme &theme, int depth)
{
  static const int kPaletteSize = 6;
  int normalized = depth % kPaletteSize;
  if (normalized < 0)
    normalized += kPaletteSize;
  switch (normalized)
  {
  case 0:
    return theme.fg_bracket1;
  case 1:
    return theme.fg_bracket2;
  case 2:
    return theme.fg_bracket3;
  case 3:
    return theme.fg_bracket4;
  case 4:
    return theme.fg_bracket5;
  default:
    return theme.fg_bracket6;
  }
}
void apply_bracket_depth_delta(char c, int &depth)
{
  if (is_open_bracket(c))
  {
    depth++;
  }
  else if (is_close_bracket(c))
  {
    depth = std::max(0, depth - 1);
  }
}
bool bracket_chars(char c, char &open, char &close, bool &is_open)
{
  switch (c)
  {
  case '(':
    open = '(';
    close = ')';
    is_open = true;
    return true;
  case ')':
    open = '(';
    close = ')';
    is_open = false;
    return true;
  case '[':
    open = '[';
    close = ']';
    is_open = true;
    return true;
  case ']':
    open = '[';
    close = ']';
    is_open = false;
    return true;
  case '{':
    open = '{';
    close = '}';
    is_open = true;
    return true;
  case '}':
    open = '{';
    close = '}';
    is_open = false;
    return true;
  default:
    return false;
  }
}
BracketPairMatch find_pair_at(const FileBuffer &buf, int line, int col)
{
  BracketPairMatch result;
  if (line < 0 || line >= (int)buf.line_count())
    return result;
  if (col < 0 || col >= (int)buf.line(line).size())
    return result;

  char open = 0, close = 0;
  bool is_open = false;
  if (!bracket_chars(buf.line(line)[col], open, close, is_open))
  {
    return result;
  }

  if (is_open)
  {
    int depth = 1;
    const int max_line =
        std::min((int)buf.line_count() - 1, line + kBracketMatchSearchLimitLines);
    for (int y = line; y <= max_line; y++)
    {
      int start_x = (y == line) ? col + 1 : 0;
      for (int x = start_x; x < (int)buf.line(y).size(); x++)
      {
        char ch = buf.line(y)[x];
        if (ch == open)
        {
          depth++;
        }
        else if (ch == close)
        {
          depth--;
          if (depth == 0)
          {
            result.found = true;
            result.open_line = line;
            result.open_col = col;
            result.close_line = y;
            result.close_col = x;
            return result;
          }
        }
      }
    }
  }
  else
  {
    int depth = 1;
    const int min_line = std::max(0, line - kBracketMatchSearchLimitLines);
    for (int y = line; y >= min_line; y--)
    {
      int start_x = (y == line) ? col - 1 : (int)buf.line(y).size() - 1;
      for (int x = start_x; x >= 0; x--)
      {
        char ch = buf.line(y)[x];
        if (ch == close)
        {
          depth++;
        }
        else if (ch == open)
        {
          depth--;
          if (depth == 0)
          {
            result.found = true;
            result.open_line = y;
            result.open_col = x;
            result.close_line = line;
            result.close_col = col;
            return result;
          }
        }
      }
    }
  }

  return result;
}
ActiveBracketGuide build_active_bracket_guide(FileBuffer &buf, int tab_size)
{
  ActiveBracketGuide guide;
  // Reuse the previous frame's answer when neither the caret nor the buffer
  // content moved. The builder is called once per pane per frame and, in the
  // worst case (an unmatched bracket under the caret), each of its three
  // candidate columns walks kBracketMatchSearchLimitLines lines looking for a
  // partner -- work that is identical from frame to frame while the caret is
  // parked. See FileBuffer::BracketGuideMemo.
  FileBuffer::BracketGuideMemo &memo = buf.bracket_guide_memo;
  if (memo.valid && memo.generation == buf.edit_generation && memo.cursor_x == buf.cursor.x
      && memo.cursor_y == buf.cursor.y && memo.tab_size == tab_size)
  {
    guide.active = memo.active;
    guide.visual_column = memo.column;
    guide.start_line = memo.start_line;
    guide.end_line = memo.end_line;
    return guide;
  }
  memo.valid = true;
  memo.generation = buf.edit_generation;
  memo.cursor_x = buf.cursor.x;
  memo.cursor_y = buf.cursor.y;
  memo.tab_size = tab_size;
  const auto store = [&memo](const ActiveBracketGuide &g) {
    memo.active = g.active;
    memo.column = g.visual_column;
    memo.start_line = g.start_line;
    memo.end_line = g.end_line;
    return g;
  };
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
  {
    return store(guide);
  }

  const std::string &line = buf.line(buf.cursor.y);
  int candidates[3] = {buf.cursor.x, buf.cursor.x - 1, buf.cursor.x + 1};
  for (int c : candidates)
  {
    if (c < 0 || c >= (int)line.size())
    {
      continue;
    }
    if (!is_open_bracket(line[c]) && !is_close_bracket(line[c]))
    {
      continue;
    }

    BracketPairMatch pair = find_pair_at(buf, buf.cursor.y, c);
    if (!pair.found)
    {
      continue;
    }

    int top = std::min(pair.open_line, pair.close_line);
    int bottom = std::max(pair.open_line, pair.close_line);
    if (bottom - top < 2)
    {
      continue; // No inner rows to draw a connector on
    }

    guide.active = true;
    const std::string &open_line = buf.line(pair.open_line);
    guide.visual_column = leading_indent_visual_column(open_line, tab_size);
    guide.start_line = top;
    guide.end_line = bottom;
    return store(guide);
  }

  return store(guide);
}
} // namespace buffer_internal

int Editor::bracket_depth_at_line_start(FileBuffer &buf, int line)
{
  const int count = (int)buf.line_count();
  if (count <= 0 || line <= 0)
  {
    return 0;
  }
  const int target = std::min(line, count - 1);
  auto &prefix = buf.bracket_depth_prefix;
  int &upto = buf.bracket_depth_prefix_upto;
  if (prefix.empty())
  {
    prefix.push_back(0); // depth at the start of line 0
  }
  // Lines above this keep the raw walk: syntax colors for huge single
  // lines are windowed (see get_line_syntax_colors), so token info for
  // the whole line is not available without tokenizing megabytes. The render
  // walk uses the same threshold so both agree on which lines are raw.
  while (upto < target)
  {
    // Walking line `upto` turns the depth at the start of line `upto`
    // into the depth at the start of line `upto + 1`.
    int value = prefix[upto];
    const std::string &ln = buf.line(upto);
    if (ln.size() > kBracketTokenAwareLineBytes)
    {
      for (char c : ln)
      {
        apply_bracket_depth_delta(c, value);
      }
    }
    else
    {
      // Same token-aware skip as the visible-row walk: brackets inside
      // strings/comments neither count toward depth nor get rainbow
      // colors. The per-line token cache makes this O(1) on re-visits.
      const auto &colors = get_line_syntax_colors(buf, upto);
      for (int i = 0; i < (int)ln.size(); i++)
      {
        const bool tokenized = i < (int)colors.size() && colors[i].first == 1;
        const int type = tokenized ? colors[i].second : 0;
        if (type == TS_TOKEN_STRING || type == TS_TOKEN_COMMENT)
        {
          continue;
        }
        apply_bracket_depth_delta(ln[i], value);
      }
    }
    prefix.push_back(value);
    upto++;
  }
  return prefix[target];
}
