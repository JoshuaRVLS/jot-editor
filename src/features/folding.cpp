#include "features/language.h"
#include "folding.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <sstream>
#include <stack>

namespace
{
  int indent_level(const std::string &line)
  {
    int indent = 0;
    for (char c : line)
    {
      if (c == ' ')
      {
        indent++;
      }
      else if (c == '\t')
      {
        indent += 4;
      }
      else
      {
        break;
      }
    }
    return indent;
  }

  bool blank_or_comment_only(const std::string &line)
  {
    size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
      return true;
    }
    return line[first] == '#';
  }

  std::vector<FoldRange> detect_indent_ranges(const std::vector<std::string> &lines)
  {
    std::vector<FoldRange> ranges;
    const int n = (int)lines.size();
    for (int i = 0; i < n - 1; i++)
    {
      if (blank_or_comment_only(lines[i]))
      {
        continue;
      }
      int base_indent = indent_level(lines[i]);
      int first_child = -1;
      for (int j = i + 1; j < n; j++)
      {
        if (blank_or_comment_only(lines[j]))
        {
          continue;
        }
        if (indent_level(lines[j]) > base_indent)
        {
          first_child = j;
        }
        break;
      }
      if (first_child < 0)
      {
        continue;
      }
      int end = first_child;
      for (int j = first_child + 1; j < n; j++)
      {
        if (blank_or_comment_only(lines[j]))
        {
          end = j;
          continue;
        }
        if (indent_level(lines[j]) <= base_indent)
        {
          break;
        }
        end = j;
      }
      if (end > i)
      {
        ranges.push_back({i, end, false});
      }
    }
    return ranges;
  }

  std::vector<FoldRange> detect_brace_ranges(const std::vector<std::string> &lines)
  {
    std::vector<FoldRange> ranges;
    std::vector<int> stack;
    bool in_block_comment = false;

    for (int y = 0; y < (int)lines.size(); y++)
    {
      const std::string &line = lines[y];
      bool in_string = false;
      char quote = 0;
      bool escaped = false;
      for (int x = 0; x < (int)line.size(); x++)
      {
        char c = line[x];
        char next = (x + 1 < (int)line.size()) ? line[x + 1] : '\0';
        if (in_block_comment)
        {
          if (c == '*' && next == '/')
          {
            in_block_comment = false;
            x++;
          }
          continue;
        }
        if (in_string)
        {
          if (escaped)
          {
            escaped = false;
          }
          else if (c == '\\')
          {
            escaped = true;
          }
          else if (c == quote)
          {
            in_string = false;
          }
          continue;
        }
        if (c == '/' && next == '/')
        {
          break;
        }
        if (c == '/' && next == '*')
        {
          in_block_comment = true;
          x++;
          continue;
        }
        if (c == '"' || c == '\'')
        {
          in_string = true;
          quote = c;
          continue;
        }
        if (c == '{')
        {
          stack.push_back(y);
        }
        else if (c == '}' && !stack.empty())
        {
          int start = stack.back();
          stack.pop_back();
          if (y > start)
          {
            ranges.push_back({start, y, false});
          }
        }
      }
    }
    std::sort(ranges.begin(),
              ranges.end(),
              [](const FoldRange &a, const FoldRange &b)
              {
                if (a.start_line != b.start_line)
                {
                  return a.start_line < b.start_line;
                }
                return a.end_line > b.end_line;
              });
    return ranges;
  }

  // Advance from `line` past every collapsed range that hides it, or return
  // `line` unchanged when nothing does. Each pass jumps to the end of a range
  // that covers the position instead of stepping a line at a time, and no
  // index is prepared: moving the caret one line should not cost a scan and
  // sort of every collapsed range (the old shape of next_visible_line).
  int skip_hidden_forward(const std::vector<FoldRange> &ranges, int line, int line_count)
  {
    int current = line;
    bool moved = true;
    while (moved && current < line_count)
    {
      moved = false;
      for (const auto &range : ranges)
      {
        if (range.collapsed && current > range.start_line && current <= range.end_line)
        {
          current = range.end_line + 1;
          moved = true;
        }
      }
    }
    return current;
  }

  // The mirror: the first line at or before `line` that is not hidden, walking
  // back over the collapsed ranges that cover it.
  int skip_hidden_backward(const std::vector<FoldRange> &ranges, int line)
  {
    int current = line;
    bool moved = true;
    while (moved && current > 0)
    {
      moved = false;
      for (const auto &range : ranges)
      {
        if (range.collapsed && current > range.start_line && current <= range.end_line)
        {
          current = range.start_line;
          moved = true;
        }
      }
    }
    return std::max(0, current);
  }

} // namespace

