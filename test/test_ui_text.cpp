#include "ui/text.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("UI Text Counts Ascii And UTF-8 Cells", "[jot]")
{
  REQUIRE(ui_cell_count("abc") == 3);
  REQUIRE(ui_cell_count("a\xe2\x97\x8f"
                        "b")
          == 3);
  REQUIRE(ui_cell_count("\xe8\xa1\xa8") == 2);
  REQUIRE(ui_cell_count("e\xcc\x81") == 1);
}

TEST_CASE("UI Text Take Cells Preserves Valid Codepoints", "[jot]")
{
  std::string text = "ab\xe2\x97\x8f"
                     "cd";
  REQUIRE(ui_take_cells(text, 0) == "");
  REQUIRE(ui_take_cells(text, 2) == "ab");
  REQUIRE(ui_take_cells(text, 3) == "ab\xe2\x97\x8f");
  REQUIRE(ui_take_cells(text, 10) == text);

  std::string wide = "a\xe8\xa1\xa8"
                     "b";
  REQUIRE(ui_take_cells(wide, 1) == "a");
  REQUIRE(ui_take_cells(wide, 2) == "a");
  REQUIRE(ui_take_cells(wide, 3) == "a\xe8\xa1\xa8");
  REQUIRE(ui_take_cells(wide, 4) == wide);
}

TEST_CASE("UI Text Truncate Right", "[jot]")
{
  REQUIRE(ui_truncate_cells("abcdef", -1) == "");
  REQUIRE(ui_truncate_cells("abcdef", 0) == "");
  REQUIRE(ui_truncate_cells("abcdef", 2) == "ab");
  REQUIRE(ui_truncate_cells("abcdef", 4) == "ab..");
  REQUIRE(ui_truncate_cells("abc", 4) == "abc");
}

TEST_CASE("UI Text Truncate Right With An Ellipsis", "[jot]")
{
  // Exact fit is untouched; a cut spends one cell on the ellipsis instead of
  // the two dots' two, so one more character of the name survives.
  REQUIRE(ui_truncate_cells_ellipsis("abcdef", -1) == "");
  REQUIRE(ui_truncate_cells_ellipsis("abcdef", 0) == "");
  REQUIRE(ui_truncate_cells_ellipsis("abcdef", 1) == "\u2026");
  REQUIRE(ui_truncate_cells_ellipsis("abcdef", 4) == "abc\u2026");
  REQUIRE(ui_truncate_cells_ellipsis("abcdef", 6) == "abcdef");
  REQUIRE(ui_truncate_cells_ellipsis("abc", 4) == "abc");

  // A wide character that would straddle the cut is dropped whole rather than
  // split or overrun, so the result never exceeds its budget.
  std::string wide = "a\xe8\xa1\xa8"
                     "b";
  REQUIRE(ui_truncate_cells_ellipsis(wide, 1) == "\u2026");
  REQUIRE(ui_truncate_cells_ellipsis(wide, 2) == "a\u2026");
  REQUIRE(ui_cell_count(ui_truncate_cells_ellipsis(wide, 3)) <= 3);
  REQUIRE(ui_cell_count(ui_truncate_cells_ellipsis(wide, 4)) == 4);
}

TEST_CASE("UI Text Truncate Left", "[jot]")
{
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 0) == "");
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 2) == "/a");
  REQUIRE(ui_truncate_left_cells("/a/b/c/d", 5) == "..c/d");
  REQUIRE(ui_truncate_left_cells("abc", 5) == "abc");
}

TEST_CASE("UI Text Invalid UTF-8 Fallback", "[jot]")
{
  std::string invalid;
  invalid.push_back((char)0xE2);
  invalid.push_back('x');
  REQUIRE(ui_cell_count(invalid) == 2);
  REQUIRE(ui_take_cells(invalid, 1) == "?");
  REQUIRE(ui_sanitized_cell_text(invalid) == "?");
  REQUIRE(ui_sanitized_cell_text("") == " ");
}

// The range form exists so the renderer can sanitize one grapheme of a string
// without materialising the substring first (draw_text runs it once per cell of
// every string it paints). It has to answer exactly what the substring form
// would, including for ranges that do not line up with a codepoint.
TEST_CASE("UI Text Sanitizes A Range Like Its Substring", "[jot]")
{
  const std::string mixed = "a\x07b\xc3\xa9c";
  for (int begin = 0; begin <= (int)mixed.size(); begin++)
  {
    for (int end = begin; end <= (int)mixed.size(); end++)
    {
      std::string out;
      ui_sanitize_cell_range(mixed, begin, end, out);
      REQUIRE(out == ui_sanitized_cell_text(mixed.substr(begin, end - begin)));
    }
  }

  // Printable ASCII is the path that skips the decoder, and it must return the
  // bytes themselves rather than a placeholder.
  std::string ascii;
  ui_sanitize_cell_range("hello", 1, 4, ascii);
  REQUIRE(ascii == "ell");

  // An empty (or inverted) range is the blank cell.
  std::string empty;
  ui_sanitize_cell_range("hello", 2, 2, empty);
  REQUIRE(empty == " ");
  ui_sanitize_cell_range("hello", 4, 1, empty);
  REQUIRE(empty == " ");

  // A control byte inside the range is still rejected; so is a construct that
  // only decodes as the whole string, not as the requested slice. (`\a` rather
  // than `\x07b`: a hex escape swallows the following hex digits, so `\x07b`
  // is the single byte 0x7b.)
  std::string control;
  ui_sanitize_cell_range("a\ab", 1, 2, control);
  REQUIRE(control == "?");
  std::string split;
  ui_sanitize_cell_range("\xc3\xa9", 0, 1, split);
  REQUIRE(split == "?");

  // Out-of-range bounds clamp to the string instead of reading past it.
  std::string overshoot;
  ui_sanitize_cell_range("ab", -5, 99, overshoot);
  REQUIRE(overshoot == "ab");
}

TEST_CASE("UI Text Grapheme Boundaries And Normalization", "[jot]")
{
  std::string decomposed = "e\xcc\x81";
  REQUIRE(ui_next_grapheme_boundary(decomposed, 0) == (int)decomposed.size());
  REQUIRE(ui_prev_grapheme_boundary(decomposed, (int)decomposed.size()) == 0);
  REQUIRE(ui_normalize_nfc(decomposed) == "\xc3\xa9");
  REQUIRE(ui_normalize_nfd("\xc3\xa9") == decomposed);
}

TEST_CASE("UI Text One Line Normalizes Whitespace", "[jot]")
{
  REQUIRE(ui_one_line(" alpha\t beta\n\n gamma  ") == "alpha beta gamma");
  REQUIRE(ui_one_line("\n\t") == "");
}
