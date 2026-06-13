#include "folding.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stack>
#include <sstream>

namespace {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

bool indentation_language(const std::string &extension) {
  std::string ext = lower_copy(extension);
  return ext == ".py" || ext == ".yaml" || ext == ".yml" || ext == ".md" ||
         ext == ".markdown";
}

int indent_level(const std::string &line) {
  int indent = 0;
  for (char c : line) {
    if (c == ' ') {
      indent++;
    } else if (c == '\t') {
      indent += 4;
    } else {
      break;
    }
  }
  return indent;
}

bool blank_or_comment_only(const std::string &line) {
  size_t first = line.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return true;
  }
  return line[first] == '#';
}

std::vector<FoldRange>
detect_indent_ranges(const std::vector<std::string> &lines) {
  std::vector<FoldRange> ranges;
  const int n = (int)lines.size();
  for (int i = 0; i < n - 1; i++) {
    if (blank_or_comment_only(lines[i])) {
      continue;
    }
    int base_indent = indent_level(lines[i]);
    int first_child = -1;
    for (int j = i + 1; j < n; j++) {
      if (blank_or_comment_only(lines[j])) {
        continue;
      }
      if (indent_level(lines[j]) > base_indent) {
        first_child = j;
      }
      break;
    }
    if (first_child < 0) {
      continue;
    }
    int end = first_child;
    for (int j = first_child + 1; j < n; j++) {
      if (blank_or_comment_only(lines[j])) {
        end = j;
        continue;
      }
      if (indent_level(lines[j]) <= base_indent) {
        break;
      }
      end = j;
    }
    if (end > i) {
      ranges.push_back({i, end, false});
    }
  }
  return ranges;
}

std::vector<FoldRange>
detect_brace_ranges(const std::vector<std::string> &lines) {
  std::vector<FoldRange> ranges;
  std::vector<int> stack;
  bool in_block_comment = false;

  for (int y = 0; y < (int)lines.size(); y++) {
    const std::string &line = lines[y];
    bool in_string = false;
    char quote = 0;
    bool escaped = false;
    for (int x = 0; x < (int)line.size(); x++) {
      char c = line[x];
      char next = (x + 1 < (int)line.size()) ? line[x + 1] : '\0';
      if (in_block_comment) {
        if (c == '*' && next == '/') {
          in_block_comment = false;
          x++;
        }
        continue;
      }
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == quote) {
          in_string = false;
        }
        continue;
      }
      if (c == '/' && next == '/') {
        break;
      }
      if (c == '/' && next == '*') {
        in_block_comment = true;
        x++;
        continue;
      }
      if (c == '"' || c == '\'') {
        in_string = true;
        quote = c;
        continue;
      }
      if (c == '{') {
        stack.push_back(y);
      } else if (c == '}' && !stack.empty()) {
        int start = stack.back();
        stack.pop_back();
        if (y > start) {
          ranges.push_back({start, y, false});
        }
      }
    }
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const FoldRange &a, const FoldRange &b) {
              if (a.start_line != b.start_line) {
                return a.start_line < b.start_line;
              }
              return a.end_line > b.end_line;
            });
  return ranges;
}

bool range_contains_line(const FoldRange &range, int line) {
  return range.collapsed && line > range.start_line && line <= range.end_line;
}
} // namespace

