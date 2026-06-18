#include <catch2/catch_test_macros.hpp>
#include "tools/terminal/integrated.h"

TEST_CASE("Integrated Terminal Default Colors Resolve To Theme", "[jot]") {
  IntegratedTerminal::StyledCell cell{"x", 7, 0, true, true, false};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  REQUIRE(colors.fg == 189);
  REQUIRE(colors.bg == 235);
}

TEST_CASE("Integrated Terminal Explicit Colors Stay Explicit", "[jot]") {
  IntegratedTerminal::StyledCell cell{"x", 196, 22, false, false, false};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  REQUIRE(colors.fg == 196);
  REQUIRE(colors.bg == 22);
}

TEST_CASE("Integrated Terminal Reverse Swaps After Default Resolution", "[jot]") {
  IntegratedTerminal::StyledCell cell{"x", 7, 22, true, false, true};
  auto colors = IntegratedTerminal::resolve_cell_colors(cell, 189, 235);

  REQUIRE(colors.fg == 22);
  REQUIRE(colors.bg == 189);
}
