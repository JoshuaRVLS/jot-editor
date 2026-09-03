#ifndef SYNTAX_HIGHLIGHTER_H
#define SYNTAX_HIGHLIGHTER_H

#include "types.h"
#include <string>
#include <utility>
#include <vector>

class SyntaxHighlighter {
private:
  std::vector<SyntaxRule> rules;
  std::string file_extension;

public:
  void set_language(const std::string &ext);
  bool has_rules() const { return !rules.empty(); }
  // `byte_limit` bounds how much of the line is scanned; a margin past the
  // limit keeps strings/comments that start near the window edge colorized.
  std::vector<std::pair<int, int>> get_colors(const std::string &line,
                                              int byte_limit = 0x7fffffff);
};

#endif
