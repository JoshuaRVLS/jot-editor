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

// Like build_visual_columns, but stops walking once `byte_limit` bytes are
// covered. Renderers only ever index columns up to the visible window, so for
// huge single-line files (minified/generated code) this keeps per-frame cost
// proportional to the on-screen width instead of the line length.
inline std::vector<int> build_visual_columns(const std::string &line,
                                             int tab_size, int byte_limit) {
  const int n = (int)line.size();
  const int limit = std::clamp(byte_limit, 0, n);
  // +8 headroom: graphemes are at most 4 bytes, so a walk that stops at
  // `limit` may emit an index up to limit + 3.
  std::vector<int> cols(limit + 8, 0);
  int visual = 0;
  for (int i = 0; i < limit;) {
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
    if (next < (int)cols.size()) {
      cols[next] = visual;
    }
    i = next;
  }
  return cols;
}

inline std::vector<int> build_visual_columns(const std::string &line,
                                             int tab_size) {
  return build_visual_columns(line, tab_size, (int)line.size());
}

inline int visual_to_logical_column(const std::string &line, int visual_col,
                                    int tab_size) {
  const int target = std::max(0, visual_col);
  int visual = 0;
  for (int i = 0; i < (int)line.size();) {
    int next = ui_next_grapheme_boundary(line, i);
    if (next <= i) next = i + 1;
    const int width = line[i] == '\t'
                          ? tab_advance(visual, tab_size)
                          : std::max(1, ui_cell_count(line.substr(i, next - i)));
    const int next_visual = visual + width;
    if (target < next_visual) {
      return target - visual <= next_visual - target ? i : next;
    }
    visual = next_visual;
    i = next;
  }
  return (int)line.size();
}

#endif
