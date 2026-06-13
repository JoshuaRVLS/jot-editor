#include "test_framework.h"
#include "ui/text.h"

TEST(UITextCountsAsciiAndUtf8Cells) {
  ASSERT_EQ(ui_cell_count("abc"), 3);
  ASSERT_EQ(ui_cell_count("a\xe2\x97\x8f" "b"), 3);
  ASSERT_EQ(ui_cell_count("\xf0\x9f\x94\xa5"), 1);
}

TEST(UITextTakeCellsPreservesValidCodepoints) {
  std::string text = "ab\xe2\x97\x8f" "cd";
  ASSERT_EQ(ui_take_cells(text, 0), "");
  ASSERT_EQ(ui_take_cells(text, 2), "ab");
  ASSERT_EQ(ui_take_cells(text, 3), "ab\xe2\x97\x8f");
  ASSERT_EQ(ui_take_cells(text, 10), text);
}

TEST(UITextTruncateRight) {
  ASSERT_EQ(ui_truncate_cells("abcdef", -1), "");
  ASSERT_EQ(ui_truncate_cells("abcdef", 0), "");
  ASSERT_EQ(ui_truncate_cells("abcdef", 2), "ab");
  ASSERT_EQ(ui_truncate_cells("abcdef", 4), "ab..");
  ASSERT_EQ(ui_truncate_cells("abc", 4), "abc");
}

TEST(UITextTruncateLeft) {
  ASSERT_EQ(ui_truncate_left_cells("/a/b/c/d", 0), "");
  ASSERT_EQ(ui_truncate_left_cells("/a/b/c/d", 2), "/a");
  ASSERT_EQ(ui_truncate_left_cells("/a/b/c/d", 5), "..c/d");
  ASSERT_EQ(ui_truncate_left_cells("abc", 5), "abc");
}

TEST(UITextInvalidUtf8Fallback) {
  std::string invalid;
  invalid.push_back((char)0xE2);
  invalid.push_back('x');
  ASSERT_EQ(ui_cell_count(invalid), 2);
  ASSERT_EQ(ui_take_cells(invalid, 1), "?");
  ASSERT_EQ(ui_sanitized_cell_text(invalid), "?");
  ASSERT_EQ(ui_sanitized_cell_text(""), " ");
}

TEST(UITextOneLineNormalizesWhitespace) {
  ASSERT_EQ(ui_one_line(" alpha\t beta\n\n gamma  "), "alpha beta gamma");
  ASSERT_EQ(ui_one_line("\n\t"), "");
}
