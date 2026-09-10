// GUI grid geometry (src/ui/gui/gui_fit.cpp).
//
// These pin the unit split that caused the "editor only fills part of the
// window" bug: window sizes arrive in logical points, cells and the renderer
// work in device pixels. Fitting points against device-pixel cells yields a
// grid of 1/scale the window on any scaled (HiDPI / fractional) display.
#include "ui/gui/gui_fit.h"

#include <catch2/catch_test_macros.hpp>

using namespace jot_gui;

TEST_CASE("Gui fit derives the display scale from window vs drawable", "[jot]")
{
  // Unscaled: points and pixels agree.
  REQUIRE(display_scale(1280, 720, 1280, 720) == 1.0f);
  // 2x HiDPI.
  REQUIRE(display_scale(1280, 720, 2560, 1440) == 2.0f);
  // Fractional compositor scaling.
  REQUIRE(display_scale(1000, 500, 1250, 625) == 1.25f);
  REQUIRE(display_scale(1000, 500, 1500, 750) == 1.5f);
  // A drawable smaller than the window would mean the scale is below 1: clamp
  // rather than shrink text below its pixel size.
  REQUIRE(display_scale(1000, 500, 500, 250) == 1.0f);
  // Degenerate sizes must not produce a nonsense scale (or divide by zero).
  REQUIRE(display_scale(0, 0, 100, 100) == 1.0f);
  REQUIRE(display_scale(100, 100, 0, 0) == 1.0f);
  // A wildly wrong report is capped instead of collapsing the grid.
  REQUIRE(display_scale(100, 100, 100000, 100000) == 8.0f);
}

TEST_CASE("Gui fit fills the drawable with whole cells", "[jot]")
{
  // 8x16 px cells in a 800x480 px drawable.
  const GridFit fit = fit_grid(800, 480, 8.0f, 16.0f);
  REQUIRE(fit.cols == 100);
  REQUIRE(fit.rows == 30);

  // Partial cells are dropped, never rounded up past the edge.
  const GridFit partial = fit_grid(799, 479, 8.0f, 16.0f);
  REQUIRE(partial.cols == 99);
  REQUIRE(partial.rows == 29);

  // The regression itself: a 2x window must give the same grid as the same
  // window at 1x, not half of it. (Before the fix the 2x case was computed as
  // window-points / device-pixel-cells and came out at half.)
  const GridFit scaled = fit_grid(2560, 1440, 16.0f, 32.0f);
  REQUIRE(scaled.cols == 160);
  REQUIRE(scaled.rows == 45);

  // A zero-sized drawable (minimize, mid-configure) still yields a valid grid.
  const GridFit empty = fit_grid(0, 0, 8.0f, 16.0f);
  REQUIRE(empty.cols == 1);
  REQUIRE(empty.rows == 1);
  // So do nonsense cell metrics.
  const GridFit bad_cells = fit_grid(800, 480, 0.0f, 0.0f);
  REQUIRE(bad_cells.cols == 1);
  REQUIRE(bad_cells.rows == 1);
}

TEST_CASE("Gui fit translates pointer coordinates through the point cell", "[jot]")
{
  // Unscaled: the point cell is the pixel cell.
  REQUIRE(point_cell_size(10.0f, 1.0f) == 10.0f);
  // 2x: the same glyph is 5 logical points wide, which is what SDL mouse
  // coordinates are measured in.
  REQUIRE(point_cell_size(10.0f, 2.0f) == 5.0f);
  REQUIRE(point_cell_size(18.0f, 1.5f) == 12.0f);
  // Degenerate inputs fall back instead of dividing by zero.
  REQUIRE(point_cell_size(10.0f, 0.0f) == 10.0f);
  REQUIRE(point_cell_size(1.0f, 100.0f) > 0.0f);
}
