// Smart indentation for the C family (src/features/text_features.*), as Enter
// and the typing of a label reach it from src/text/edit.cpp.
//
// Most of this is the pure rule: the indent the next line starts at, for a
// given line and caret, and which typed lines are labels. The editor cases then
// pin the two ends -- the line Enter actually creates and the dedent that
// happens while a label is being typed -- against a real .cpp on disk, because
// the file's language is what selects the C-family rules.

#include "core/editor.h"
#include "text/text_features.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  // A config home and a directory for the buffers, both cut fresh for the run.
  const std::string &scratch_dir()
  {
    static const std::string dir = [] {
      char home[] = "/tmp/jot_cpp_indent_XXXXXX";
      const std::string path = mkdtemp(home);
      setenv("JOT_CONFIG_HOME", path.c_str(), 1);
      setenv("JOT_CACHE_HOME", path.c_str(), 1);
      return path;
    }();
    return dir;
  }

  // The indent of the line Enter would create, asked for the way the editor
  // asks: the file, the line's text, the caret's column. The project's tab size.
  int indent_after(const std::string &path, const std::string &line, int caret_col, int tab_size = 2)
  {
    return EditorFeatures::indent_for_new_line(path, line, caret_col, tab_size);
  }

  // One editor, built after the config home is the test's own: auto-indent is a
  // config value, so the config it reads must not be the developer's.
  Editor &probe_editor()
  {
    scratch_dir();
    static Editor e;
    return e;
  }

  void type_text(Editor &e, const std::string &text)
  {
    for (char c : text)
      e.host().core.insert_char_at_carets(c);
  }

  // A .cpp on disk, since the language is what picks the C-family rules out.
  void open_cpp(Editor &e, const std::string &text, int line, int col)
  {
    scratch_dir();
    static int counter = 0;
    const std::string path = scratch_dir() + "/" + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.scroll_cursor_to_for_test(line, col);
  }
} // namespace

TEST_CASE("C++ indent: a continuation lines up under the argument it continues",
          "[jot][indent]")
{
  REQUIRE(indent_after("a.cpp", "foo(alpha,", 10) == 4);
  REQUIRE(indent_after("a.cpp", "  res = call(arg,", 17) == 13);
  // The opener the line ends on has no argument after it, so the new line steps
  // in a level instead of lining up with nothing.
  REQUIRE(indent_after("a.cpp", "foo(", 4) == 2);
  REQUIRE(indent_after("a.cpp", "  foo(  ", 7) == 4);
  // A brace opens a body rather than an argument list.
  REQUIRE(indent_after("a.cpp", "auto v = std::vector<int>{", 26) == 2);
  REQUIRE(indent_after("a.cpp", "if (x) {", 8) == 2);
  // Brackets in a literal or a comment are text, and the line leaves none open.
  REQUIRE(indent_after("a.cpp", "printf(\"a(\");", 13) == 0);
  REQUIRE(indent_after("a.cpp", "// foo(", 7) == 0);
}

TEST_CASE("C++ indent: a control statement does not indent, its brace does", "[jot][indent]")
{
  REQUIRE(indent_after("a.cpp", "if (x)", 6) == 0);
  REQUIRE(indent_after("a.cpp", "  else", 6) == 2);
  REQUIRE(indent_after("a.cpp", "  switch (x)", 12) == 2);
  REQUIRE(indent_after("a.cpp", "class Widget", 12) == 0);
  REQUIRE(indent_after("a.cpp", "  int x = 1;", 12) == 2);
  // A language whose brace goes on the statement's own line keeps stepping in.
  REQUIRE(indent_after("a.js", "if (x)", 6) == 2);
  REQUIRE(indent_after("a.py", "if x:", 5) == 2);
}

