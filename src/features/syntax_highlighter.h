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
  std::vector<std::pair<int, int>> get_colors(const std::string &line);
};

#endif
