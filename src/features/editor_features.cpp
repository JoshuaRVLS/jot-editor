#include "editor_features.h"
#include <algorithm>
#include <cctype>

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
  return std::string(level, ' ');
}

bool EditorFeatures::should_auto_indent(const std::string &line) {
  std::string trimmed = line;
  size_t start = trimmed.find_first_not_of(" \t");
  if (start == std::string::npos)
    return false;
  trimmed.erase(0, start);

  // Remove comments
  size_t comment_pos = trimmed.find("//");
  if (comment_pos != std::string::npos) {
    trimmed = trimmed.substr(0, comment_pos);
    // Trim right again after removing comment
    size_t end = trimmed.find_last_not_of(" \t");
    if (end == std::string::npos)
      return false;
    trimmed = trimmed.substr(0, end + 1);
  }

  if (trimmed.empty())
    return false;

  // Check for block starters
  char last_char = trimmed.back();
  if (last_char == '{' || last_char == ':' || last_char == '[')
    return true;

  // Check for specific keywords
  static const std::vector<std::string> keywords = {
      "if",     "for",  "while",   "else", "def",  "class",
      "switch", "case", "default", "try",  "catch"};

  for (const auto &kw : keywords) {
    if (trimmed.compare(0, kw.length(), kw) == 0) {
      // Check if it's the whole string or followed by a non-identifier char
      if (trimmed.length() == kw.length())
        return true;
      char next_char = trimmed[kw.length()];
      if (!isalnum(next_char) && next_char != '_')
        return true;
    }
  }

  return false;
}

bool EditorFeatures::should_dedent(const std::string &line) {
  std::string trimmed = line;
  size_t start = trimmed.find_first_not_of(" \t");
  if (start == std::string::npos)
    return false;
  trimmed.erase(0, start);

  if (trimmed.empty())
    return false;

  // Dedent if the line starts with a closing brace/bracket
  if (trimmed.front() == '}' || trimmed.front() == ']' ||
      trimmed.front() == ')')
    return true;

  // Dedent for specific keywords that continue a block but are indented less
  // (like 'else', 'catch') in some styles, though often 'else' is on the same
  // line as '}' or indented to match 'if'. If we assume K&R or 1TBS, 'else' is
  // after '}', but if it's on a new line it usually matches the 'if'.
  if (trimmed.compare(0, 4, "else") == 0 ||
      trimmed.compare(0, 5, "catch") == 0 ||
      trimmed.compare(0, 7, "finally") == 0 ||
      trimmed.compare(0, 4, "case") == 0 ||
      trimmed.compare(0, 7, "default") == 0) {
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