namespace Folding
{
  // See folding.h: the queries below all depend only on the collapsed ranges,
  // and the per-row callers were re-scanning every detected range (one per
  // brace pair, ~20k on a large file) for each visible row of every frame.
  FoldView::FoldView(const std::vector<FoldRange> &ranges)
  {
    // One pass over the ranges collects the collapsed ones. The detected range
    // vector is sorted by start line (longest first within a start) and the
    // callers preserve that, so the index usually comes out of the scan in
    // order and the sort is skipped: on a file with 8k collapsed pairs sorting
    // them cost more than a whole frame's queries. Inputs that are not in order
    // (a hand-built or saved range list) take the sort path, so the answers
    // never depend on the order they arrive in.
    bool sorted = true;
    int prev_start = std::numeric_limits<int>::min();
    int prev_end = std::numeric_limits<int>::max();
    for (size_t i = 0; i < ranges.size(); i++)
    {
      const FoldRange &range = ranges[i];
      if (!range.collapsed)
      {
        continue;
      }
      if (range.start_line < prev_start
          || (range.start_line == prev_start && range.end_line > prev_end))
      {
        sorted = false;
      }
      prev_start = range.start_line;
      prev_end = range.end_line;
      headers_.push_back(Header{range.start_line, range.end_line, (int)i});
    }
    if (!sorted)
    {
      std::sort(headers_.begin(),
                headers_.end(),
                [](const Header &a, const Header &b)
                {
                  if (a.start_line != b.start_line)
                  {
                    return a.start_line < b.start_line;
                  }
                  const int len_a = a.end_line - a.start_line;
                  const int len_b = b.end_line - b.start_line;
                  if (len_a != len_b)
                  {
                    return len_a > len_b;
                  }
                  return a.range_index < b.range_index;
                });
    }
    // A collapsed [s, e] hides lines s+1..e. The headers are sorted by start
    // line, so the hidden spans merge in one pass into disjoint inclusive
    // ranges -- "is line N hidden" is then a binary search.
    for (const Header &header : headers_)
    {
      if (header.end_line <= header.start_line)
      {
        continue;
      }
      const int span_first = header.start_line + 1;
      if (spans_.empty() || span_first > spans_.back().second + 1)
      {
        spans_.push_back({span_first, header.end_line});
      }
      else
      {
        spans_.back().second = std::max(spans_.back().second, header.end_line);
      }
    }
    prefix_.resize(spans_.size() + 1);
    for (size_t i = 0; i < spans_.size(); i++)
    {
      prefix_[i + 1] = prefix_[i] + (spans_[i].second - spans_[i].first + 1);
    }
  }

  bool FoldView::hidden(int line) const
  {
    if (spans_.empty() || line < spans_.front().first)
    {
      return false;
    }
    auto it = std::upper_bound(spans_.begin(),
                               spans_.end(),
                               std::make_pair(line, std::numeric_limits<int>::max()));
    if (it == spans_.begin())
    {
      return false;
    }
    --it;
    return line <= it->second;
  }

  int FoldView::hidden_before(int limit) const
  {
    if (limit <= 0 || spans_.empty())
    {
      return 0;
    }
    auto it = std::lower_bound(spans_.begin(),
                               spans_.end(),
                               std::make_pair(limit, std::numeric_limits<int>::min()));
    size_t idx = (size_t)(it - spans_.begin());
    if (idx == 0)
    {
      return 0;
    }
    int total = prefix_[idx];
    const auto &last = spans_[idx - 1];
    if (last.second >= limit)
    {
      total -= last.second - limit + 1;
    }
    return total;
  }

  bool FoldView::folded_header(int line, int *range_index) const
  {
    auto it = std::lower_bound(headers_.begin(),
                               headers_.end(),
                               line,
                               [](const Header &header, int value)
                               { return header.start_line < value; });
    if (it == headers_.end() || it->start_line != line)
    {
      return false;
    }
    if (range_index)
    {
      *range_index = it->range_index;
    }
    return true;
  }

