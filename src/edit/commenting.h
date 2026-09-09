// Comment toggling: per-extension comment styles (imported from
// comment.nvim's ft.lua, see tools/comment_import.py) and the pure
// line-toggle logic shared by the editor and tests.
#pragma once

#include "jot/integrations/comment_style_data.h"
#include <string>
#include <string_view>
#include <vector>

namespace commenting
{
  // Comment markers for a file type. `block_open`/`block_close` are non-empty
  // when the language has a true block-comment form; a multi-line selection
  // then wraps in one pair instead of commenting each line separately.
  struct Style
  {
    std::string_view prefix;       // line comment prefix ("//", "#", "--", "%")
    std::string_view suffix;       // per-line closer for pseudo-block langs (html "-->")
    std::string_view block_open;   // block opener, or empty when none exists
    std::string_view block_close;  // block closer, or empty when none exists
  };

  inline Style style_for(const std::string &ext)
  {
    for (const CommentStyleSpec &entry : kCommentStyles)
    {
      if (entry.ext == ext)
      {
        return {entry.line_prefix, entry.line_suffix, entry.block_open, entry.block_close};
      }
    }
    // Unknown extensions get the C-family form, matching the old default.
    return {"//", "", "/*", "*/"};
  }

  inline std::string line_indent(const std::string &s)
  {
    size_t n = 0;
    while (n < s.size() && (s[n] == ' ' || s[n] == '\t'))
    {
      n++;
    }
    return s.substr(0, n);
  }

  inline bool line_is_commented(const std::string &s, const Style &style)
  {
    const std::string body = s.substr(line_indent(s).size());
    return body.compare(0, style.prefix.size(), style.prefix) == 0;
  }

  // True when the whole [start,end] range is wrapped in one block comment:
  // the opener sits on the first line and the closer on the last.
  inline bool range_is_block_commented(const std::vector<std::string> &lines,
                                       int start,
                                       int end,
                                       const Style &style)
  {
    if (style.block_open.empty() || start > end)
    {
      return false;
    }
    const std::string &first_body = lines[start].substr(line_indent(lines[start]).size());
    const std::string &last_body = lines[end].substr(line_indent(lines[end]).size());
    return first_body.compare(0, style.block_open.size(), style.block_open) == 0
           && last_body.size() >= style.block_close.size()
           && last_body.compare(last_body.size() - style.block_close.size(),
                                style.block_close.size(),
                                style.block_close)
                  == 0;
  }

