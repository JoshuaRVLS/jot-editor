// The pure half of the editor's line commands (see text/line_ops.h for what
// each one is and who owns the rest of the command).
//
// Every function here derives its range from the arguments and never reads or
// writes anything outside `lines`, so a caller can hand it a buffer that is
// still lazy: the editor materialises first, and none of this has to know.
#include "line_ops.h"
#include "base/string_util.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <unordered_set>

namespace
{
  bool is_blank_line(const std::string &line)
  {
    for (char c : line)
    {
      if (!std::isspace((unsigned char)c))
      {
        return false;
      }
    }
    return true;
  }

  // The key a sort compares on: the line folded to lower case, so `Apple` sorts
  // next to `apple` rather than with the capitals.
  std::string folded(const std::string &line)
  {
    std::string out = line;
    std::transform(
        out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
  }

  // Whether [first, last] names a range that exists inside `lines` and has more
  // than one line in it -- what every edit below needs before it starts.
  bool usable_range(const std::vector<std::string> &lines, int first, int last)
  {
    return first >= 0 && last < (int)lines.size() && first < last;
  }
} // namespace

void line_ops::sort_lines(std::vector<std::string> &lines, int first, int last, bool descending)
{
  if (!usable_range(lines, first, last))
  {
    return;
  }
  std::stable_sort(lines.begin() + first,
                   lines.begin() + last + 1,
                   [descending](const std::string &a, const std::string &b)
                   {
                     const std::string left = folded(a);
                     const std::string right = folded(b);
                     return descending ? left > right : left < right;
                   });
}

void line_ops::reverse_lines(std::vector<std::string> &lines, int first, int last)
{
  if (!usable_range(lines, first, last))
  {
    return;
  }
  std::reverse(lines.begin() + first, lines.begin() + last + 1);
}

void line_ops::shuffle_lines(std::vector<std::string> &lines, int first, int last)
{
  if (!usable_range(lines, first, last))
  {
    return;
  }
  std::random_device device;
  std::mt19937 generator(device());
  std::shuffle(lines.begin() + first, lines.begin() + last + 1, generator);
}

int line_ops::remove_duplicate_lines(std::vector<std::string> &lines, int first, int last)
{
  if (!usable_range(lines, first, last))
  {
    return 0;
  }
  std::unordered_set<std::string> seen;
  int removed = 0;
  // `last` walks backwards with the erases, and the loop still tests against the
  // buffer's length because each erase shrinks it.
  for (int y = first; y <= last && y < (int)lines.size();)
  {
    if (seen.insert(lines[(size_t)y]).second)
    {
      y++;
      continue;
    }
    lines.erase(lines.begin() + y);
    last--;
    removed++;
  }
  return removed;
}

int line_ops::remove_blank_lines(std::vector<std::string> &lines, int first, int last)
{
  if (first < 0 || last >= (int)lines.size() || first > last)
  {
    return 0;
  }
  int removed = 0;
  for (int y = last; y >= first && y < (int)lines.size(); y--)
  {
    if (!is_blank_line(lines[(size_t)y]))
    {
      continue;
    }
    // A buffer with nothing left in it is not a buffer: the last line stays even
    // when it is blank.
    if ((int)lines.size() == 1)
    {
      break;
    }
    lines.erase(lines.begin() + y);
    removed++;
  }
  return removed;
}

int line_ops::join_lines(std::vector<std::string> &lines, int first, int last)
{
  if (!usable_range(lines, first, last))
  {
    return 0;
  }
  int joins = 0;
  for (int y = first; y < last && y + 1 < (int)lines.size();)
  {
    std::string left = lines[(size_t)y];
    const std::string right = string_util::ltrim_copy(lines[(size_t)y + 1]);
    if (!left.empty() && !right.empty() && !std::isspace((unsigned char)left.back()))
    {
      left.push_back(' ');
    }
    lines[(size_t)y] = left + right;
    lines.erase(lines.begin() + y + 1);
    last--;
    joins++;
  }
  return joins;
}
