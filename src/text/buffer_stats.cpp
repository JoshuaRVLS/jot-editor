// The counting behind `:stats` (see text/buffer_stats.h). Every count is made as
// the lines go by, so nothing here needs the whole buffer at once and a lazy
// buffer stays lazy.
#include "buffer_stats.h"

#include <algorithm>
#include <cctype>
#include <utility>

buffer_stats::Counter::Counter(
    bool has_selection, int start_line, int start_col, int end_line, int end_col)
    : has_selection_(has_selection)
{
  if (!has_selection)
  {
    return;
  }
  const bool forward = start_line < end_line || (start_line == end_line && start_col <= end_col);
  first_line_ = forward ? start_line : end_line;
  first_col_ = forward ? start_col : end_col;
  last_line_ = forward ? end_line : start_line;
  last_col_ = forward ? end_col : start_col;
}

void buffer_stats::Counter::add_line(int index, const std::string &line)
{
  counts_.lines++;
  counts_.chars += (long long)line.size();

  // A run of letters, digits and underscores is one word; the run does not
  // continue across a line break.
  bool in_word = false;
  for (char c : line)
  {
    if (std::isalnum((unsigned char)c) || c == '_')
    {
      if (!in_word)
      {
        counts_.words++;
        in_word = true;
      }
    }
    else
    {
      in_word = false;
    }
  }

  if (!has_selection_ || index < first_line_ || index > last_line_)
  {
    return;
  }
  // The selection's own first and last line are cut at their column; the lines
  // between them are covered whole.
  int from = (index == first_line_) ? std::clamp(first_col_, 0, (int)line.size()) : 0;
  int to = (index == last_line_) ? std::clamp(last_col_, 0, (int)line.size()) : (int)line.size();
  if (to < from)
  {
    std::swap(to, from);
  }
  counts_.selected_chars += to - from;
}

buffer_stats::Counts buffer_stats::Counter::totals() const
{
  return counts_;
}
