// Blank-line indent guide inheritance, adapted from indent-blankline's
// blankline rule: an empty line draws the indent guides of the next
// non-blank line so the guide column reads as one continuous vertical
// line across blank rows. Trailing blanks at EOF draw nothing, and a
// blank run just before a closing line (`}`, `)`, `]`, `end`) inherits
// the previous non-blank line's indent instead -- indent-blankline's
// "trail" rule.
#pragma once

#include <cctype>
#include <string>

namespace blank_guides
{
  // Leading whitespace length in bytes (spaces/tabs at line start).
  inline int leading_ws(const std::string &line)
  {
    int n = 0;
    while (n < (int)line.size() && (line[n] == ' ' || line[n] == '\t'))
    {
      n++;
    }
    return n;
  }

  // True when the line (ignoring leading whitespace) starts a block
  // closer: `}`, `]`, `)` or the `end` keyword -- indent-blankline's
  // has_end rule (`^\s*(}\|]\|)\|end\)`).
  inline bool is_closing_line(const std::string &line)
  {
    const int i = leading_ws(line);
    if (i >= (int)line.size())
    {
      return false;
    }
    const char c = line[i];
    if (c == '}' || c == ']' || c == ')')
    {
      return true;
    }
    return line.compare(i, 3, "end") == 0;
  }

  // For a blank line at `idx`, the line whose indent its guides should
  // follow: the next non-blank line, or -- when that line is a closer --
  // the previous non-blank line. Returns -1 when there is no source
  // (trailing blank at EOF), meaning nothing should be drawn.
  template <typename LineAt>
  inline int guide_source_line(int line_count, LineAt &&line_at, int idx)
  {
    int next = idx + 1;
    while (next < line_count && line_at(next).empty())
    {
      next++;
    }
    if (next >= line_count)
    {
      return -1;
    }
    if (is_closing_line(line_at(next)))
    {
      int prev = idx - 1;
      while (prev >= 0 && line_at(prev).empty())
      {
        prev--;
      }
      if (prev >= 0)
      {
        return prev;
      }
    }
    return next;
  }
} // namespace blank_guides