  int FoldView::hidden_count_for_header(int line) const
  {
    auto it = std::lower_bound(headers_.begin(),
                               headers_.end(),
                               line,
                               [](const Header &header, int value)
                               { return header.start_line < value; });
    if (it == headers_.end() || it->start_line != line)
    {
      return 0;
    }
    return std::max(0, it->end_line - it->start_line);
  }

  int FoldView::first_visible_at_or_after(int line, int line_count) const
  {
    if (line_count <= 0 || line >= line_count)
    {
      return -1;
    }
    int current = std::max(0, line);
    while (current < line_count && hidden(current))
    {
      // Jump to the end of the span hiding `current` instead of stepping one
      // line at a time: a collapsed block can cover most of the buffer.
      auto it = std::upper_bound(spans_.begin(),
                                 spans_.end(),
                                 std::make_pair(current, std::numeric_limits<int>::max()));
      --it;
      current = it->second + 1;
    }
    return current < line_count ? current : -1;
  }

  int FoldView::rank_of_line(int line) const
  {
    return line - hidden_before(line);
  }

  int FoldView::line_at_rank(int rank, int line_count) const
  {
    if (rank < 0 || line_count <= 0)
    {
      return -1;
    }
    int remaining = rank;
    int cursor = 0;
    for (const auto &span : spans_)
    {
      const int gap_visible = span.first - cursor; // lines cursor..span.first-1
      if (remaining < gap_visible)
      {
        const int line = cursor + remaining;
        return line < line_count ? line : -1;
      }
      remaining -= gap_visible;
      cursor = span.second + 1;
    }
    const int line = cursor + remaining;
    return line < line_count ? line : -1;
  }

  int FoldView::visible_rank_of_line(int line) const
  {
    const int below = std::max(0, line);
    return below - hidden_before(below);
  }

  int FoldView::visible_line_count(int line_count) const
  {
    if (spans_.empty())
    {
      return std::max(1, line_count);
    }
    return std::max(1, line_count - hidden_before(line_count));
  }

  int FoldView::buffer_line_for_visible_index(int visible_index, int line_count) const
  {
    const int target = std::max(0, visible_index);
    const int line = line_at_rank(target, line_count);
    if (line >= 0)
    {
      return line;
    }
    return std::max(0, line_count - 1);
  }

  int FoldView::buffer_line_for_visible_offset(int first_line, int offset, int line_count) const
  {
    // first_line arrives from a scroll position and is clamped to the last
    // line before the walk starts, the way the callers have always treated it.
    const int clamped_first = std::clamp(first_line, 0, std::max(0, line_count - 1));
    const int start = first_visible_at_or_after(clamped_first, line_count);
    if (start < 0)
    {
      return -1;
    }
    if (offset <= 0)
    {
      return start;
    }
    return line_at_rank(rank_of_line(start) + offset, line_count);
  }

  int FoldView::visible_row_for_line(int first_line,
                                     int target_line,
                                     int visible_rows,
                                     int line_count) const
  {
    if (line_count <= 0 || visible_rows <= 0 || target_line < 0 || target_line >= line_count
        || hidden(target_line))
    {
      return -1;
    }
    const int first_visible = first_visible_at_or_after(
        std::clamp(first_line, 0, std::max(0, line_count - 1)), line_count);
    if (first_visible < 0)
    {
      return -1;
    }
    const int row = rank_of_line(target_line) - rank_of_line(first_visible);
    return (row >= 0 && row < visible_rows) ? row : -1;
  }

  int FoldView::next_visible_line(int line, int line_count) const
  {
    const int last = std::max(0, line_count - 1);
    const int next = std::min(line + 1, last);
    const int found = first_visible_at_or_after(next, line_count);
    return found < 0 ? last : found;
  }

  int FoldView::previous_visible_line(int line) const
  {
    int prev = std::max(0, line - 1);
    while (prev > 0 && hidden(prev))
    {
      auto it = std::upper_bound(spans_.begin(),
                                 spans_.end(),
                                 std::make_pair(prev, std::numeric_limits<int>::max()));
      --it;
      prev = it->first - 1;
    }
    return std::max(0, prev);
  }

