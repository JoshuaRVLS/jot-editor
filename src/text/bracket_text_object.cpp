#include "bracket_text_object.h"

#include <algorithm>

namespace BracketTextObject
{
  namespace
  {
    bool is_open(char c)
    {
      return c == '(' || c == '[' || c == '{';
    }

    bool closes(char open, char c)
    {
      return (open == '(' && c == ')') || (open == '[' && c == ']') || (open == '{' && c == '}');
    }

    // The index just past the literal that starts at `start`. An unterminated
    // literal ends at the end of the line: a lone apostrophe in a comment must
    // not turn the rest of the file into a string.
    std::size_t skip_literal(const std::string &line, std::size_t start)
    {
      const char quote = line[start];
      for (std::size_t i = start + 1; i < line.size(); i++)
      {
        if (line[i] == '\\')
        {
          i++;
          continue;
        }
        if (line[i] == quote)
        {
          return i + 1;
        }
      }
      return line.size();
    }

    // The index just past the comment that starts at `start`: // runs to the
    // end of the line, /* to its terminator or, unterminated, to the end of it.
    std::size_t skip_comment(const std::string &line, std::size_t start)
    {
      if (line[start + 1] == '/')
      {
        return line.size();
      }
      for (std::size_t i = start + 2; i + 1 < line.size(); i++)
      {
        if (line[i] == '*' && line[i + 1] == '/')
        {
          return i + 2;
        }
      }
      return line.size();
    }

    // The index just past the literal or comment starting at `start`, or
    // `start` itself when an ordinary character is there. Both walks use it to
    // see only the brackets that are code.
    std::size_t skip_from(const std::string &line, std::size_t start)
    {
      const char c = line[start];
      if (c == '"' || c == '\'' || c == '`')
      {
        return skip_literal(line, start);
      }
      if (c == '/' && start + 1 < line.size() && (line[start + 1] == '/' || line[start + 1] == '*'))
      {
        return skip_comment(line, start);
      }
      return start;
    }

    // Which cells of a line are code. The backward walk needs this because it
    // cannot skip a literal it is already standing inside of, only one it meets
    // head on.
    std::vector<bool> code_mask(const std::string &line)
    {
      std::vector<bool> mask(line.size(), true);
      for (std::size_t i = 0; i < line.size();)
      {
        const std::size_t past = skip_from(line, i);
        if (past == i)
        {
          i++;
          continue;
        }
        for (std::size_t j = i; j < past && j < mask.size(); j++)
        {
          mask[j] = false;
        }
        i = std::max(past, i + 1);
      }
      return mask;
    }
  } // namespace

  bool is_supported_open(char c)
  {
    return is_open(c);
  }

  Range find_inner_range(const std::vector<std::string> &lines,
                         int cursor_line,
                         int cursor_x,
                         int max_scan_lines)
  {
    Range result;
    if (cursor_line < 0 || cursor_line >= (int)lines.size())
    {
      return result;
    }

    const std::string &line = lines[(std::size_t)cursor_line];
    if (cursor_x < 0 || cursor_x > (int)line.size())
    {
      return result;
    }

    int open_line = -1;
    int open_col = -1;
    const std::vector<bool> caret_line_code = code_mask(line);
    // A caret parked on an open bracket is inside it: that bracket is the pair
    // the caret is on, the same cell a bracket-match highlight would name.
    if (cursor_x < (int)line.size() && caret_line_code[(std::size_t)cursor_x]
        && is_open(line[(std::size_t)cursor_x]))
    {
      open_line = cursor_line;
      open_col = cursor_x;
    }
    else
    {
      // Otherwise the pair has to open at or before the caret, so one walk back
      // from it finds the nearest open with no close between the two of the
      // same kind. A close of one kind does not spend an open of another, so a
      // stray ) inside ( [ ] does not throw the ( away.
      int round = 0;
      int square = 0;
      int brace = 0;
      const int first_line = std::max(0, cursor_line - std::max(0, max_scan_lines));
      for (int y = cursor_line; y >= first_line && open_line < 0; y--)
      {
        const std::string &scan = lines[(std::size_t)y];
        const std::vector<bool> code = (y == cursor_line) ? caret_line_code : code_mask(scan);
        const int from = (y == cursor_line) ? cursor_x - 1 : (int)scan.size() - 1;
        for (int x = from; x >= 0; x--)
        {
          if (!code[(std::size_t)x])
          {
            continue;
          }
          const char c = scan[(std::size_t)x];
          if (c == ')')
          {
            round++;
          }
          else if (c == ']')
          {
            square++;
          }
          else if (c == '}')
          {
            brace++;
          }
          else if (c == '(')
          {
            if (round > 0)
            {
              round--;
              continue;
            }
            open_line = y;
            open_col = x;
            break;
          }
          else if (c == '[')
          {
            if (square > 0)
            {
              square--;
              continue;
            }
            open_line = y;
            open_col = x;
            break;
          }
          else if (c == '{')
          {
            if (brace > 0)
            {
              brace--;
              continue;
            }
            open_line = y;
            open_col = x;
            break;
          }
        }
      }
    }

    if (open_line < 0)
    {
      return result;
    }

    const char open = lines[(std::size_t)open_line][(std::size_t)open_col];
    const int last_line = std::min((int)lines.size() - 1, open_line + std::max(0, max_scan_lines));
    int depth = 0;
    for (int y = open_line; y <= last_line; y++)
    {
      const std::string &scan = lines[(std::size_t)y];
      for (std::size_t x = (y == open_line) ? (std::size_t)open_col : 0; x < scan.size();)
      {
        const std::size_t past = skip_from(scan, x);
        if (past != x)
        {
          x = past;
          continue;
        }
        const char c = scan[x];
        if (c == open)
        {
          depth++;
        }
        else if (closes(open, c))
        {
          depth--;
          if (depth == 0)
          {
            result.found = true;
            result.open_line = open_line;
            result.open_col = open_col;
            result.close_line = y;
            result.close_col = (int)x;
            return result;
          }
        }
        x++;
      }
    }

    return result;
  }

} // namespace BracketTextObject
