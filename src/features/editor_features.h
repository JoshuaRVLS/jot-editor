#ifndef EDITOR_FEATURES_H
#define EDITOR_FEATURES_H

#include <string>
#include <vector>

struct Diagnostic {
  int line;
  int col;
  int end_line;
  int end_col;
  std::string message;
  int severity; // 1=Error, 2=Warning, 3=Info, 4=Hint
};

class EditorFeatures {
public:
  static int get_indent_level(const std::string &line);
  static std::string get_indent_string(int level, int tab_size);
  static bool should_auto_indent(const std::string &line);
  static bool should_dedent(const std::string &line);
  static int find_matching_bracket(const std::vector<std::string> &lines,
                                   int line, int col, char open, char close);
  static void format_line(std::string &line, int tab_size);
  static std::string trim_right(const std::string &s);
  static bool is_whitespace(const std::string &s);
};

#endif // EDITOR_FEATURES_H
