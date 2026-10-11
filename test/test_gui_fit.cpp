// GUI grid geometry (src/gui/gui_fit.cpp).
//
// These pin the unit split that caused the "editor only fills part of the
// window" bug: window sizes arrive in logical points, cells and the renderer
// work in device pixels. Fitting points against device-pixel cells yields a
// grid of 1/scale the window on any scaled (HiDPI / fractional) display.
#include "gui/gui_fit.h"

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

// The fitted grid is whole cells, so flooring left up to a cell of bare
// background along the right and bottom edges of the window. At the bottom that
// read as an empty margin under the status line, which is the last row of the
// grid: the window was resized to a pixel height the cell height did not divide.
// The pitch spreads that remainder over the cells that fit.
TEST_CASE("Gui fit spreads the sub-cell remainder so the grid covers the drawable", "[jot]")
{
  // An exact division leaves the font's own cell alone.
  const CellPitch exact = pitch_to_fill(800, 480, 100, 30);
  REQUIRE(exact.cell_w == 8.0f);
  REQUIRE(exact.cell_h == 16.0f);
  const GridFit exact_fit = fit_grid(800, 480, 8.0f, 16.0f);
  REQUIRE(exact_fit.cols == 100);
  REQUIRE(exact_fit.rows == 30);

  // 1897x194 px of window at a 10x20 px cell: 9 rows fit (a tenth would need
  // 200 px), and the 14 px left over used to sit below the grid as background.
  const GridFit fit = fit_grid(1897, 194, 10.0f, 20.0f);
  REQUIRE(fit.cols == 189);
  REQUIRE(fit.rows == 9);
  const CellPitch pitch = pitch_to_fill(1897, 194, fit.cols, fit.rows);

  // The grid covers the drawable exactly, to both edges.
  REQUIRE(pitch.cell_w * (float)fit.cols >= 1897.0f - 0.01f);
  REQUIRE(pitch.cell_h * (float)fit.rows >= 194.0f - 0.01f);
  // What it adds is the remainder and nothing else: less than one cell in
  // total, and no cell stretched by a whole cell.
  REQUIRE((pitch.cell_w - 10.0f) * (float)fit.cols < 10.0f);
  REQUIRE((pitch.cell_h - 20.0f) * (float)fit.rows < 20.0f);
  REQUIRE(pitch.cell_w < 20.0f);
  REQUIRE(pitch.cell_h < 40.0f);
  // And it only ever grows: the cell count came from flooring, so the pitch can
  // never come out below the font's cell.
  REQUIRE(pitch.cell_w >= 10.0f - 0.01f);
  REQUIRE(pitch.cell_h >= 20.0f - 0.01f);

  // A 2x drawable: the same reasoning, and the result is still the font's cell
  // when the division is exact.
  const GridFit hidpi = fit_grid(2560, 1440, 16.0f, 32.0f);
  const CellPitch hidpi_pitch = pitch_to_fill(2560, 1440, hidpi.cols, hidpi.rows);
  REQUIRE(hidpi_pitch.cell_w == 16.0f);
  REQUIRE(hidpi_pitch.cell_h == 32.0f);

  // Degenerate sizes: a 1x1 grid in a zero-sized drawable must not divide by
  // zero or come out as a zero-sized cell.
  const CellPitch empty = pitch_to_fill(0, 0, 0, 0);
  REQUIRE(empty.cell_w >= 1.0f);
  REQUIRE(empty.cell_h >= 1.0f);
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

// The caret's glide. It eases so a one-cell move reads as motion rather than a
// teleport -- except while the pane under it is scrolling, where the content is
// already sliding and a second, slower curve for the caret left it visibly
// trailing its own line (the "caret gets dragged" symptom).
TEST_CASE("The caret glide eases normally and snaps while scrolling", "[jot]")
{
  // Easing: part-way after one frame, monotonically closer, never overshooting.
  const float eased = glide_axis(0.0f, 100.0f, 0.016f, /*snap=*/false);
  REQUIRE(eased > 0.0f);
  REQUIRE(eased < 100.0f);

  const float eased_more = glide_axis(eased, 100.0f, 0.016f, false);
  REQUIRE(eased_more > eased);
  REQUIRE(eased_more < 100.0f);

  // Many frames converge on the target and settle exactly there.
  float pos = 0.0f;
  for (int i = 0; i < 100; i++)
  {
    pos = glide_axis(pos, 100.0f, 0.016f, false);
  }
  REQUIRE(pos == 100.0f);

  // Snapping: placed on the target immediately, whatever the distance -- this is
  // what keeps the caret glued to its line while the viewport slides.
  REQUIRE(glide_axis(0.0f, 100.0f, 0.016f, /*snap=*/true) == 100.0f);
  REQUIRE(glide_axis(0.0f, -37.0f, 0.016f, true) == -37.0f);
  REQUIRE(glide_axis(50.0f, 50.0f, 0.016f, true) == 50.0f);

  // Sub-pixel remainders snap so the caret lands crisply on its cell.
  REQUIRE(glide_axis(99.99f, 100.0f, 0.016f, false) == 100.0f);

  // A zero (or negative) frame delta must not move the caret, and a settled
  // caret stays put rather than drifting.
  REQUIRE(glide_axis(10.0f, 100.0f, 0.0f, false) == 10.0f);
  REQUIRE(glide_axis(100.0f, 100.0f, 0.016f, false) == 100.0f);
}
