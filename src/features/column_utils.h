#ifndef COLUMN_UTILS_H
#define COLUMN_UTILS_H

#include <string>

inline int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

inline int compute_visual_column(const std::string &line, int logical_col,
                                int tab_size) {
  int clamped = std::clamp(logical_col, 0, (int)line.size());
  int visual = 0;
  for (int i = 0; i < clamped; i++) {
    visual += (line[i] == '\t') ? tab_advance(visual, tab_size) : 1;
  }
  return visual;
}

#endif
