#pragma once

// Pure edits over a range of lines, one function per line command the editor
// offers: `:sort`, `:sortdesc`, `:reverselines`, `:shufflelines`,
// `:uniquelines`, `:trimblank` and `:join`. Each takes the buffer's lines and an
// inclusive range the caller has already clamped to the buffer, and touches
// nothing else -- the undo step, the cursor, the message line and the redraws
// belong to the caller (commands/line_commands.cpp).
//
// This is the half of those commands that has nothing to do with the editor, so
// it is the half that lives in the module the editor is above.

#include <string>
#include <vector>

namespace line_ops
{
  // Sort [first, last] case-insensitively, leaving lines that compare equal in
  // the order they had.
  void sort_lines(std::vector<std::string> &lines, int first, int last, bool descending);

  // Reverse [first, last] in place.
  void reverse_lines(std::vector<std::string> &lines, int first, int last);

  // Shuffle [first, last] with a fresh random seed.
  void shuffle_lines(std::vector<std::string> &lines, int first, int last);

  // Drop every line whose text has already appeared in [first, last]. Returns
  // how many lines were removed.
  int remove_duplicate_lines(std::vector<std::string> &lines, int first, int last);

  // Drop every whitespace-only line in [first, last], never the last line left
  // in the buffer. Returns how many lines were removed.
  int remove_blank_lines(std::vector<std::string> &lines, int first, int last);

  // Join [first, last] into one line at `first`: each continuation is
  // left-trimmed and gets one space before it when the line it joins has no
  // trailing whitespace, which is what `:join` does to a wrapped line. Returns
  // how many lines were removed.
  int join_lines(std::vector<std::string> &lines, int first, int last);
} // namespace line_ops
