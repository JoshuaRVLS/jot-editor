#ifndef EDITOR_FEATURES_H
#define EDITOR_FEATURES_H

#include <string>
#include <vector>

struct Diagnostic
{
  int line;
  int col;
  int end_line;
  int end_col;
  std::string message;
  int severity; // 1=Error, 2=Warning, 3=Info, 4=Hint
};

class EditorFeatures
{
public:
  static int get_indent_level(const std::string &line);
  static std::string get_indent_string(int level, int tab_size);
  static bool should_auto_indent(const std::string &line);
  static bool should_dedent(const std::string &line);
  static bool should_python_auto_indent(const std::string &line);
  static bool should_python_dedent(const std::string &line);
  static bool should_lua_auto_indent(const std::string &line);
  static bool should_lua_dedent(const std::string &line);

  // The indent level of the line Enter creates when it is pressed at
  // `caret_col` on `line`, for the language of `path`. A bracket left open on
  // the line lines the new line up under the argument that follows it; a line
  // that opens a body steps in one level. A C-family control statement does
  // not: in the project's brace style its `{` stands on the next line at the
  // statement's own indent.
  static int indent_for_new_line(const std::string &path,
                                 const std::string &line,
                                 int caret_col,
                                 int tab_size);

  // Whether a line that has just been typed is a label belonging one level out
  // from the body it introduces: `case ...:` and `default:`, which
  // clang-format leaves unindented (IndentCaseLabels: false), or an access
  // specifier.
  static bool should_cpp_dedent(const std::string &line);

  // Whether a just-typed line is nothing but the `#` a preprocessor directive
  // starts with, which clang-format leaves at column 0.
  static bool is_preprocessor_directive_start(const std::string &line);
  static int find_matching_bracket(
      const std::vector<std::string> &lines, int line, int col, char open, char close);
  static void format_line(std::string &line, int tab_size);
  static std::string trim_right(const std::string &s);
  static bool is_whitespace(const std::string &s);
};

#endif // EDITOR_FEATURES_H
