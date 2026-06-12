#include "folding.h"
#include "test_framework.h"

TEST(TestFoldingDetectsCppBlock) {
  std::vector<std::string> lines = {
      "int main() {",
      "  if (ok) {",
      "    return 1;",
      "  }",
      "}",
  };
  auto ranges = Folding::detect_ranges(lines, ".cpp");
  ASSERT_TRUE(!ranges.empty());
  ASSERT_EQ(ranges[0].start_line, 0);
  ASSERT_EQ(ranges[0].end_line, 4);
}

TEST(TestFoldingDetectsPythonIndentBlock) {
  std::vector<std::string> lines = {
      "def f():",
      "    x = 1",
      "    return x",
      "print(f())",
  };
  auto ranges = Folding::detect_ranges(lines, ".py");
  ASSERT_EQ((int)ranges.size(), 1);
  ASSERT_EQ(ranges[0].start_line, 0);
  ASSERT_EQ(ranges[0].end_line, 2);
}

TEST(TestFoldingVisibleLineMapping) {
  std::vector<FoldRange> ranges = {{0, 4, true}, {1, 3, true}};
  ASSERT_TRUE(!Folding::is_line_hidden(ranges, 0));
  ASSERT_TRUE(Folding::is_line_hidden(ranges, 2));
  ASSERT_EQ(Folding::next_visible_line(ranges, 0, 6), 5);
  ASSERT_EQ(Folding::buffer_line_for_visible_offset(ranges, 0, 1, 6), 5);
  ASSERT_EQ(Folding::visible_line_count(ranges, 6), 2);
}

TEST(TestFoldingEncodeDecodeCollapsedRanges) {
  std::vector<FoldRange> ranges = {{0, 4, true}, {6, 8, false}, {10, 12, true}};
  std::string encoded = Folding::encode_collapsed_ranges(ranges);
  ASSERT_EQ(encoded, "0-4,10-12");

  auto decoded = Folding::decode_collapsed_ranges(encoded);
  ASSERT_EQ((int)decoded.size(), 2);
  ASSERT_EQ(decoded[0].start_line, 0);
  ASSERT_EQ(decoded[0].end_line, 4);
  ASSERT_TRUE(decoded[0].collapsed);
  ASSERT_EQ(decoded[1].start_line, 10);
  ASSERT_EQ(decoded[1].end_line, 12);
}

TEST(TestFoldingDecodeIgnoresMalformedRanges) {
  auto decoded = Folding::decode_collapsed_ranges("bad,4-x,7-6,2-5");
  ASSERT_EQ((int)decoded.size(), 1);
  ASSERT_EQ(decoded[0].start_line, 2);
  ASSERT_EQ(decoded[0].end_line, 5);
}

TEST(TestFoldingApplyCollapsedRangesRequiresExactMatch) {
  std::vector<FoldRange> ranges = {{0, 4, false}, {5, 9, false}};
  std::vector<FoldRange> collapsed = {{0, 4, true}, {7, 9, true}};
  Folding::apply_collapsed_ranges(ranges, collapsed);
  ASSERT_TRUE(ranges[0].collapsed);
  ASSERT_TRUE(!ranges[1].collapsed);
}
