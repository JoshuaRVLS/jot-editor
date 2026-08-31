#include "column_utils.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Visual columns map tabs and wide graphemes", "[jot]") {
  const std::string line = "a\t" "\xE7\x95\x8C" "b";

  REQUIRE(compute_visual_column(line, 1, 4) == 1);
  REQUIRE(compute_visual_column(line, 2, 4) == 4);
  REQUIRE(visual_to_logical_column(line, 4, 4) == 2);
  REQUIRE(visual_to_logical_column(line, 5, 4) == 2);
  REQUIRE(visual_to_logical_column(line, 6, 4) == 5);
}

TEST_CASE("UI cursor changes request an idle refresh", "[jot]") {
  UI ui(nullptr);
  REQUIRE(ui.cursor_needs_flush());
  ui.set_cursor(1, 1);
  REQUIRE(ui.cursor_needs_flush());
  ui.hide_cursor();
  REQUIRE(ui.cursor_needs_flush());
}
