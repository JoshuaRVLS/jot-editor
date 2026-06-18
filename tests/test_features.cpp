#include "jot/editor_features.hpp"
#include "html.h"
#include "quote_text_object.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Indent Level", "[jot]") {
  REQUIRE(EditorFeatures::get_indent_level("    code") == 4);
  REQUIRE(EditorFeatures::get_indent_level("\tcode") == 4); // Assuming 4 space tab
  REQUIRE(EditorFeatures::get_indent_level("  code") == 2);
  REQUIRE(EditorFeatures::get_indent_level("code") == 0);
}

TEST_CASE("Trim Right", "[jot]") {
  REQUIRE(EditorFeatures::trim_right("abc  ") == "abc");
  REQUIRE(EditorFeatures::trim_right("abc\n") == "abc\n");
  REQUIRE(EditorFeatures::trim_right("  ") == "");
}

TEST_CASE("Auto Indent", "[jot]") {
  REQUIRE(EditorFeatures::should_auto_indent("if (x) {"));
  REQUIRE(EditorFeatures::should_auto_indent("def foo():"));
  REQUIRE_FALSE(EditorFeatures::should_auto_indent("x = 1"));
  REQUIRE_FALSE(EditorFeatures::should_auto_indent("defaultValue = 1"));
}

TEST_CASE("Python Auto Indent", "[jot]") {
  REQUIRE(EditorFeatures::should_python_auto_indent("if x:"));
  REQUIRE(EditorFeatures::should_python_auto_indent("def f():"));
  REQUIRE(EditorFeatures::should_python_auto_indent("async def f():"));
  REQUIRE(EditorFeatures::should_python_auto_indent("with open(path):"));
  REQUIRE(EditorFeatures::should_python_auto_indent("match value:"));
  REQUIRE(EditorFeatures::should_python_auto_indent("case 1:"));
  REQUIRE(EditorFeatures::should_python_auto_indent("if x:  # comment"));
  REQUIRE_FALSE(EditorFeatures::should_python_auto_indent("x = 1"));
  REQUIRE_FALSE(EditorFeatures::should_python_auto_indent("defaultValue = 1"));
  REQUIRE_FALSE(EditorFeatures::should_python_auto_indent("elsewhere: value"));
  REQUIRE_FALSE(EditorFeatures::should_python_auto_indent("text = '#:'"));
}

TEST_CASE("Dedent", "[jot]") {
  REQUIRE(EditorFeatures::should_dedent("}"));
  REQUIRE(EditorFeatures::should_dedent("  }"));
  REQUIRE_FALSE(EditorFeatures::should_dedent("{"));
  REQUIRE_FALSE(EditorFeatures::should_dedent("defaultValue = 1"));
}

TEST_CASE("Python Dedent", "[jot]") {
  REQUIRE(EditorFeatures::should_python_dedent("elif x:"));
  REQUIRE(EditorFeatures::should_python_dedent("else:"));
  REQUIRE(EditorFeatures::should_python_dedent("except ValueError:"));
  REQUIRE(EditorFeatures::should_python_dedent("finally:"));
  REQUIRE(EditorFeatures::should_python_dedent("case _:"));
  REQUIRE(EditorFeatures::should_python_dedent("else:  # comment"));
  REQUIRE_FALSE(EditorFeatures::should_python_dedent("else"));
  REQUIRE_FALSE(EditorFeatures::should_python_dedent("elsewhere: value"));
  REQUIRE_FALSE(EditorFeatures::should_python_dedent("defaultValue = 1"));
  REQUIRE_FALSE(EditorFeatures::should_python_dedent("text = '# else:'"));
}

TEST_CASE("Matching Bracket", "[jot]") {
  std::vector<std::string> doc = {"if (a) {", "  print(b)", "}"};

  // Test opening bracket at line 0, char 7 '{' -> match line 2, char 0 '}'
  int match = EditorFeatures::find_matching_bracket(doc, 0, 7, '{', '}');
  int expected = 2 * 10000 + 0;
  REQUIRE(match == expected);

  // Test backward matching
  // Line 2, char 0 '}' -> Line 0, char 7 '{'
  match = EditorFeatures::find_matching_bracket(doc, 2, 0, '{', '}');
  expected = 0 * 10000 + 7;
  REQUIRE(match == expected);
}

