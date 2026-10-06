#ifndef SAVE_HYGIENE_H
#define SAVE_HYGIENE_H

#include <string>
#include <vector>

// What a save cleans up on its way to the disk, and what it has to leave
// alone. The rules live here, free of the buffer and of the filesystem, so they
// can be asserted directly; file.cpp asks and does the writing.
class SaveHygiene
{
public:
  // Whether `line` ends in a space or a tab, so a save has something to drop.
  static bool has_trailing_whitespace(const std::string &line);

  // `line` without its trailing spaces and tabs. A line that is nothing but
  // whitespace comes back empty, which is the blank line it meant to be.
  static std::string trim_trailing_whitespace(const std::string &line);

  // Whether trailing whitespace in this file is content rather than stray
  // blanks. Markdown reads two of them as a hard line break, so a save has to
  // leave those exactly as they are; `:trim` still cleans them on request.
  static bool preserves_trailing_whitespace(const std::string &path);

  // Whether the save may rebase this file's indentation at all: the languages
  // the editor has its own block rules for, and never a format whose
  // whitespace is data (see the note on reindent_lines).
  static bool supports_reindent(const std::string &path);

  // The document with its leading whitespace rebased on the indent size in
  // settings. A brace language has its nesting recomputed from the file's own
  // braces, brackets and labels; a Python file has each block's depth read from
  // its own indentation, since a Python block is that indentation and nothing
  // else in the file says where it ends. The first line is the baseline, so a
  // fragment that opens at column four keeps its four and only the nesting
  // under it moves. Lines inside a multi-line comment, string or long bracket,
  // and lines whose column is a wrapped expression's alignment or a braceless
  // body, are left exactly as they are, and a file whose indentation is already
  // right comes back unchanged.
  static std::vector<std::string>
  reindent_lines(const std::vector<std::string> &lines, const std::string &path, int tab_size);
};

#endif
