#include "folding.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Folding Detects C++ Block", "[jot]") {
  std::vector<std::string> lines = {
      "int main() {",
      "  if (ok) {",
      "    return 1;",
      "  }",
      "}",
  };
  auto ranges = Folding::detect_ranges(lines, ".cpp");
  REQUIRE_FALSE(ranges.empty());
  REQUIRE(ranges[0].start_line == 0);
  REQUIRE(ranges[0].end_line == 4);
}

TEST_CASE("Folding Detects Python Indent Block", "[jot]") {
  std::vector<std::string> lines = {
      "def f():",
      "    x = 1",
      "    return x",
      "print(f())",
  };
  auto ranges = Folding::detect_ranges(lines, ".py");
  REQUIRE((int)ranges.size() == 1);
  REQUIRE(ranges[0].start_line == 0);
  REQUIRE(ranges[0].end_line == 2);
}

TEST_CASE("Folding Visible Line Mapping", "[jot]") {
  std::vector<FoldRange> ranges = {{0, 4, true}, {1, 3, true}};
  REQUIRE_FALSE(Folding::is_line_hidden(ranges, 0));
  REQUIRE(Folding::is_line_hidden(ranges, 2));
  REQUIRE(Folding::next_visible_line(ranges, 0, 6) == 5);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 1, 6) == 5);
  REQUIRE(Folding::visible_line_count(ranges, 6) == 2);
}

TEST_CASE("Folding Visible Line Mapping Past End Returns Sentinel", "[jot]") {
  std::vector<FoldRange> ranges;
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 0, 3) == 0);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 2, 3) == 2);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 3, 3) == -1);
}

TEST_CASE("Folding Visible Line Mapping Past Folded End Returns Sentinel", "[jot]") {
  std::vector<FoldRange> ranges = {{0, 4, true}, {1, 3, true}};
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 0, 6) == 0);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 1, 6) == 5);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 0, 2, 6) == -1);
}

TEST_CASE("Folding Visible Line Mapping Hidden Start Finds Next Visible Line", "[jot]") {
  std::vector<FoldRange> ranges = {{0, 2, true}};
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 1, 0, 4) == 3);
  REQUIRE(Folding::buffer_line_for_visible_offset(ranges, 1, 1, 4) == -1);
}

TEST_CASE("Folding Visible Row For Line", "[jot]") {
  std::vector<FoldRange> ranges = {{2, 4, true}};
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 0, 6, 8) == 0);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 2, 6, 8) == 2);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 5, 6, 8) == 3);
  REQUIRE(Folding::visible_row_for_line(ranges, 0, 3, 6, 8) == -1);
}

TEST_CASE("Folding Buffer Line For Visible Index Skips Hidden Lines", "[jot]") {
  std::vector<FoldRange> ranges = {{1, 3, true}, {6, 7, true}};
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 0, 9) == 0);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 1, 9) == 1);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 2, 9) == 4);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 4, 9) == 6);
  REQUIRE(Folding::buffer_line_for_visible_index(ranges, 5, 9) == 8);
}

TEST_CASE("Folding Encode Decode Collapsed Ranges", "[jot]") {
  std::vector<FoldRange> ranges = {{0, 4, true}, {6, 8, false}, {10, 12, true}};
  std::string encoded = Folding::encode_collapsed_ranges(ranges);
  REQUIRE(encoded == "0-4,10-12");

  auto decoded = Folding::decode_collapsed_ranges(encoded);
  REQUIRE((int)decoded.size() == 2);
  REQUIRE(decoded[0].start_line == 0);
  REQUIRE(decoded[0].end_line == 4);
  REQUIRE(decoded[0].collapsed);
  REQUIRE(decoded[1].start_line == 10);
  REQUIRE(decoded[1].end_line == 12);
}

TEST_CASE("Folding Decode Ignores Malformed Ranges", "[jot]") {
  auto decoded = Folding::decode_collapsed_ranges("bad,4-x,7-6,2-5");
  REQUIRE((int)decoded.size() == 1);
  REQUIRE(decoded[0].start_line == 2);
  REQUIRE(decoded[0].end_line == 5);
}

TEST_CASE("Folding Apply Collapsed Ranges Requires Exact Match", "[jot]") {
  std::vector<FoldRange> ranges = {{0, 4, false}, {5, 9, false}};
  std::vector<FoldRange> collapsed = {{0, 4, true}, {7, 9, true}};
  Folding::apply_collapsed_ranges(ranges, collapsed);
  REQUIRE(ranges[0].collapsed);
  REQUIRE_FALSE(ranges[1].collapsed);
}
