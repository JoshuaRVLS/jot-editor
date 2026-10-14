// The event filter matcher (src/core/pattern.h).
//
// Both filters that take a pattern -- a jot.autocmd hook and a jot.events
// subscription -- go through this one matcher, so the cases here are the
// contract for both. The shapes that are easy to get wrong are the ones worth
// pinning: a single star stopping at a separator, a double star crossing one,
// a set that contains a ']', and the tail rule that makes "*.py" mean the file
// name rather than the whole path.

#include "core/pattern.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("pattern: a literal pattern matches only itself", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("notes.txt", "notes.txt"));
  REQUIRE_FALSE(jot_pattern::glob_match("notes.txt", "notes.txt.bak"));
  REQUIRE_FALSE(jot_pattern::glob_match("notes", "notes.txt"));
  REQUIRE(jot_pattern::glob_match("", ""));
  REQUIRE_FALSE(jot_pattern::glob_match("", "x"));
}

TEST_CASE("pattern: a star takes any run of characters but not a separator", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("*.py", "run.py"));
  REQUIRE_FALSE(jot_pattern::glob_match("*.py", "run.pyc"));
  REQUIRE_FALSE(jot_pattern::glob_match("*.py", "src/run.py"));
  REQUIRE(jot_pattern::glob_match("a*c", "abc"));
  REQUIRE(jot_pattern::glob_match("a*c", "ac"));
  REQUIRE(jot_pattern::glob_match("a*c", "axyzc"));
  REQUIRE(jot_pattern::glob_match("*", "anything at all"));
  REQUIRE(jot_pattern::glob_match("a*", "a"));
  REQUIRE(jot_pattern::glob_match("*a", "a"));
  // Two stars in one pattern: the second one takes over the backtracking, so
  // the first does not have to hold the whole tail.
  REQUIRE(jot_pattern::glob_match("*b*c", "abxc"));
  REQUIRE(jot_pattern::glob_match("*b*c", "abc"));
}

TEST_CASE("pattern: a double star crosses separators", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("**/run.py", "src/deep/run.py"));
  REQUIRE(jot_pattern::glob_match("**/run.py", "/src/run.py"));
  REQUIRE(jot_pattern::glob_match("**", "a/b/c"));
  REQUIRE(jot_pattern::glob_match("src/**/*.c", "src/a/b/main.c"));
  REQUIRE_FALSE(jot_pattern::glob_match("src/*.c", "src/a/b/main.c"));
  // A double star stands for characters, not for a path component: it is
  // "any run, separators included", so a component still has to be there.
  REQUIRE_FALSE(jot_pattern::glob_match("a/**/b", "a/b"));
  REQUIRE(jot_pattern::glob_match("a/**/b", "a/x/b"));
}

TEST_CASE("pattern: a question mark is one character, not a separator", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("a?c", "abc"));
  REQUIRE_FALSE(jot_pattern::glob_match("a?c", "ac"));
  REQUIRE_FALSE(jot_pattern::glob_match("a?c", "a/c"));
  REQUIRE(jot_pattern::glob_match("?/?", "a/b"));
}

TEST_CASE("pattern: sets, ranges and the inverted set", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("[ab]c", "ac"));
  REQUIRE(jot_pattern::glob_match("[ab]c", "bc"));
  REQUIRE_FALSE(jot_pattern::glob_match("[ab]c", "cc"));
  REQUIRE(jot_pattern::glob_match("[a-c]x", "bx"));
  REQUIRE_FALSE(jot_pattern::glob_match("[a-c]x", "dx"));
  REQUIRE(jot_pattern::glob_match("[!ab]c", "cc"));
  REQUIRE_FALSE(jot_pattern::glob_match("[!ab]c", "ac"));
  REQUIRE(jot_pattern::glob_match("[^ab]c", "cc"));
  // A ']' first in the set is a member of it.
  REQUIRE(jot_pattern::glob_match("[]x]", "]"));
  REQUIRE(jot_pattern::glob_match("[]x]", "x"));
  // A '-' that ends the set is a member, not a range.
  REQUIRE(jot_pattern::glob_match("[a-]", "-"));
  // No closing bracket: the '[' is a literal.
  REQUIRE(jot_pattern::glob_match("[abc", "[abc"));
  REQUIRE_FALSE(jot_pattern::glob_match("[abc", "a"));
}

TEST_CASE("pattern: a backslash escapes the next character", "[jot]")
{
  REQUIRE(jot_pattern::glob_match("a\\*b", "a*b"));
  REQUIRE_FALSE(jot_pattern::glob_match("a\\*b", "axb"));
  REQUIRE(jot_pattern::glob_match("a\\?b", "a?b"));
}

TEST_CASE("pattern: an empty pattern or a lone star is no filter", "[jot]")
{
  REQUIRE_FALSE(jot_pattern::is_filter(""));
  REQUIRE_FALSE(jot_pattern::is_filter("*"));
  REQUIRE(jot_pattern::is_filter("**"));
  REQUIRE(jot_pattern::is_filter("*.py"));
  // No filter takes everything, separators and all.
  REQUIRE(jot_pattern::path_match("", "/any/where/at/all.txt"));
  REQUIRE(jot_pattern::path_match("*", "/any/where/at/all.txt"));
}

TEST_CASE("pattern: a name pattern is also tried against the last component", "[jot]")
{
  REQUIRE(jot_pattern::path_match("*.py", "/home/me/proj/run.py"));
  REQUIRE(jot_pattern::path_match("*.py", "run.py"));
  REQUIRE_FALSE(jot_pattern::path_match("*.py", "/home/me/proj/run.pyc"));
  // A pattern that names a separator is about the whole path, so the tail rule
  // does not apply to it; a leading double star is how that is anchored.
  REQUIRE_FALSE(jot_pattern::path_match("proj/*.py", "/home/me/proj/run.py"));
  REQUIRE(jot_pattern::path_match("**/proj/*.py", "/home/me/proj/run.py"));
  REQUIRE(jot_pattern::path_match("/home/me/proj/*.py", "/home/me/proj/run.py"));
}
