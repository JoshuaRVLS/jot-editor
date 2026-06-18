#include <catch2/catch_test_macros.hpp>
#include "ui/text.h"

TEST_CASE("UI Text Counts Ascii And UTF-8 Cells", "[jot]") {
  REQUIRE(ui_cell_count("abc") == 3);
  REQUIRE(ui_cell_count("a\xe2\x97\x8f" "b") == 3);
  REQUIRE(ui_cell_count("\xf0\x9f\x94\xa5") == 1);
}

TEST_CASE("UI Text Take Cells Preserves Valid Codepoints", "[jot]") {
  std::string text = "ab\xe2\x97\x8f" "cd";
  REQUIRE(ui_take_cells(text, 0) == "");
  REQUIRE(ui_take_cells(text, 2) == "ab");
  REQUIRE(ui_take_cells(text, 3) == "ab\xe2\x97\x8f");
  REQUIRE(ui_take_cells(text, 10) == text);
}

TEST_CASE("UI Text Truncate Right", "[jot]") {
  REQUIRE(ui_truncate_cells("abcdef", -1) == "");
  REQUIRE(ui_truncate_cells("abcdef", 0) == "");
  REQUIRE(ui_truncate_cells("abcdef", 2) == "ab");
  REQUIRE(ui_truncate_cells("abcdef", 4) == "ab..");
  REQUIRE(ui_truncate_cells("abc", 4) == "abc");
}

TEST_CASE("UI Text Truncate Left", "[jot]") {
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 0) == "");
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 2) == "/a");
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 5) == "..c/d");
  REQUIRE(ui_truncate_left_cells("abc", 5) == "abc");
}

TEST_CASE("UI Text Invalid UTF-8 Fallback", "[jot]") {
  std::string invalid;
  invalid.push_back((char)0xE2);
  invalid.push_back('x');
  REQUIRE(ui_cell_count(invalid) == 2);
  REQUIRE(ui_take_cells(invalid, 1) == "?");
  REQUIRE(ui_sanitized_cell_text(invalid) == "?");
  REQUIRE(ui_sanitized_cell_text("") == " ");
}

TEST_CASE("UI Text One Line Normalizes Whitespace", "[jot]") {
  REQUIRE(ui_one_line(" alpha\t beta\n\n gamma  ") == "alpha beta gamma");
  REQUIRE(ui_one_line("\n\t") == "");
}