TEST_CASE("C++ indent: a label introduces a body and belongs one level out of it",
          "[jot][indent]")
{
  REQUIRE(indent_after("a.cpp", "case 1:", 7) == 2);
  REQUIRE(indent_after("a.cpp", "public:", 7) == 2);
  REQUIRE(indent_after("a.cpp", "  default:", 10) == 4);

  REQUIRE(EditorFeatures::should_cpp_dedent("  case 1:"));
  REQUIRE(EditorFeatures::should_cpp_dedent("    case 'a':"));
  REQUIRE(EditorFeatures::should_cpp_dedent("  default:"));
  REQUIRE(EditorFeatures::should_cpp_dedent("    public:"));
  REQUIRE(EditorFeatures::should_cpp_dedent("private:"));
  REQUIRE(EditorFeatures::should_cpp_dedent("protected:"));
  REQUIRE(EditorFeatures::should_cpp_dedent("case 1: {"));
  // A name that merely starts with the word, or a line that ends in a colon for
  // another reason, is not a label.
  REQUIRE_FALSE(EditorFeatures::should_cpp_dedent("casey:"));
  REQUIRE_FALSE(EditorFeatures::should_cpp_dedent("publicity:"));
  REQUIRE_FALSE(EditorFeatures::should_cpp_dedent("  x = y ? a :"));
  REQUIRE_FALSE(EditorFeatures::should_cpp_dedent("  int x;"));

  REQUIRE(EditorFeatures::is_preprocessor_directive_start("  #"));
  REQUIRE_FALSE(EditorFeatures::is_preprocessor_directive_start("  #include <string>"));
  REQUIRE_FALSE(EditorFeatures::is_preprocessor_directive_start("x # y"));
}

TEST_CASE("Enter after a control statement leaves its brace on the statement's line",
          "[jot][indent]")
{
  Editor &e = probe_editor();
  open_cpp(e, "if (x)", 0, 6);
  e.new_line_for_test();

  auto &core = e.host().core;
  REQUIRE(core.buffer_content() == "if (x)\n");
  REQUIRE(core.cursor() == std::make_pair(1, 0));

  // The brace that follows is auto-closed, and splitting it produces the body
  // one level in and the closing brace back at the `if`.
  type_text(e, "{");
  REQUIRE(core.buffer_content() == "if (x)\n{}");
  // Splitting the pair leaves the brace on the `if`'s own line, the body one
  // level in, and the closing brace back at the `if`.
  e.new_line_for_test();
  REQUIRE(core.buffer_content() == "if (x)\n{\n  \n}");
  REQUIRE(core.cursor() == std::make_pair(2, 2));

  type_text(e, "return 0;");
  REQUIRE(core.buffer_content() == "if (x)\n{\n  return 0;\n}");
  REQUIRE(core.cursor() == std::make_pair(2, 11));
}

TEST_CASE("Enter inside a call lines the new line up under its argument", "[jot][indent]")
{
  Editor &e = probe_editor();
  open_cpp(e, "  res = call(arg,", 0, 17);
  e.new_line_for_test();

  auto &core = e.host().core;
  REQUIRE(core.buffer_content() == "  res = call(arg,\n             ");
  REQUIRE(core.cursor() == std::make_pair(1, 13));
}

TEST_CASE("Typing an access specifier pulls the line back out of the class body",
          "[jot][indent]")
{
  Editor &e = probe_editor();
  open_cpp(e, "class Widget\n{\n  ", 2, 2);
  type_text(e, "public:");

  auto &core = e.host().core;
  REQUIRE(core.buffer_content() == "class Widget\n{\npublic:");
  REQUIRE(core.cursor() == std::make_pair(2, 7));

  e.new_line_for_test();
  REQUIRE(core.buffer_content() == "class Widget\n{\npublic:\n  ");
}

TEST_CASE("Typing a case label pulls it back to the switch's level", "[jot][indent]")
{
  Editor &e = probe_editor();
  open_cpp(e, "switch (x)\n{\n  ", 2, 2);
  type_text(e, "case 1:");

  auto &core = e.host().core;
  REQUIRE(core.buffer_content() == "switch (x)\n{\ncase 1:");
  REQUIRE(core.cursor() == std::make_pair(2, 7));
}

TEST_CASE("Typing # takes the line back to column 0", "[jot][indent]")
{
  Editor &e = probe_editor();
  open_cpp(e, "int main()\n{\n    ", 2, 4);
  type_text(e, "#");

  auto &core = e.host().core;
  REQUIRE(core.buffer_content() == "int main()\n{\n#");
  REQUIRE(core.cursor() == std::make_pair(2, 1));
}
