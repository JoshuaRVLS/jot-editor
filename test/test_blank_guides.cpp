// Blank-line indent guide inheritance (indent-blankline's blankline rule):
// empty lines draw the next non-blank line's indent guides so the guide
// column reads as one continuous vertical line across blank rows. Trailing
// blanks at EOF draw nothing, and a blank run just before a closing line
// (`}`, `)`, `]`, `end`) inherits the previous non-blank line's indent.
#include "render/blank_guides.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace blank_guides;

namespace
{
  std::vector<std::string> L(std::initializer_list<const char *> lines)
  {
    std::vector<std::string> out;
    for (const char *s : lines)
    {
      out.emplace_back(s);
    }
    return out;
  }

  int source_of(const std::vector<std::string> &lines, int idx)
  {
    return guide_source_line((int)lines.size(),
                             [&](int i) -> const std::string & { return lines[i]; },
                             idx);
  }
} // namespace

TEST_CASE("blank guides: empty line inherits the next non-blank line",
          "[blank-guides]")
{
  const auto lines = L({"if (x) {", "    foo();", "", "    bar();", "}"});
  REQUIRE(source_of(lines, 2) == 3);
}

TEST_CASE("blank guides: blank run shares one source line", "[blank-guides]")
{
  const auto lines = L({"    a();", "", "", "    b();"});
  REQUIRE(source_of(lines, 1) == 3);
  REQUIRE(source_of(lines, 2) == 3);
}

TEST_CASE("blank guides: closer ahead inherits the previous indent",
          "[blank-guides]")
{
  const auto lines = L({"if (x) {", "    foo();", "", "}"});
  REQUIRE(source_of(lines, 2) == 1);
}

TEST_CASE("blank guides: closer ahead with blank run uses the last code line",
          "[blank-guides]")
{
  const auto lines = L({"    a();", "", "", "}"});
  REQUIRE(source_of(lines, 1) == 0);
  REQUIRE(source_of(lines, 2) == 0);
}

TEST_CASE("blank guides: trailing blanks at EOF draw nothing", "[blank-guides]")
{
  const auto lines = L({"    foo();", "", ""});
  REQUIRE(source_of(lines, 1) == -1);
  REQUIRE(source_of(lines, 2) == -1);
}

TEST_CASE("blank guides: whole-file blank draws nothing", "[blank-guides]")
{
  const auto lines = L({"", "", ""});
  REQUIRE(source_of(lines, 0) == -1);
}

TEST_CASE("blank guides: closing lines are detected", "[blank-guides]")
{
  REQUIRE(is_closing_line("}"));
  REQUIRE(is_closing_line("  )"));
  REQUIRE(is_closing_line("\t]"));
  REQUIRE(is_closing_line("end"));
  REQUIRE(is_closing_line("  end -- comment"));
  REQUIRE(is_closing_line("endif"));
  REQUIRE_FALSE(is_closing_line(""));
  REQUIRE_FALSE(is_closing_line("else:"));
  REQUIRE_FALSE(is_closing_line("x = 1"));
  // indent-blankline's regex matches the bare `end` substring too.
  REQUIRE(is_closing_line("endless loop()"));
}

TEST_CASE("blank guides: leading whitespace is measured", "[blank-guides]")
{
  REQUIRE(leading_ws("") == 0);
  REQUIRE(leading_ws("x") == 0);
  REQUIRE(leading_ws("  ") == 2);
  REQUIRE(leading_ws("\t\tx") == 2);
  REQUIRE(leading_ws("  x  ") == 2);
}