namespace Folding {
std::vector<FoldRange> detect_ranges(const std::vector<std::string> &lines,
                                     const std::string &extension) {
  if (lines.size() < 2) {
    return {};
  }
  if (indentation_language(extension)) {
    return detect_indent_ranges(lines);
  }
  return detect_brace_ranges(lines);
}

void refresh_ranges(std::vector<FoldRange> &ranges,
                    const std::vector<std::string> &lines,
                    const std::string &extension) {
  std::set<std::pair<int, int>> collapsed;
  for (const auto &range : ranges) {
    if (range.collapsed) {
      collapsed.insert({range.start_line, range.end_line});
    }
  }
  ranges = detect_ranges(lines, extension);
  for (auto &range : ranges) {
    range.collapsed =
        collapsed.count({range.start_line, range.end_line}) > 0;
  }
}

std::string encode_collapsed_ranges(const std::vector<FoldRange> &ranges) {
  std::ostringstream out;
  bool first = true;
  for (const auto &range : ranges) {
    if (!range.collapsed || range.end_line <= range.start_line) {
      continue;
    }
    if (!first) {
      out << ",";
    }
    first = false;
    out << range.start_line << "-" << range.end_line;
  }
  return out.str();
}

std::vector<FoldRange> decode_collapsed_ranges(const std::string &payload) {
  std::vector<FoldRange> ranges;
  std::stringstream ss(payload);
  std::string item;
  while (std::getline(ss, item, ',')) {
    size_t dash = item.find('-');
    if (dash == std::string::npos) {
      continue;
    }
    try {
      int start = std::stoi(item.substr(0, dash));
      int end = std::stoi(item.substr(dash + 1));
      if (start >= 0 && end > start) {
        ranges.push_back({start, end, true});
      }
    } catch (...) {
    }
  }
  return ranges;
}

void apply_collapsed_ranges(std::vector<FoldRange> &ranges,
                            const std::vector<FoldRange> &collapsed) {
  std::set<std::pair<int, int>> wanted;
  for (const auto &range : collapsed) {
    if (range.collapsed) {
      wanted.insert({range.start_line, range.end_line});
    }
  }
  for (auto &range : ranges) {
    range.collapsed = wanted.count({range.start_line, range.end_line}) > 0;
  }
}

int fold_at_or_before_line(const std::vector<FoldRange> &ranges, int line) {
  int best = -1;
  int best_start = -1;
  int best_len = 0;
  for (int i = 0; i < (int)ranges.size(); i++) {
    const auto &range = ranges[i];
    if (range.start_line <= line && line <= range.end_line &&
        range.start_line >= best_start) {
      int len = range.end_line - range.start_line;
      if (range.start_line > best_start || best < 0 || len < best_len) {
        best = i;
        best_start = range.start_line;
        best_len = len;
      }
    }
  }
  return best;
}

int fold_starting_at_line(const std::vector<FoldRange> &ranges, int line) {
  int best = -1;
  int best_len = 0;
  for (int i = 0; i < (int)ranges.size(); i++) {
    const auto &range = ranges[i];
    if (range.start_line != line) {
      continue;
    }
    int len = range.end_line - range.start_line;
    if (best < 0 || len > best_len) {
      best = i;
      best_len = len;
    }
  }
  return best;
}

bool is_line_hidden(const std::vector<FoldRange> &ranges, int line) {
  for (const auto &range : ranges) {
    if (range_contains_line(range, line)) {
      return true;
    }
  }
  return false;
}

bool is_line_folded_header(const std::vector<FoldRange> &ranges, int line,
                           int *range_index) {
  for (int i = 0; i < (int)ranges.size(); i++) {
    const auto &range = ranges[i];
    if (range.collapsed && range.start_line == line) {
      if (range_index) {
        *range_index = i;
      }
      return true;
    }
  }
  return false;
}

int hidden_line_count_for_header(const std::vector<FoldRange> &ranges,
                                 int line) {
  int index = -1;
  if (!is_line_folded_header(ranges, line, &index)) {
    return 0;
  }
  return std::max(0, ranges[index].end_line - ranges[index].start_line);
}

int next_visible_line(const std::vector<FoldRange> &ranges, int line,
                      int line_count) {
  int next = std::min(line + 1, std::max(0, line_count - 1));
  while (next < line_count && is_line_hidden(ranges, next)) {
    next++;
  }
  return std::min(next, std::max(0, line_count - 1));
}

int previous_visible_line(const std::vector<FoldRange> &ranges, int line) {
  int prev = std::max(0, line - 1);
  while (prev > 0 && is_line_hidden(ranges, prev)) {
    prev--;
  }
  return prev;
}

int advance_visible_lines(const std::vector<FoldRange> &ranges, int line,
                          int delta, int line_count) {
  int current = std::clamp(line, 0, std::max(0, line_count - 1));
  int steps = std::abs(delta);
  for (int i = 0; i < steps; i++) {
    int next = delta > 0 ? next_visible_line(ranges, current, line_count)
                         : previous_visible_line(ranges, current);
    if (next == current) {
      break;
    }
    current = next;
  }
  return current;
}

int visible_line_count(const std::vector<FoldRange> &ranges, int line_count) {
  int visible = 0;
  for (int line = 0; line < line_count; line++) {
    if (!is_line_hidden(ranges, line)) {
      visible++;
    }
  }
  return std::max(1, visible);
}

int buffer_line_for_visible_index(const std::vector<FoldRange> &ranges,
                                  int visible_index, int line_count) {
  int target = std::max(0, visible_index);
  for (int line = 0; line < line_count; line++) {
    if (is_line_hidden(ranges, line)) {
      continue;
    }
    if (target == 0) {
      return line;
    }
    target--;
  }
  return std::max(0, line_count - 1);
}

int visible_row_for_line(const std::vector<FoldRange> &ranges, int first_line,
                         int target_line, int visible_rows, int line_count) {
  if (line_count <= 0 || visible_rows <= 0 || target_line < 0 ||
      target_line >= line_count || is_line_hidden(ranges, target_line)) {
    return -1;
  }
  int current = std::clamp(first_line, 0, std::max(0, line_count - 1));
  while (current < line_count && is_line_hidden(ranges, current)) {
    current++;
  }
  if (current >= line_count) {
    return -1;
  }
  for (int row = 0; row < visible_rows && current < line_count; row++) {
    if (current == target_line) {
      return row;
    }
    int next = next_visible_line(ranges, current, line_count);
    if (next == current) {
      break;
    }
    current = next;
  }
  return -1;
}

int buffer_line_for_visible_offset(const std::vector<FoldRange> &ranges,
                                   int first_line, int offset,
                                   int line_count) {
  if (line_count <= 0) {
    return -1;
  }
  int current = std::clamp(first_line, 0, std::max(0, line_count - 1));
  while (current < line_count && is_line_hidden(ranges, current)) {
    current++;
  }
  if (current >= line_count) {
    return -1;
  }
  for (int i = 0; i < offset; i++) {
    if (current >= line_count - 1) {
      return -1;
    }
    current = next_visible_line(ranges, current, line_count);
    if (current >= line_count || is_line_hidden(ranges, current)) {
      return -1;
    }
  }
  return current;
}

int clamp_scroll_offset(const std::vector<FoldRange> &ranges, int scroll,
                        int visible_rows, int line_count) {
  if (line_count <= 0) {
    return 0;
  }
  int clamped = std::clamp(scroll, 0, line_count - 1);
  while (clamped > 0 && is_line_hidden(ranges, clamped)) {
    clamped--;
  }
  int visible_total = visible_line_count(ranges, line_count);
  int max_visible_start = std::max(0, visible_total - std::max(1, visible_rows));
  int visible_index = 0;
  for (int line = 0; line < line_count && line < clamped; line++) {
    if (!is_line_hidden(ranges, line)) {
      visible_index++;
    }
  }
  if (visible_index <= max_visible_start) {
    return clamped;
  }
  int target_visible = max_visible_start;
  for (int line = 0; line < line_count; line++) {
    if (is_line_hidden(ranges, line)) {
      continue;
    }
    if (target_visible == 0) {
      return line;
    }
    target_visible--;
  }
  return 0;
}
} // namespace Folding
