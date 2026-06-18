#ifndef COLUMN_UTILS_H
#define COLUMN_UTILS_H

#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

inline int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

inline int compute_visual_column(const std::string &line, int logical_col,
                                int tab_size) {
  int clamped =
      ui_clamp_to_utf8_boundary(line, std::clamp(logical_col, 0, (int)line.size()));
  int visual = 0;
  for (int i = 0; i < clamped;) {
    int next = ui_next_grapheme_boundary(line, i);
    if (next <= i)
      next = i + 1;
    if (line[i] == '\t') {
      visual += tab_advance(visual, tab_size);
    } else {
      visual += std::max(1, ui_cell_count(line.substr(i, next - i)));
    }
    i = next;
  }
  return visual;
}

inline std::vector<int> build_visual_columns(const std::string &line,
                                             int tab_size) {
  std::vector<int> cols(line.size() + 1, 0);
  int visual = 0;
  for (int i = 0; i < (int)line.size();) {
    int next = ui_next_grapheme_boundary(line, i);
    if (next <= i)
      next = i + 1;
    int width = 1;
    if (line[i] == '\t') {
      width = tab_advance(visual, tab_size);
    } else {
      width = std::max(1, ui_cell_count(line.substr(i, next - i)));
    }
    for (int j = i; j < next && j < (int)cols.size(); j++) {
      cols[j] = visual;
    }
    visual += width;
    cols[next] = visual;
    i = next;
  }
  return cols;
}

#endif
