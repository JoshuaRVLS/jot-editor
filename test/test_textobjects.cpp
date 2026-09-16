// Tree-sitter textobjects: expand/shrink the selection to a syntax node, take
// the inside/around of a function, class or argument, and step between
// functions.
//
// Highlights were the only consumer of the syntax tree before this, so these
// cases also pin that the tree is reachable from editor code at all: they parse
// a real file with a real grammar and assert the selection lands on the node the
// cursor is inside. Machines without the cpp grammar installed skip (the tree is
// simply absent), which is how the other tree-sitter tests guard too.
#include "editor.h"
#include "features/textobjects.h"

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
      char cfgdir[] = "/tmp/jot_textobject_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  const char *kSource = "int other() {\n"
                        "  return 0;\n"
                        "}\n"
                        "\n"
                        "int add(int a, int b) {\n"
                        "  int sum = a + b;\n"
                        "  return sum;\n"
                        "}\n";

  // Loads the source and puts the cursor on line 5 (inside add()'s body).
  bool load_and_place(Editor &e, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_textobject_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << kSource;
    out.close();
    e.load_file(path);
    if (!e.syntax_tree_ready_for_test())
    {
      return false; // no grammar installed: the caller skips
    }
    e.reset_jumplist_for_test();
    e.scroll_cursor_to_for_test(line, col);
    return true;
  }
} // namespace

TEST_CASE("Expand walks out of the syntax nodes the cursor sits in", "[jot][textobject]")
{
  Editor &e = probe_editor();
  if (!load_and_place(e, 6, 6)) // inside "int sum = a + b;"
  {
    SUCCEED("cpp grammar not installed; textobjects skipped");
    return;
  }

  // Each press covers a strictly larger node: the expression, the statement, the
  // body, then the whole function.
  REQUIRE(e.expand_selection_for_test());
  const std::string first = e.host().core.selected_text();
  REQUIRE_FALSE(first.empty());

  bool reached_function = false;
  for (int i = 0; i < 8; i++)
  {
    if (!e.expand_selection_for_test())
    {
      break;
    }
    const std::string text = e.host().core.selected_text();
    REQUIRE(text.size() >= first.size());
    if (text.find("int add(int a, int b)") != std::string::npos)
    {
      reached_function = true;
      break;
    }
  }
  REQUIRE(reached_function);
}

TEST_CASE("Shrink returns to the largest node inside the selection", "[jot][textobject]")
{
  Editor &e = probe_editor();
  if (!load_and_place(e, 6, 6))
  {
    SUCCEED("cpp grammar not installed; textobjects skipped");
    return;
  }

  REQUIRE(e.select_textobject_for_test("function", false));
  INFO("around: [" << e.host().core.selected_text() << "]");
  REQUIRE(e.host().core.selected_text().find("int add(int a, int b)") != std::string::npos);

  // One shrink from the whole function lands inside it (the body), and the text
  // is a strict subset of what was selected before.
  const std::string whole = e.host().core.selected_text();
  REQUIRE(e.shrink_selection_for_test());
  const std::string smaller = e.host().core.selected_text();
  INFO("whole: [" << whole << "] smaller: [" << smaller << "]");
  REQUIRE_FALSE(smaller.empty());
  REQUIRE(smaller.size() < whole.size());
  REQUIRE(whole.find(smaller) != std::string::npos);
}

TEST_CASE("Inside a function selects the body, around selects the definition", "[jot][textobject]")
{
  Editor &e = probe_editor();
  if (!load_and_place(e, 6, 6))
  {
    SUCCEED("cpp grammar not installed; textobjects skipped");
    return;
  }

  // "around" is the whole definition.
  REQUIRE(e.select_textobject_for_test("function", false));
  const std::string around = e.host().core.selected_text();
  REQUIRE(around.find("int add") != std::string::npos);
  REQUIRE(around.find("return sum;") != std::string::npos);

  // "inside" is the body only: it no longer carries the signature.
  e.scroll_cursor_to_for_test(6, 6);
  REQUIRE(e.select_textobject_for_test("function", true));
  const std::string inner = e.host().core.selected_text();
  REQUIRE(inner.find("return sum;") != std::string::npos);
  REQUIRE(inner.find("int add") == std::string::npos);

  // An argument: "around" is the parameter declaration, "inside" its name.
  e.scroll_cursor_to_for_test(4, 8); // on "a" in the signature
  REQUIRE(e.select_textobject_for_test("argument", false));
  const std::string arg = e.host().core.selected_text();
  INFO("argument: [" << arg << "]");
  REQUIRE(arg.find("int a") != std::string::npos);
  e.scroll_cursor_to_for_test(4, 8);
  REQUIRE(e.select_textobject_for_test("argument", true));
  REQUIRE(e.host().core.selected_text() == "a");
}

