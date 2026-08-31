#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("UI dim scrim preserves popup grid content", "[jot]") {
  UI ui(nullptr);
  ui.draw_text(1, 1, "base", 7, 0);
  ui.dim_rect({0, 0, 8, 4});

  REQUIRE(ui.cell_at(1, 1)->dim);
  REQUIRE(ui.cell_at(1, 1)->ch == "b");

  ui.draw_text(1, 1, "modal", 15, 0, true);
  REQUIRE_FALSE(ui.cell_at(1, 1)->dim);
  REQUIRE(ui.cell_at(1, 1)->ch == "m");
  REQUIRE(ui.cell_at(1, 1)->bold);
}
