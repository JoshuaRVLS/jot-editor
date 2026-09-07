#include "column_utils.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>

// The row-diff renderer only emits rows whose content changed since the
// last frame. Immediate-mode drawing repaints the whole grid every frame,
// so render() compares each row against the retained copy of the last
// frame actually written to the terminal and skips identical rows. These
// tests pin that behavior: an identical repaint must emit far fewer bytes
// than the initial full frame, and a single-row change must emit roughly
// one row's worth.
//
// The UI is pointed at a real Terminal whose output lands on stdout (and
// into the ctest log); assertions read last_flush_bytes_ after each
// render, so the exact byte counts are deterministic per frame.
TEST_CASE("UI render skips rows unchanged since the last frame", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(40, 10);

  // Frame 1: every row painted with a distinct background. All rows differ
  // from the blank baseline, so the whole grid is emitted.
  for (int y = 0; y < 10; y++)
  {
    UIRect r{0, y, 40, 1};
    ui.fill_rect(r, " ", 7, 30 + y);
  }
  ui.render();
  const int full_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(full_bytes > 500);

  // Frame 2: byte-identical repaint (what immediate mode does every
  // frame). Rows are cell-identical to the last painted frame, so only
  // cursor/attribute setup should be emitted -- not the whole grid.
  for (int y = 0; y < 10; y++)
  {
    UIRect r{0, y, 40, 1};
    ui.fill_rect(r, " ", 7, 30 + y);
  }
  ui.render();
  const int skip_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(skip_bytes < 300);

  // Frame 3: only row 5 changes. Just that row is emitted; the other nine
  // stay skipped.
  UIRect changed{0, 5, 40, 1};
  ui.fill_rect(changed, " ", 7, 45);
  ui.render();
  const int one_row_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(one_row_bytes > 0);
  REQUIRE(one_row_bytes < full_bytes - 300);
}

TEST_CASE("Visual columns map tabs and wide graphemes", "[jot]")
{
  const std::string line = "a\t"
                           "\xE7\x95\x8C"
                           "b";

  REQUIRE(compute_visual_column(line, 1, 4) == 1);
  REQUIRE(compute_visual_column(line, 2, 4) == 4);
  REQUIRE(visual_to_logical_column(line, 4, 4) == 2);
  REQUIRE(visual_to_logical_column(line, 5, 4) == 2);
  REQUIRE(visual_to_logical_column(line, 6, 4) == 5);
}

TEST_CASE("UI cursor changes request an idle refresh", "[jot]")
{
  UI ui(nullptr);
  REQUIRE(ui.cursor_needs_flush());
  ui.set_cursor(1, 1);
  REQUIRE(ui.cursor_needs_flush());
  ui.hide_cursor();
  REQUIRE(ui.cursor_needs_flush());
}
