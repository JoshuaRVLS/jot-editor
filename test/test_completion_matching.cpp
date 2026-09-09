// Completion popup matching: byte offsets into the label of the characters
// the typed query consumed, mirroring the ranker's priority (exact, prefix,
// substring, greedy subsequence) -- nvim-cmp's CmpItemAbbrMatch highlight.
#include "jot/integrations/lsp/matching.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace completion_matching;

TEST_CASE("completion matching: prefix positions", "[completion-matching]")
{
  const auto m = match_positions("pri", "printf");
  REQUIRE(m == std::vector<int>({0, 1, 2}));
}

TEST_CASE("completion matching: exact match covers the whole label",
          "[completion-matching]")
{
  const auto m = match_positions("foo", "foo");
  REQUIRE(m == std::vector<int>({0, 1, 2}));
}

TEST_CASE("completion matching: substring positions", "[completion-matching]")
{
  const auto m = match_positions("int", "point");
  REQUIRE(m == std::vector<int>({2, 3, 4}));
}

TEST_CASE("completion matching: case-insensitive prefix", "[completion-matching]")
{
  const auto m = match_positions("PRI", "printf");
  REQUIRE(m == std::vector<int>({0, 1, 2}));
}

TEST_CASE("completion matching: greedy subsequence", "[completion-matching]")
{
  const auto m = match_positions("tsp", "telescope");
  REQUIRE(m == std::vector<int>({0, 4, 7}));
}

TEST_CASE("completion matching: no match yields empty", "[completion-matching]")
{
  REQUIRE(match_positions("xyz", "abc").empty());
  REQUIRE(match_positions("", "abc").empty());
}

TEST_CASE("completion matching: multibyte labels keep byte offsets",
          "[completion-matching]")
{
  // 'é' is two bytes, so the substring starts at byte 2.
  const auto m = match_positions("cl", "éclair");
  REQUIRE(m == std::vector<int>({2, 3}));
}

TEST_CASE("completion matching: prefix longer than label yields empty",
          "[completion-matching]")
{
  REQUIRE(match_positions("toolong", "ab").empty());
}