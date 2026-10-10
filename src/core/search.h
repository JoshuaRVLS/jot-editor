#ifndef JOT_MODEL_SEARCH_H
#define JOT_MODEL_SEARCH_H

#include <tuple>
struct SearchMatch
{
  int line = 0;
  int col = 0;
  int len = 0;

  bool operator<(const SearchMatch &other) const
  {
    return std::tie(line, col, len) < std::tie(other.line, other.col, other.len);
  }
};

#endif