TEST_CASE("Markup Extensions", "[jot]") {
  REQUIRE(HtmlFeatures::is_html_extension("index.html"));
  REQUIRE(HtmlFeatures::is_html_extension("index.htm"));
  REQUIRE(HtmlFeatures::is_jsx_extension("view.jsx"));
  REQUIRE(HtmlFeatures::is_jsx_extension("view.tsx"));
  REQUIRE(HtmlFeatures::is_markup_tag_extension("view.tsx"));
  REQUIRE_FALSE(HtmlFeatures::is_markup_tag_extension("view.ts"));
}

TEST_CASE("Markup Auto Close Tag", "[jot]") {
  std::string closing;
  REQUIRE(HtmlFeatures::should_insert_closing_tag("<div>", 5, closing));
  REQUIRE(closing == "</div>");
  REQUIRE(HtmlFeatures::should_insert_closing_tag("return <Component>", 18,
                                                      closing));
  REQUIRE(closing == "</Component>");
  REQUIRE_FALSE(HtmlFeatures::should_insert_closing_tag("<img>", 5, closing));
  REQUIRE_FALSE(HtmlFeatures::should_insert_closing_tag("</div>", 6, closing));
  REQUIRE_FALSE(HtmlFeatures::should_insert_closing_tag("foo<T>", 6, closing));
  REQUIRE_FALSE(HtmlFeatures::should_insert_closing_tag("a < b>", 5, closing));
}

TEST_CASE("Markup Between Tags", "[jot]") {
  std::string tag;
  REQUIRE(HtmlFeatures::is_between_matching_tags("<div>", "</div>", tag));
  REQUIRE(tag == "div");
  REQUIRE(HtmlFeatures::is_between_matching_tags(
      "return <Component>", "</Component>", tag));
  REQUIRE(tag == "Component");
  REQUIRE_FALSE(HtmlFeatures::is_between_matching_tags("<img>", "", tag));
  REQUIRE_FALSE(HtmlFeatures::is_between_matching_tags("foo<T>", "</T>", tag));
}

TEST_CASE("Quote Text Object Finds Containing Pair", "[jot]") {
  auto range = QuoteTextObject::find_inner_range("auto s = \"hello\";", 11, '"');
  REQUIRE(range.found);
  REQUIRE(range.open == 9);
  REQUIRE(range.close == 15);
  REQUIRE(range.inner_start == 10);
  REQUIRE(range.inner_end == 15);
}

TEST_CASE("Quote Text Object Supports Single And Backtick Quotes", "[jot]") {
  auto single = QuoteTextObject::find_inner_range("name = 'jot'", 9, '\'');
  REQUIRE(single.found);
  REQUIRE(single.inner_start == 8);
  REQUIRE(single.inner_end == 11);

  auto tick = QuoteTextObject::find_inner_range("const q = `abc`;", 12, '`');
  REQUIRE(tick.found);
  REQUIRE(tick.inner_start == 11);
  REQUIRE(tick.inner_end == 14);
}

TEST_CASE("Quote Text Object Ignores Escaped Quotes", "[jot]") {
  auto range =
      QuoteTextObject::find_inner_range("s = \"a \\\"quoted\\\" value\";", 8, '"');
  REQUIRE(range.found);
  REQUIRE(range.open == 4);
  REQUIRE(range.close == 23);
  REQUIRE(range.inner_start == 5);
  REQUIRE(range.inner_end == 23);
}

TEST_CASE("Quote Text Object Chooses Nearest Pair Outside Cursor", "[jot]") {
  auto range =
      QuoteTextObject::find_inner_range("\"left\" + \"right\"", 8, '"');
  REQUIRE(range.found);
  REQUIRE(range.open == 9);
  REQUIRE(range.close == 15);
}

TEST_CASE("Quote Text Object Rejects Missing Pair", "[jot]") {
  auto range = QuoteTextObject::find_inner_range("s = \"unterminated", 6, '"');
  REQUIRE_FALSE(range.found);
  range = QuoteTextObject::find_inner_range("s = \"ok\"", 5, ')');
  REQUIRE_FALSE(range.found);
}
