// Comment toggling: per-extension style lookup (imported from comment.nvim's
// ft.lua) and the pure multi-line toggle logic, including the single-line
// block unwrap fix (<!-- x --> -> x) and blank-line skipping.
#include "commenting.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace
{
  void toggle(std::vector<std::string> &lines,
              const commenting::Style &style,
              int start,
              int end,
              bool multi_line,
              bool block_boundary = false)
  {
    commenting::toggle_lines(lines, style, start, end, multi_line, block_boundary);
  }
} // namespace

TEST_CASE("Comment styles resolve per extension", "[commenting]")
{
  // The old hardcoded table mapped Lua to '#': comment.nvim says '--'.
  REQUIRE(commenting::style_for(".lua").prefix == "--");
  REQUIRE(commenting::style_for(".lua").block_open == "--[[");
  REQUIRE(commenting::style_for(".html").prefix == "<!--");
  REQUIRE(commenting::style_for(".html").suffix == "-->");
  REQUIRE(commenting::style_for(".clj").prefix == ";");
  REQUIRE(commenting::style_for(".erl").prefix == "%%");
  REQUIRE(commenting::style_for(".py").prefix == "#");
  REQUIRE(commenting::style_for(".css").prefix == "/*");
  REQUIRE(commenting::style_for(".cpp").prefix == "//");
  // Unknown extensions fall back to the C-family form.
  REQUIRE(commenting::style_for(".zzz").prefix == "//");
}

TEST_CASE("Single line toggles line comments", "[commenting]")
{
  const commenting::Style cpp = commenting::style_for(".cpp");
  std::vector<std::string> lines = {"int x = 1;"};
  toggle(lines, cpp, 0, 0, false);
  REQUIRE(lines == std::vector<std::string>{"//int x = 1;"});
  toggle(lines, cpp, 0, 0, false);
  REQUIRE(lines == std::vector<std::string>{"int x = 1;"});
}

TEST_CASE("Single-line block comments unwrap fully", "[commenting]")
{
  // Regression: the old logic removed the opener but left the closer behind
  // ("hello -->") because the start and end branches were mutually exclusive.
  const commenting::Style html = commenting::style_for(".html");
  std::vector<std::string> lines = {"<!-- hello -->"};
  toggle(lines, html, 0, 0, false);
  REQUIRE(lines == std::vector<std::string>{"hello"});
  toggle(lines, html, 0, 0, false);
  // Re-wrap uses the commentstring verbatim: <!--%s--> has no padding.
  REQUIRE(lines == std::vector<std::string>{"<!--hello-->"});

  const commenting::Style css = commenting::style_for(".css");
  lines = {"/* hi */"};
  toggle(lines, css, 0, 0, false);
  REQUIRE(lines == std::vector<std::string>{"hi"});
}

TEST_CASE("Multi-line selection comments every line", "[commenting]")
{
  // Regression: the old logic wrapped a multi-line selection in one
  // /* ... */ pair, so middle lines carried no markers at all.
  const commenting::Style cpp = commenting::style_for(".cpp");
  std::vector<std::string> lines = {"foo();", "  bar();", "baz();"};
  toggle(lines, cpp, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"//foo();", "  //bar();", "//baz();"});
  toggle(lines, cpp, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"foo();", "  bar();", "baz();"});
}

TEST_CASE("Lua multi-line comments every line", "[commenting]")
{
  const commenting::Style lua = commenting::style_for(".lua");
  std::vector<std::string> lines = {"a = 1", "b = 2"};
  toggle(lines, lua, 0, 1, true);
  REQUIRE(lines == std::vector<std::string>{"--a = 1", "--b = 2"});
  toggle(lines, lua, 0, 1, true);
  REQUIRE(lines == std::vector<std::string>{"a = 1", "b = 2"});
}

TEST_CASE("HTML multi-line comments every line", "[commenting]")
{
  const commenting::Style html = commenting::style_for(".html");
  std::vector<std::string> lines = {"<p>a</p>", "<p>b</p>", "<p>c</p>"};
  toggle(lines, html, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"<!--<p>a</p>-->", "<!--<p>b</p>-->", "<!--<p>c</p>-->"});
  toggle(lines, html, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"<p>a</p>", "<p>b</p>", "<p>c</p>"});
}

TEST_CASE("Hand-written block pairs still unwrap as one range", "[commenting]")
{
  // Per-line commenting never creates wrapped pairs, but pre-existing
  // /* ... */ blocks must still unwrap cleanly from their boundaries.
  const commenting::Style cpp = commenting::style_for(".cpp");
  std::vector<std::string> lines = {"/* a", "b", "c */"};
  toggle(lines, cpp, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("Hash languages comment per line and skip blanks", "[commenting]")
{
  const commenting::Style py = commenting::style_for(".py");
  std::vector<std::string> lines = {"a", "b", "", "  c"};
  toggle(lines, py, 0, 3, true);
  REQUIRE(lines == std::vector<std::string>{"#a", "#b", "", "  #c"});
  // Blank lines are never marker-only, so the whole range is not
  // "all commented"; toggling the commented subset unwraps cleanly.
  toggle(lines, py, 0, 1, true);
  REQUIRE(lines == std::vector<std::string>{"a", "b", "", "  #c"});
}

TEST_CASE("Mixed selections finish with per-line markers", "[commenting]")
{
  const commenting::Style cpp = commenting::style_for(".cpp");
  std::vector<std::string> lines = {"//a", "b"};
  toggle(lines, cpp, 0, 1, true);
  REQUIRE(lines == std::vector<std::string>{"//a", "//b"});
}

TEST_CASE("Per-line block style unwraps each line", "[commenting]")
{
  const commenting::Style html = commenting::style_for(".html");
  std::vector<std::string> lines = {"<!-- a -->", "<!-- b -->"};
  // Not a single wrapped pair: every line carries its own markers, so the
  // toggle strips each line's prefix + suffix.
  toggle(lines, html, 0, 1, true);
  REQUIRE(lines == std::vector<std::string>{"a", "b"});
}

TEST_CASE("Blank lines never grow marker-only rows", "[commenting]")
{
  const commenting::Style cpp = commenting::style_for(".cpp");
  std::vector<std::string> lines = {"a();", "", "b();"};
  toggle(lines, cpp, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"//a();", "", "//b();"});
  // The blank line keeps the range from being "all commented", so a second
  // toggle is a no-op (idempotent) instead of mangling the blank row.
  toggle(lines, cpp, 0, 2, true);
  REQUIRE(lines == std::vector<std::string>{"//a();", "", "//b();"});
}