#include "editor_features.h"
#include <algorithm>
#include <cctype>

namespace {
std::string trim_left(const std::string &s) {
  const size_t start = s.find_first_not_of(" \t");
  return start == std::string::npos ? "" : s.substr(start);
}

std::string trim_right_ws(const std::string &s) {
  const size_t end = s.find_last_not_of(" \t");
  return end == std::string::npos ? "" : s.substr(0, end + 1);
}

bool starts_with_keyword(const std::string &line, const std::string &keyword) {
  if (line.size() < keyword.size() ||
      line.compare(0, keyword.size(), keyword) != 0) {
    return false;
  }

  if (line.size() == keyword.size()) {
    return true;
  }

  const unsigned char next = static_cast<unsigned char>(line[keyword.size()]);
  return !std::isalnum(next) && next != '_';
}
} // namespace

int EditorFeatures::get_indent_level(const std::string &line) {
  int level = 0;
  for (char c : line) {
    if (c == ' ')
      level++;
    else if (c == '\t')
      level += 4;
    else
      break;
  }
  return level;
}

std::string EditorFeatures::get_indent_string(int level, int tab_size) {
  if (level <= 0)
    return "";

  const int step = std::max(1, tab_size);
  const int tabs = level / step;
  const int spaces = level % step;
  return std::string(tabs * step + spaces, ' ');
}

bool EditorFeatures::should_auto_indent(const std::string &line) {
  std::string trimmed = trim_left(line);
  if (trimmed.empty())
    return false;

  // Treat trailing whitespace as non-semantic for indent decisions.
  trimmed = trim_right_ws(trimmed);
  if (trimmed.empty())
    return false;

  // Remove comments
  const size_t comment_pos = trimmed.find("//");
  if (comment_pos != std::string::npos) {
    trimmed = trim_right_ws(trimmed.substr(0, comment_pos));
  }

  if (trimmed.empty())
    return false;

  // Check for block starters
  const char last_char = trimmed.back();
  if (last_char == '{' || last_char == '[' || last_char == '(' ||
      last_char == ':')
    return true;

  // Check for specific keywords
  static const std::vector<std::string> keywords = {
      "if",    "for",    "while", "else", "def", "class", "switch",
      "case",  "default", "try",   "catch", "do",  "finally"};

  for (const auto &kw : keywords) {
    if (starts_with_keyword(trimmed, kw))
      return true;
  }

  return false;
}

bool EditorFeatures::should_dedent(const std::string &line) {
  const std::string trimmed = trim_left(line);

  if (trimmed.empty())
    return false;

  // Dedent if the line starts with a closing brace/bracket
  if (trimmed.front() == '}' || trimmed.front() == ']' ||
      trimmed.front() == ')')
    return true;

  if (starts_with_keyword(trimmed, "else") ||
      starts_with_keyword(trimmed, "catch") ||
      starts_with_keyword(trimmed, "finally") ||
      starts_with_keyword(trimmed, "case") ||
      starts_with_keyword(trimmed, "default")) {
    return true;
  }

  return false;
}

int EditorFeatures::find_matching_bracket(const std::vector<std::string> &lines,
                                          int line, int col, char open,
                                          char close) {
  if (line < 0 || line >= (int)lines.size())
    return -1;
  if (col < 0 || col >= (int)lines[line].length())
    return -1;

  if (lines[line][col] != open && lines[line][col] != close)
    return -1;

  // char target = lines[line][col] == open ? close : open; // Unused
  int dir = lines[line][col] == open ? 1 : -1;
  int depth = 1;

  int cur_line = line;
  int cur_col = col;

  while (cur_line >= 0 && cur_line < (int)lines.size()) {
    while (cur_col >= 0 && cur_col < (int)lines[cur_line].length()) {
      if (lines[cur_line][cur_col] == open)
        depth += dir;
      if (lines[cur_line][cur_col] == close)
        depth -= dir;

      if (depth == 0) {
        return cur_line * 10000 + cur_col;
      }

      cur_col += dir;
    }

    if (dir > 0) {
      cur_line++;
      cur_col = 0;
    } else {
      cur_line--;
      if (cur_line >= 0)
        cur_col = lines[cur_line].length() - 1;
    }
  }

  return -1;
}

void EditorFeatures::format_line(std::string &line, int tab_size) {
  size_t pos = 0;
  while ((pos = line.find('\t', pos)) != std::string::npos) {
    line.replace(pos, 1, std::string(tab_size, ' '));
    pos += tab_size;
  }
}

std::string EditorFeatures::trim_right(const std::string &s) {
  size_t end = s.find_last_not_of(" \t\r\n");
  return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

bool EditorFeatures::is_whitespace(const std::string &s) {
  return std::all_of(s.begin(), s.end(),
                     [](char c) { return std::isspace(c); });
}
