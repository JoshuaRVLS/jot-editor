#ifndef FOLDING_H
#define FOLDING_H

#include "types.h"
#include <string>
#include <utility>
#include <vector>

namespace Folding
{
  // A prepared view over one fold-range vector.
  //
  // Every visibility question -- is this line hidden, is it a folded header,
  // how many lines does that header hide, which buffer line sits on visible
  // row N, which row does line L sit on -- depends only on the *collapsed*
  // ranges. The free functions answer a question from scratch (a scan of the
  // whole vector, or a freshly built index), which is fine once per event but
  // not once per visible row: on a file with ~20k detected ranges (one per
  // brace pair) a 34-row frame spent ~3.7ms in is_line_folded_header and
  // ~1.4ms in is_line_hidden alone.
  //
  // A view indexes the collapsed ranges once and answers in O(log n) (O(1)
  // per line for the hidden/header tests). Build one per frame per buffer and
  // reuse it for every row; building it scans the range vector once, so a
  // one-shot caller can still just call the free function.
  class FoldView
  {
  public:
    explicit FoldView(const std::vector<FoldRange> &ranges);

    bool hidden(int line) const;
    bool folded_header(int line, int *range_index = nullptr) const;
    int hidden_count_for_header(int line) const;
    int visible_line_count(int line_count) const;
    // Number of visible lines with an index below `line` (the visible rank of
    // a visible line, and of the first line shown after it).
    int visible_rank_of_line(int line) const;
    int buffer_line_for_visible_index(int visible_index, int line_count) const;
    int buffer_line_for_visible_offset(int first_line, int offset, int line_count) const;
    int visible_row_for_line(int first_line,
                             int target_line,
                             int visible_rows,
                             int line_count) const;
    int next_visible_line(int line, int line_count) const;
    int previous_visible_line(int line) const;
    // Moves `delta` visible lines from `line` (a scroll step, so `delta` can be
    // a notch's worth of lines). Answers from this view's index: the free
    // function of the same name steps off the range vector, one full scan per
    // line it crosses.
    int advance_visible_lines(int line, int delta, int line_count) const;
    int clamp_scroll_offset(int scroll, int visible_rows, int line_count) const;

  private:
    // Number of hidden lines with index < `limit`.
    int hidden_before(int limit) const;
    // First visible line at or after `line`, or -1 when there is none.
    int first_visible_at_or_after(int line, int line_count) const;
    // Buffer line with visible rank `rank`, or -1 when the rank is past the
    // last line of the buffer.
    int line_at_rank(int rank, int line_count) const;
    // Visible rank (0-based, hidden lines excluded) of a visible line.
    int rank_of_line(int line) const;

    // One collapsed fold header, indexed for the per-line lookup.
    struct Header
    {
      int start_line = 0;
      int end_line = 0;
      int range_index = 0;
    };

    // Merged, inclusive spans of the lines the collapsed ranges hide.
    std::vector<std::pair<int, int>> spans_;
    // prefix_[i + 1] is the number of hidden lines in spans_[0..i].
    std::vector<int> prefix_;
    // Collapsed headers sorted by start line (longest first within a start
    // line, so a nested pair answers with the outer block).
    std::vector<Header> headers_;
  };

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
  bool
  is_line_folded_header(const std::vector<FoldRange> &ranges, int line, int *range_index = nullptr);
  int hidden_line_count_for_header(const std::vector<FoldRange> &ranges, int line);
  int next_visible_line(const std::vector<FoldRange> &ranges, int line, int line_count);
  int previous_visible_line(const std::vector<FoldRange> &ranges, int line);
  int advance_visible_lines(const std::vector<FoldRange> &ranges,
                            int line,
                            int delta,
                            int line_count);
  int visible_line_count(const std::vector<FoldRange> &ranges, int line_count);
  int buffer_line_for_visible_index(const std::vector<FoldRange> &ranges,
                                    int visible_index,
                                    int line_count);
  int visible_row_for_line(const std::vector<FoldRange> &ranges,
                           int first_line,
                           int target_line,
                           int visible_rows,
                           int line_count);
  int buffer_line_for_visible_offset(const std::vector<FoldRange> &ranges,
                                     int first_line,
                                     int offset,
                                     int line_count);
  int clamp_scroll_offset(const std::vector<FoldRange> &ranges,
                          int scroll,
                          int visible_rows,
                          int line_count);
} // namespace Folding

#endif
