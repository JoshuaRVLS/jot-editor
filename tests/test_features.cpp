#include "jot/editor_features.hpp"
#include "html.h"
#include "test_framework.h"

TEST(TestIndentLevel) {
  ASSERT_EQ(EditorFeatures::get_indent_level("    code"), 4);
  ASSERT_EQ(EditorFeatures::get_indent_level("\tcode"),
            4); // Assuming 4 space tab
  ASSERT_EQ(EditorFeatures::get_indent_level("  code"), 2);
  ASSERT_EQ(EditorFeatures::get_indent_level("code"), 0);
}

TEST(TestTrimRight) {
  ASSERT_EQ(EditorFeatures::trim_right("abc  "), "abc");
  ASSERT_EQ(EditorFeatures::trim_right("abc\n"), "abc\n");
  ASSERT_EQ(EditorFeatures::trim_right("  "), "");
}

TEST(TestAutoIndent) {
  ASSERT_TRUE(EditorFeatures::should_auto_indent("if (x) {"));
  ASSERT_TRUE(EditorFeatures::should_auto_indent("def foo():"));
  ASSERT_TRUE(!EditorFeatures::should_auto_indent("x = 1"));
  ASSERT_TRUE(!EditorFeatures::should_auto_indent("defaultValue = 1"));
}

TEST(TestDedent) {
  ASSERT_TRUE(EditorFeatures::should_dedent("}"));
  ASSERT_TRUE(EditorFeatures::should_dedent("  }"));
  ASSERT_TRUE(!EditorFeatures::should_dedent("{"));
  ASSERT_TRUE(!EditorFeatures::should_dedent("defaultValue = 1"));
}

TEST(TestMatchingBracket) {
  std::vector<std::string> doc = {"if (a) {", "  print(b)", "}"};

  // Test opening bracket at line 0, char 7 '{' -> match line 2, char 0 '}'
  int match = EditorFeatures::find_matching_bracket(doc, 0, 7, '{', '}');
  int expected = 2 * 10000 + 0;
  ASSERT_EQ(match, expected);

  // Test backward matching
  // Line 2, char 0 '}' -> Line 0, char 7 '{'
  match = EditorFeatures::find_matching_bracket(doc, 2, 0, '{', '}');
  expected = 0 * 10000 + 7;
  ASSERT_EQ(match, expected);
}

TEST(TestMarkupExtensions) {
  ASSERT_TRUE(HtmlFeatures::is_html_extension("index.html"));
  ASSERT_TRUE(HtmlFeatures::is_html_extension("index.htm"));
  ASSERT_TRUE(HtmlFeatures::is_jsx_extension("view.jsx"));
  ASSERT_TRUE(HtmlFeatures::is_jsx_extension("view.tsx"));
  ASSERT_TRUE(HtmlFeatures::is_markup_tag_extension("view.tsx"));
  ASSERT_TRUE(!HtmlFeatures::is_markup_tag_extension("view.ts"));
}

TEST(TestMarkupAutoCloseTag) {
  std::string closing;
  ASSERT_TRUE(HtmlFeatures::should_insert_closing_tag("<div>", 5, closing));
  ASSERT_EQ(closing, "</div>");
  ASSERT_TRUE(HtmlFeatures::should_insert_closing_tag("return <Component>", 18,
                                                      closing));
  ASSERT_EQ(closing, "</Component>");
  ASSERT_TRUE(!HtmlFeatures::should_insert_closing_tag("<img>", 5, closing));
  ASSERT_TRUE(!HtmlFeatures::should_insert_closing_tag("</div>", 6, closing));
  ASSERT_TRUE(!HtmlFeatures::should_insert_closing_tag("foo<T>", 6, closing));
  ASSERT_TRUE(!HtmlFeatures::should_insert_closing_tag("a < b>", 5, closing));
}

TEST(TestMarkupBetweenTags) {
  std::string tag;
  ASSERT_TRUE(
      HtmlFeatures::is_between_matching_tags("<div>", "</div>", tag));
  ASSERT_EQ(tag, "div");
  ASSERT_TRUE(HtmlFeatures::is_between_matching_tags(
      "return <Component>", "</Component>", tag));
  ASSERT_EQ(tag, "Component");
  ASSERT_TRUE(!HtmlFeatures::is_between_matching_tags("<img>", "", tag));
  ASSERT_TRUE(!HtmlFeatures::is_between_matching_tags("foo<T>", "</T>", tag));
}
