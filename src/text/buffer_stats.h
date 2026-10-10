#pragma once

// Counts over a buffer's text: what `:stats` reports. Lines are folded in one at
// a time with add_line(), in order, so the caller decides where they come from
// -- a lazy buffer streams them out of its provider and is never materialised
// just to be counted.
//
// Nothing here knows about the editor. The selection arrives as four plain
// positions, and the caller turns the counts into a sentence
// (commands/line_commands.cpp).

#include <string>

namespace buffer_stats
{
  struct Counts
  {
    long long lines = 0;
    long long chars = 0;
    long long words = 0;
    // Characters the selection covers, 0 when there is none.
    long long selected_chars = 0;
  };

  class Counter
  {
  public:
    // `has_selection` off means the four positions are ignored. On means they
    // are the two ends of the selection, inclusive at both ends and in either
    // order, in the coordinates of the lines that follow.
    Counter(bool has_selection, int start_line, int start_col, int end_line, int end_col);

    // Folds the line at row `index` into the counts. Rows arrive from 0 upwards:
    // the selection's endpoints are matched against them.
    void add_line(int index, const std::string &line);

    Counts totals() const;

  private:
    bool has_selection_ = false;
    // The selection in forward order, so the span below is derived the same way
    // whichever end it was dragged from.
    int first_line_ = 0;
    int first_col_ = 0;
    int last_line_ = 0;
    int last_col_ = 0;
    Counts counts_;
  };
} // namespace buffer_stats