  int FoldView::advance_visible_lines(int line, int delta, int line_count) const
  {
    int current = std::clamp(line, 0, std::max(0, line_count - 1));
    const int steps = std::abs(delta);
    for (int i = 0; i < steps; i++)
    {
      const int next = delta > 0 ? next_visible_line(current, line_count)
                                 : previous_visible_line(current);
      if (next == current)
      {
        break;
      }
      current = next;
    }
    return current;
  }

  int FoldView::clamp_scroll_offset(int scroll, int visible_rows, int line_count) const
  {
    if (line_count <= 0)
    {
      return 0;
    }
    const int last = std::max(0, line_count - 1);
    // A scroll offset inside a collapsed block anchors on the line above it,
    // never on a hidden line (the old back-walk).
    int clamped = std::clamp(scroll, 0, last);
    if (hidden(clamped))
    {
      clamped = previous_visible_line(clamped + 1);
    }
    const int visible_total =
        spans_.empty() ? line_count : std::max(1, line_count - hidden_before(line_count));
    const int max_visible_start = std::max(0, visible_total - std::max(1, visible_rows));
    const int visible_before_clamped = spans_.empty() ? clamped : clamped - hidden_before(clamped);
    if (visible_before_clamped <= max_visible_start)
    {
      return clamped;
    }
    // Viewport would start below the last visible row: anchor it at the line
    // whose visible rank is max_visible_start (the highest allowed start).
    const int line = line_at_rank(max_visible_start, line_count);
    return line >= 0 ? line : clamped;
  }

  std::vector<FoldRange> detect_ranges(const std::vector<std::string> &lines,
                                       const std::string &extension)
  {
    if (lines.size() < 2)
    {
      return {};
    }
    if (Language::is_indentation_language(extension))
    {
      return detect_indent_ranges(lines);
    }
    return detect_brace_ranges(lines);
  }

  void refresh_ranges(std::vector<FoldRange> &ranges,
                      const std::vector<std::string> &lines,
                      const std::string &extension)
  {
    std::set<std::pair<int, int>> collapsed;
    for (const auto &range : ranges)
    {
      if (range.collapsed)
      {
        collapsed.insert({range.start_line, range.end_line});
      }
    }
    ranges = detect_ranges(lines, extension);
    for (auto &range : ranges)
    {
      range.collapsed = collapsed.count({range.start_line, range.end_line}) > 0;
    }
  }

  std::string encode_collapsed_ranges(const std::vector<FoldRange> &ranges)
  {
    std::ostringstream out;
    bool first = true;
    for (const auto &range : ranges)
    {
      if (!range.collapsed || range.end_line <= range.start_line)
      {
        continue;
      }
      if (!first)
      {
        out << ",";
      }
      first = false;
      out << range.start_line << "-" << range.end_line;
    }
    return out.str();
  }

  std::vector<FoldRange> decode_collapsed_ranges(const std::string &payload)
  {
    std::vector<FoldRange> ranges;
    std::stringstream ss(payload);
    std::string item;
    while (std::getline(ss, item, ','))
    {
      size_t dash = item.find('-');
      if (dash == std::string::npos)
      {
        continue;
      }
      try
      {
        int start = std::stoi(item.substr(0, dash));
        int end = std::stoi(item.substr(dash + 1));
        if (start >= 0 && end > start)
        {
          ranges.push_back({start, end, true});
        }
      }
      catch (...)
      {
      }
    }
    return ranges;
  }

  void apply_collapsed_ranges(std::vector<FoldRange> &ranges,
                              const std::vector<FoldRange> &collapsed)
  {
    std::set<std::pair<int, int>> wanted;
    for (const auto &range : collapsed)
    {
      if (range.collapsed)
      {
        wanted.insert({range.start_line, range.end_line});
      }
    }
    for (auto &range : ranges)
    {
      range.collapsed = wanted.count({range.start_line, range.end_line}) > 0;
    }
  }

  int fold_at_or_before_line(const std::vector<FoldRange> &ranges, int line)
  {
    int best = -1;
    int best_start = -1;
    int best_len = 0;
    for (int i = 0; i < (int)ranges.size(); i++)
    {
      const auto &range = ranges[i];
      if (range.start_line <= line && line <= range.end_line && range.start_line >= best_start)
      {
        int len = range.end_line - range.start_line;
        if (range.start_line > best_start || best < 0 || len < best_len)
        {
          best = i;
          best_start = range.start_line;
          best_len = len;
        }
      }
    }
    return best;
  }

