#ifndef FOLDING_H
#define FOLDING_H

#include "types.h"
#include <string>
#include <vector>

namespace Folding {
std::vector<FoldRange> detect_ranges(const std::vector<std::string> &lines,
                                     const std::string &extension);
void refresh_ranges(std::vector<FoldRange> &ranges,
                    const std::vector<std::string> &lines,
                    const std::string &extension);
std::string encode_collapsed_ranges(const std::vector<FoldRange> &ranges);
std::vector<FoldRange> decode_collapsed_ranges(const std::string &payload);
void apply_collapsed_ranges(std::vector<FoldRange> &ranges,
                            const std::vector<FoldRange> &collapsed);
int fold_at_or_before_line(const std::vector<FoldRange> &ranges, int line);
int fold_starting_at_line(const std::vector<FoldRange> &ranges, int line);
bool is_line_hidden(const std::vector<FoldRange> &ranges, int line);
bool is_line_folded_header(const std::vector<FoldRange> &ranges, int line,
                           int *range_index = nullptr);
int hidden_line_count_for_header(const std::vector<FoldRange> &ranges,
                                 int line);
int next_visible_line(const std::vector<FoldRange> &ranges, int line,
                      int line_count);
int previous_visible_line(const std::vector<FoldRange> &ranges, int line);
int advance_visible_lines(const std::vector<FoldRange> &ranges, int line,
                          int delta, int line_count);
int visible_line_count(const std::vector<FoldRange> &ranges, int line_count);
int buffer_line_for_visible_index(const std::vector<FoldRange> &ranges,
                                  int visible_index, int line_count);
int visible_row_for_line(const std::vector<FoldRange> &ranges, int first_line,
                         int target_line, int visible_rows, int line_count);
int buffer_line_for_visible_offset(const std::vector<FoldRange> &ranges,
                                   int first_line, int offset,
                                   int line_count);
int clamp_scroll_offset(const std::vector<FoldRange> &ranges, int scroll,
                        int visible_rows, int line_count);
} // namespace Folding

#endif
