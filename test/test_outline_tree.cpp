// The outline's tree: extract_outline groups a C++ file's symbols under their
// scope (namespace > class > members; fields and methods under the class, the
// anonymous namespace spelled out as its own group), with the flat order
// depth-first and every consumer (selection, folding, jumps) reading the same
// visible-row order. Python nests by the extractor's name column; the winbar's
// flat extractor keeps its old shape for the languages without a tree rule.
#include "syntax/index.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
  const SymbolMatch *find(const std::vector<SymbolMatch> &tree, const std::string &name)
  {
    for (const SymbolMatch &s : tree)
    {
      if (s.name == name)
      {
        return &s;
      }
    }
    return nullptr;
  }
} // namespace

TEST_CASE("Outline tree: C++ members group under their class, fields carry their type", "[jot]")
{
  const std::vector<std::string> lines = {
      "namespace app {",
      "struct Widget {",
      "  int height;",
      "  bool visible;",
      "  int area() const;",
      "  void draw() {}",
      "};",
      "}",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "widget.hpp");

  // The chain app::Widget exists as a container, and the fields/methods hang
  // under it, one level deeper than the class, which is one under the namespace.
  const SymbolMatch *ns = find(tree, "app");
  REQUIRE(ns != nullptr);
  REQUIRE(ns->kind == "namespace");
  REQUIRE(ns->depth == 0);
  REQUIRE(ns->has_children());

  const SymbolMatch *klass = find(tree, "Widget");
  REQUIRE(klass != nullptr);
  REQUIRE(klass->kind == "struct");
  REQUIRE(klass->depth == 1);
  REQUIRE(klass->parent == (int)(ns - tree.data()));
  REQUIRE(klass->has_children());

  const SymbolMatch *height = find(tree, "height");
  REQUIRE(height != nullptr);
  REQUIRE(height->kind == "field");
  REQUIRE(height->detail == "int");
  REQUIRE(height->depth == 2);
  REQUIRE(height->parent == (int)(klass - tree.data()));

  const SymbolMatch *area = find(tree, "area");
  REQUIRE(area != nullptr);
  REQUIRE(area->kind == "method");
  REQUIRE(area->detail == "int");
  REQUIRE(area->depth == 2);

  const SymbolMatch *draw = find(tree, "draw");
  REQUIRE(draw != nullptr);
  REQUIRE(draw->kind == "method");
  // The definition's own return type is what the detail shows.
  REQUIRE(draw->detail == "void");
}

TEST_CASE("Outline tree: access labels do not swallow the fields under them", "[jot]")
{
  const std::vector<std::string> lines = {
      "class Widget {",
      "public:",
      "  int height;",
      "  static int count;",
      "  void draw();",
      "private:",
      "  int bits : 4;",
      "};",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "widget.hpp");

  // The label declares nothing, so it is not a row and the field after it still
  // is: `public` used to glue itself onto `int height` and cost the field.
  REQUIRE(find(tree, "public") == nullptr);
  const SymbolMatch *klass = find(tree, "Widget");
  REQUIRE(klass != nullptr);
  REQUIRE(klass->has_children());

  const SymbolMatch *height = find(tree, "height");
  REQUIRE(height != nullptr);
  REQUIRE(height->kind == "field");
  REQUIRE(height->detail == "int");
  REQUIRE(height->parent == (int)(klass - tree.data()));

  const SymbolMatch *bits = find(tree, "bits");
  REQUIRE(bits != nullptr);
  REQUIRE(bits->kind == "field");
  REQUIRE(bits->detail == "int");
  REQUIRE(bits->parent == (int)(klass - tree.data()));

  // A static member is a declaration, not a per-object field: no field row.
  const SymbolMatch *statik = find(tree, "count");
  REQUIRE(statik == nullptr);

  const SymbolMatch *draw = find(tree, "draw");
  REQUIRE(draw != nullptr);
  REQUIRE(draw->kind == "method");
  REQUIRE(draw->parent == (int)(klass - tree.data()));
}

TEST_CASE("Outline tree: an anonymous namespace is its own group", "[jot]")
{
  const std::vector<std::string> lines = {
      "namespace {",
      "bool is_word_char(char c);",
      "int replace_in_line(std::string &s);",
      "}",
      "void Editor::replace_all_text() {}",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "replace.cpp");

  const SymbolMatch *anon = find(tree, "(anonymous namespace)");
  REQUIRE(anon != nullptr);
  REQUIRE(anon->depth == 0);
  REQUIRE(anon->has_children());

  const SymbolMatch *word = find(tree, "is_word_char");
  REQUIRE(word != nullptr);
  REQUIRE(word->detail == "bool");
  REQUIRE(word->depth == 1);
  REQUIRE(word->parent == (int)(anon - tree.data()));

  // The out-of-class definition keeps the top level (its class is not in this
  // file to be a group) and shows the qualified name's last component.
  const SymbolMatch *all = find(tree, "replace_all_text");
  REQUIRE(all != nullptr);
  REQUIRE(all->depth == 0);
}

TEST_CASE("Outline tree: members are in file order under their scope", "[jot]")
{
  const std::vector<std::string> lines = {
      "struct Range {",
      "  bool found;",
      "  int open_line;",
      "  int close_col;",
      "  bool is_supported_open(char c);",
      "};",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "bracket.hpp");
  REQUIRE(tree.size() >= 5);
  // Depth-first flat order: the struct, then its members in declaration order.
  REQUIRE(tree[0].name == "Range");
  REQUIRE(tree[1].name == "found");
  REQUIRE(tree[2].name == "open_line");
  REQUIRE(tree[3].name == "close_col");
  REQUIRE(tree[4].name == "is_supported_open");
}