TEST_CASE("Next and previous function step between definitions", "[jot][textobject]")
{
  Editor &e = probe_editor();
  if (!load_and_place(e, 6, 6))
  {
    SUCCEED("cpp grammar not installed; textobjects skipped");
    return;
  }

  // From inside add(), backwards first lands on add()'s own start (the way vim's
  // [f does), then on the function before it, then there is nowhere to go.
  REQUIRE(e.goto_function_for_test(-1));
  INFO("after first prev: line " << e.buffer_for_test().cursor.y << " col "
                                 << e.buffer_for_test().cursor.x);
  REQUIRE(e.buffer_for_test().cursor.y == 4);
  REQUIRE(e.buffer_for_test().cursor.x == 0);
  REQUIRE(e.goto_function_for_test(-1));
  REQUIRE(e.buffer_for_test().cursor.y == 0);
  REQUIRE_FALSE(e.goto_function_for_test(-1));

  // Forwards from the first function lands on the second, and the last one has
  // nothing after it.
  REQUIRE(e.goto_function_for_test(1));
  REQUIRE(e.buffer_for_test().cursor.y == 4);
  REQUIRE_FALSE(e.goto_function_for_test(1));
}

// The node-name table is the part that decides whether a textobject works at all
// for a language, and it needs no grammar loaded to check: the names come from
// the grammars, and a typo would make the command silently do nothing.
TEST_CASE("Textobject node names differ per language family", "[jot][textobject]")
{
  using jot_textobjects::names_for_extension;

  // Extension and language-id spellings are the same key.
  REQUIRE(names_for_extension(".cpp").is_function("function_definition"));
  REQUIRE(names_for_extension("cpp").is_function("function_definition"));
  REQUIRE(names_for_extension("C++").is_function("function_definition"));

  // The C family declares functions with function_definition, JavaScript with
  // function_declaration, Rust with function_item.
  REQUIRE(names_for_extension(".cpp").is_function("function_declarator"));
  REQUIRE(names_for_extension(".js").is_function("function_declaration"));
  REQUIRE(names_for_extension(".ts").is_function("method_definition"));
  REQUIRE(names_for_extension(".rs").is_function("function_item"));
  REQUIRE(names_for_extension(".go").is_function("method_declaration"));
  REQUIRE(names_for_extension(".py").is_class("class_definition"));
  REQUIRE(names_for_extension(".js").is_class("class_declaration"));
  REQUIRE(names_for_extension(".rs").is_class("struct_item"));

  // Arguments and comments, where the names differ most.
  REQUIRE(names_for_extension(".py").is_argument("parameters"));
  REQUIRE(names_for_extension(".js").is_argument("formal_parameters"));
  REQUIRE(names_for_extension(".ts").is_argument("required_parameter"));
  REQUIRE(names_for_extension(".rs").is_comment("line_comment"));
  REQUIRE(names_for_extension(".go").is_comment("comment"));

  // A language the table does not know still gets the C-like fallback rather than
  // nothing at all.
  const auto unknown = names_for_extension(".zzz");
  REQUIRE(unknown.is_function("function_definition"));
  REQUIRE(unknown.is_function("function_item"));
  REQUIRE_FALSE(unknown.empty());

  // Wrong names for the right language are the failure that hides: assert the
  // families do not leak into each other.
  REQUIRE_FALSE(names_for_extension(".rs").is_function("function_definition"));
  REQUIRE_FALSE(names_for_extension(".py").is_function("function_item"));
}
