#include "../src/features/editor_features.h"
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