  // Toggles commenting on lines[start_y..end_y] in place. `multi_line` is
  // true when the caller's selection spanned several lines; `block_boundary`
  // marks ranges discovered through the re-toggle memory / boundary scan
  // (the caller's cursor sat on an existing block opener or closer).
  inline void toggle_lines(std::vector<std::string> &lines,
                           const Style &style,
                           int start_y,
                           int end_y,
                           bool multi_line,
                           bool block_boundary)
  {
    // Commenting always stamps every line of the range with its own markers
    // (//, #, <!-- -->, ...) — the predictable behavior of vim/comment.nvim.
    // Unwrapping takes one of two shapes: the whole range sits in a single
    // hand-written block pair (/* ... */) whose middle lines carry no markers,
    // or every line carries its own line comment to strip.
    // A block-only style (html/xml: <!-- -->) is one where every line comment
    // is itself a single-line block wrap.
    const bool block_only_style = !style.block_open.empty() && style.prefix == style.block_open;
    const bool all_line_commented = [&]
    {
      for (int i = start_y; i <= end_y; i++)
      {
        if (!line_is_commented(lines[i], style))
        {
          return false;
        }
      }
      return true;
    }();
    const bool wrapped_in_block =
        !style.block_open.empty()
        && (multi_line || block_only_style || block_boundary)
        // A range where every line carries its own marker (<!-- a --> on
        // each line) is per-line commenting, not one wrapped pair: unwrapping
        // must strip every line, or the middle lines keep half a marker.
        && !all_line_commented
        && range_is_block_commented(lines, start_y, end_y, style);
    const bool uncomment = wrapped_in_block || all_line_commented;

    for (int i = start_y; i <= end_y; i++)
    {
      const std::string indent = line_indent(lines[i]);
      const std::string body = lines[i].substr(indent.size());
      const bool line_commented = line_is_commented(lines[i], style);

      // Middle lines of a wrapped block are already commented by the wrapper;
      // only the boundary lines carry the markers to remove.
      if (uncomment && wrapped_in_block && i != start_y && i != end_y)
      {
        continue;
      }
      if (uncomment && wrapped_in_block && i == start_y)
      {
        // Remove the opener on the range's first line (may also carry line
        // comment on the same line, e.g. /*// code).
        std::string rest = body;
        const bool has_open = style.block_open.empty()
                                  ? false
                                  : rest.compare(0, style.block_open.size(), style.block_open)
                                        == 0;
        if (has_open)
        {
          rest = rest.substr(style.block_open.size());
        }
        // Only strip a separate line-comment marker (C: ///* ...). For block
        // styles whose line form is the same token (html: <!--), the opener
        // removal above already consumed it.
        if (has_open && style.prefix != style.block_open && line_commented
            && rest.compare(0, style.prefix.size(), style.prefix) == 0)
        {
          rest = rest.substr(style.prefix.size());
        }
        if (!rest.empty() && rest[0] == ' ')
        {
          rest.erase(0, 1); // undo the "/* " separator
        }
        // Single-line block (start == end): the same line carries the closer,
        // which the end-of-range branch below would never reach.
        if (i == end_y)
        {
          if (!style.block_close.empty() && rest.size() >= style.block_close.size()
              && rest.compare(rest.size() - style.block_close.size(),
                              style.block_close.size(),
                              style.block_close)
                     == 0)
          {
            rest.erase(rest.size() - style.block_close.size());
          }
          size_t end_nonspace = rest.find_last_not_of(' ');
          if (end_nonspace != std::string::npos && end_nonspace + 1 < rest.size())
          {
            rest.erase(end_nonspace + 1); // undo the " */" separator
          }
        }
        lines[i] = indent + rest;
      }
      else if (uncomment && i == end_y && wrapped_in_block)
      {
        // Remove the closer on the range's last line.
        std::string rest = body;
        if (!style.block_close.empty() && rest.size() >= style.block_close.size()
            && rest.compare(rest.size() - style.block_close.size(),
                            style.block_close.size(),
                            style.block_close)
                   == 0)
        {
          rest.erase(rest.size() - style.block_close.size());
        }
        if (line_commented && style.prefix != style.block_open
            && rest.compare(0, style.prefix.size(), style.prefix) == 0)
        {
          rest = rest.substr(style.prefix.size());
        }
        size_t end_nonspace = rest.find_last_not_of(' ');
        if (end_nonspace != std::string::npos && end_nonspace + 1 < rest.size())
        {
          rest.erase(end_nonspace + 1); // undo the " */" separator
        }
        lines[i] = indent + rest;
      }
      else if (uncomment && line_commented)
      {
        // Strip this line's own comment marker (prefix and, for per-line
        // block style, its trailing suffix).
        const size_t after = indent.size() + style.prefix.size();
        std::string rest = lines[i].substr(after);
        if (!style.suffix.empty() && rest.size() >= style.suffix.size()
            && rest.compare(rest.size() - style.suffix.size(), style.suffix.size(), style.suffix)
                   == 0)
        {
          rest.erase(rest.size() - style.suffix.size());
          // Undo the "<!-- " / " -->" separators hand-written comments carry.
          if (!rest.empty() && rest[0] == ' ')
          {
            rest.erase(0, 1);
          }
          size_t end_nonspace = rest.find_last_not_of(' ');
          if (end_nonspace != std::string::npos && end_nonspace + 1 < rest.size())
          {
            rest.erase(end_nonspace + 1);
          }
        }
        lines[i] = indent + rest;
      }
      else
      {
        // Comment the line unless it already carries a marker (a line already
        // commented is left alone); blank lines are skipped so a selection
        // never grows marker-only rows.
        if (!line_commented && !body.empty())
        {
          lines[i] = indent + std::string(style.prefix) + body;
          if (!style.suffix.empty())
          {
            lines[i] += style.suffix;
          }
        }
      }
    }
  }
} // namespace commenting