  int fold_starting_at_line(const std::vector<FoldRange> &ranges, int line)
  {
    int best = -1;
    int best_len = 0;
    for (int i = 0; i < (int)ranges.size(); i++)
    {
      const auto &range = ranges[i];
      if (range.start_line != line)
      {
        continue;
      }
      int len = range.end_line - range.start_line;
      if (best < 0 || len > best_len)
      {
        best = i;
        best_len = len;
      }
    }
    return best;
  }

  // The predicates stay hand-rolled scans: they answer the one-shot questions
  // (is the caret on a hidden line, is this row a folded header), and a scan
  // over the range vector is cheaper than preparing a whole view for a single
  // answer. Callers that ask per visible row use Folding::FoldView instead.
  bool is_line_hidden(const std::vector<FoldRange> &ranges, int line)
  {
    for (const auto &range : ranges)
    {
      if (range.collapsed && line > range.start_line && line <= range.end_line)
      {
        return true;
      }
    }
    return false;
  }

  bool is_line_folded_header(const std::vector<FoldRange> &ranges, int line, int *range_index)
  {
    for (int i = 0; i < (int)ranges.size(); i++)
    {
      const auto &range = ranges[i];
      if (range.collapsed && range.start_line == line)
      {
        if (range_index)
        {
          *range_index = i;
        }
        return true;
      }
    }
    return false;
  }

  int hidden_line_count_for_header(const std::vector<FoldRange> &ranges, int line)
  {
    int index = -1;
    if (!is_line_folded_header(ranges, line, &index))
    {
      return 0;
    }
    return std::max(0, ranges[index].end_line - ranges[index].start_line);
  }

  int next_visible_line(const std::vector<FoldRange> &ranges, int line, int line_count)
  {
    const int last = std::max(0, line_count - 1);
    const int next = std::min(line + 1, last);
    const int found = skip_hidden_forward(ranges, next, line_count);
    return found < line_count ? std::min(found, last) : last;
  }

  int previous_visible_line(const std::vector<FoldRange> &ranges, int line)
  {
    int prev = std::max(0, line - 1);
    while (prev > 0)
    {
      const int jumped = skip_hidden_backward(ranges, prev);
      if (jumped == prev)
      {
        break;
      }
      prev = jumped;
    }
    return std::max(0, prev);
  }

  int advance_visible_lines(const std::vector<FoldRange> &ranges,
                            int line,
                            int delta,
                            int line_count)
  {
    // One-shot callers (a wrap, a jump) get the answer from a prepared view,
    // which indexes the collapsed ranges once instead of re-scanning the range
    // vector for every line the step crosses. Anyone moving the viewport
    // repeatedly -- the wheel -- should hold one view for the whole gesture
    // rather than rebuild it per step (see the overload in the header).
    return FoldView(ranges).advance_visible_lines(line, delta, line_count);
  }

  int visible_line_count(const std::vector<FoldRange> &ranges, int line_count)
  {
    return FoldView(ranges).visible_line_count(line_count);
  }

  int buffer_line_for_visible_index(const std::vector<FoldRange> &ranges,
                                    int visible_index,
                                    int line_count)
  {
    return FoldView(ranges).buffer_line_for_visible_index(visible_index, line_count);
  }

  int visible_row_for_line(const std::vector<FoldRange> &ranges,
                           int first_line,
                           int target_line,
                           int visible_rows,
                           int line_count)
  {
    return FoldView(ranges).visible_row_for_line(
        first_line, target_line, visible_rows, line_count);
  }

  int buffer_line_for_visible_offset(const std::vector<FoldRange> &ranges,
                                     int first_line,
                                     int offset,
                                     int line_count)
  {
    return FoldView(ranges).buffer_line_for_visible_offset(first_line, offset, line_count);
  }

  int clamp_scroll_offset(const std::vector<FoldRange> &ranges,
                          int scroll,
                          int visible_rows,
                          int line_count)
  {
    return FoldView(ranges).clamp_scroll_offset(scroll, visible_rows, line_count);
  }
} // namespace Folding
