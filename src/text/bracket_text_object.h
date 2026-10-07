#ifndef BRACKET_TEXT_OBJECT_H
#define BRACKET_TEXT_OBJECT_H

#include <string>
#include <vector>

namespace BracketTextObject
{

  struct Range
  {
    bool found = false;
    int open_line = -1;
    int open_col = -1;
    int close_line = -1;
    int close_col = -1;
  };

  bool is_supported_open(char c);

  // The innermost ( [ { pair that encloses the caret: its opening bracket is
  // the nearest one at or before the caret (the caret's own cell counts, so a
  // caret parked on an opening bracket is inside it), and its closing bracket
  // is walked forward from there. Both walks are bounded by `max_scan_lines`
  // and neither counts a bracket inside a string literal or a comment, so
  // printf("(") has no pair and an apostrophe in a comment cannot pair with a
  // bracket on the next line.
  Range find_inner_range(const std::vector<std::string> &lines,
                         int cursor_line,
                         int cursor_x,
                         int max_scan_lines);

} // namespace BracketTextObject

#endif
