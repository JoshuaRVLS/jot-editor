// Paste indent: where a pasted block lands (src/text/clipboard.cpp,
// smart_indent_paste_text). The paste re-bases the block onto the caret's own
// context, using the indent Enter would open there (EditorFeatures::
// indent_for_new_line), so the cases pin what it must not do: glue the block to
// an opener line, repeat the indent the caret already sits after, or ignore the
// language's own step. The probe for the same behavior on a real terminal is
// test/paste_indent_probe.py.
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_paste_indent_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Opens a file with exactly this content and parks the caret at (line, col).
  // The extension is what the paste reads the language from.
  void open_with(Editor &e, const std::string &name, const std::string &content, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_paste_indent_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + name;
    std::ofstream out(path);
    out << content;
    out.close();
    e.load_file(path);
    e.buffer_for_test().cursor.x = col;
    e.buffer_for_test().cursor.y = line;
  }
} // namespace

TEST_CASE("Paste: a block after an opener starts on its own line, inside it", "[jot][paste]")
{
  Editor &e = probe_editor();
  open_with(e, "opener.cpp", "    if (x) {\n}\n", 0, 12);
  e.paste_text_for_test("foo();\nbar();\n");

  // Not glued to the brace, one step (tab_size, 2 by default) in from the
  // loop's own indent, and the block's own common indent is rebased rather
  // than doubled.
  REQUIRE(e.buffer_for_test().line(0) == "    if (x) {");
  REQUIRE(e.buffer_for_test().line(1) == "      foo();");
  REQUIRE(e.buffer_for_test().line(2) == "      bar();");

  // The caret lands on the block's trailing blank line at the block's indent,
  // which is what a line opened with Enter gives it.
  REQUIRE(e.buffer_for_test().cursor.y == 3);
  REQUIRE(e.buffer_for_test().line(3) == "      ");
  REQUIRE(e.buffer_for_test().cursor.x == 6);
}

TEST_CASE("Paste: a block pasted mid-line continues the line without the indent", "[jot][paste]")
{
  Editor &e = probe_editor();
  open_with(e, "midline.cpp", "    x = \n}\n", 0, 8);
  e.paste_text_for_test("foo();\nbar();\n");

  // The first line joins the text already before the caret and takes no indent
  // of its own; the lines after it take the statement's indent.
  REQUIRE(e.buffer_for_test().line(0) == "    x = foo();");
  REQUIRE(e.buffer_for_test().line(1) == "    bar();");
  REQUIRE(e.buffer_for_test().line(2) == "    ");
}

TEST_CASE("Paste: the block's indent follows the language, not the braces", "[jot][paste]")
{
  Editor &e = probe_editor();
  open_with(e, "colon.py", "if x:\n    pass\n", 0, 5);
  e.paste_text_for_test("one()\ntwo()\n");

  // A Python header opens a body for Enter, so it opens one for the paste too;
  // a C-family line like it would land on the next line at the same indent.
  REQUIRE(e.buffer_for_test().line(0) == "if x:");
  REQUIRE(e.buffer_for_test().line(1) == "  one()");
  REQUIRE(e.buffer_for_test().line(2) == "  two()");
}

TEST_CASE("Paste: the block's own indent is rebased, the caret's is not doubled", "[jot][paste]")
{
  Editor &e = probe_editor();
  // An 8-space block into a deeper body: the common indent becomes the block
  // indent the context asks for, and the block's inner steps survive it.
  open_with(e, "rebase.cpp", "int main() {\n    if (y) {\n    }\n}\n", 1, 12);
  e.paste_text_for_test("        one();\n        two();\n");
  REQUIRE(e.buffer_for_test().line(1) == "    if (y) {");
  REQUIRE(e.buffer_for_test().line(2) == "      one();");
  REQUIRE(e.buffer_for_test().line(3) == "      two();");

  // Inserted at the content start of an indented line, the whitespace already
  // before the caret counts toward the indent rather than being repeated, and
  // the line's own text follows the block at the same indent.
  open_with(e, "content_start.cpp", "    x = 1;\n}\n", 0, 4);
  e.paste_text_for_test("foo();\nbar();\n");
  REQUIRE(e.buffer_for_test().line(0) == "    foo();");
  REQUIRE(e.buffer_for_test().line(1) == "    bar();");
  REQUIRE(e.buffer_for_test().line(2) == "    x = 1;");
}