TEST_CASE("Outline tree: the visible walk honours the expanded marks", "[jot]")
{
  const std::vector<std::string> lines = {
      "namespace ns {",
      "struct Box {",
      "  int w;",
      "  int area() const;",
      "};",
      "}",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "box.hpp");
  REQUIRE(tree.size() == 4); // ns, Box, w, area

  // Everything open: all four rows in depth-first order.
  std::vector<char> expanded(tree.size(), 1);
  std::vector<int> visible = SymbolIndex::outline_visible_rows(tree, expanded);
  REQUIRE(visible.size() == 4);
  REQUIRE(visible[0] == 0);
  REQUIRE(visible[1] == 1);

  // The namespace folded: only its own row shows, and the members under Box go
  // with it (a folded container hides its whole subtree).
  expanded[0] = 0;
  visible = SymbolIndex::outline_visible_rows(tree, expanded);
  REQUIRE(visible.size() == 1);
  REQUIRE(visible[0] == 0);

  // Namespace open, class folded: two rows.
  expanded[0] = 1;
  expanded[1] = 0;
  visible = SymbolIndex::outline_visible_rows(tree, expanded);
  REQUIRE(visible.size() == 2);
  REQUIRE(visible[1] == 1);
}

TEST_CASE("Outline tree: enums and typedefs are rows, enumerators under their enum", "[jot]")
{
  const std::vector<std::string> lines = {
      "enum class Color { Red, Green = 3 };",
      "enum { First, Second };",
      "typedef unsigned long long U64;",
      "using Alias = Range;",
      "using ns::Thing;",
      "struct S {",
      "  enum Inner { One, Two, };",
      "  typedef int Count;",
      "  int member;",
      "};",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "kinds.hpp");

  // A scoped enum is a container with its enumerators under it, and the value an
  // enumerator pins is its row's detail.
  const SymbolMatch *color = find(tree, "Color");
  REQUIRE(color != nullptr);
  REQUIRE(color->kind == "enum");
  REQUIRE(color->has_children());
  const SymbolMatch *red = find(tree, "Red");
  REQUIRE(red != nullptr);
  REQUIRE(red->kind == "enum_member");
  REQUIRE(red->parent == (int)(color - tree.data()));
  const SymbolMatch *green = find(tree, "Green");
  REQUIRE(green != nullptr);
  REQUIRE(green->detail == "3");

  // An anonymous enum still gets a row, and its members hang under that row
  // rather than under a node of their own.
  const SymbolMatch *anon = find(tree, "(anonymous enum)");
  REQUIRE(anon != nullptr);
  REQUIRE(anon->kind == "enum");
  REQUIRE(anon->has_children());
  const SymbolMatch *first = find(tree, "First");
  REQUIRE(first != nullptr);
  REQUIRE(first->parent == (int)(anon - tree.data()));

  // The outline is the file's declarations: an enum or a typedef in it is a row
  // (content used to go missing here).
  const SymbolMatch *u64 = find(tree, "U64");
  REQUIRE(u64 != nullptr);
  REQUIRE(u64->kind == "typedef");
  REQUIRE(u64->detail == "unsigned long long");
  const SymbolMatch *alias = find(tree, "Alias");
  REQUIRE(alias != nullptr);
  REQUIRE(alias->kind == "type_alias");
  REQUIRE(alias->detail == "Range");
  REQUIRE(find(tree, "Thing") != nullptr);

  // ...including inside a class body, where an enum's trailing comma must not
  // cost its last enumerator or the declarations that follow.
  const SymbolMatch *klass = find(tree, "S");
  REQUIRE(klass != nullptr);
  const SymbolMatch *inner = find(tree, "Inner");
  REQUIRE(inner != nullptr);
  REQUIRE(inner->kind == "enum");
  REQUIRE(inner->parent == (int)(klass - tree.data()));
  const SymbolMatch *two = find(tree, "Two");
  REQUIRE(two != nullptr);
  REQUIRE(two->parent == (int)(inner - tree.data()));
  const SymbolMatch *count = find(tree, "Count");
  REQUIRE(count != nullptr);
  REQUIRE(count->kind == "typedef");
  REQUIRE(count->parent == (int)(klass - tree.data()));
  const SymbolMatch *member = find(tree, "member");
  REQUIRE(member != nullptr);
  REQUIRE(member->kind == "field");
  REQUIRE(member->parent == (int)(klass - tree.data()));
}

TEST_CASE("Outline tree: Python methods nest under their class", "[jot]")
{
  const std::vector<std::string> lines = {
      "class Widget:",
      "    def area(self):",
      "        return 0",
      "    def draw(self):",
      "        pass",
  };
  const std::vector<SymbolMatch> tree = SymbolIndex::extract_outline(lines, "widget.py");
  const SymbolMatch *klass = find(tree, "Widget");
  REQUIRE(klass != nullptr);
  REQUIRE(klass->depth == 0);
  REQUIRE(klass->has_children());
  const SymbolMatch *area = find(tree, "area");
  REQUIRE(area != nullptr);
  REQUIRE(area->depth == 1);
  REQUIRE(area->parent == (int)(klass - tree.data()));
}
