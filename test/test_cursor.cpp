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

TEST_CASE("UI cursor blink visibility forces a flush only on change", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  // The blink clock flips visibility: the cursor must flush so the
  // terminal gets the show/hide sequence even when position did not move.
  ui.set_cursor(2, 2);
  ui.set_cursor_blink_visible(false);
  REQUIRE(ui.cursor_needs_flush());
  ui.flush_cursor();
  // Idle frames with the same visibility never re-emit cursor bytes.
  REQUIRE_FALSE(ui.cursor_needs_flush());
  ui.set_cursor_blink_visible(false);
  REQUIRE_FALSE(ui.cursor_needs_flush());
  // Back to visible: flush again, and the emitted sequence shows the
  // steady shape (blinking is software-side now).
  ui.set_cursor_blink_visible(true);
  REQUIRE(ui.cursor_needs_flush());
}

// The caret's terminal bytes. Composed in one place (UI::cursor_sequence) because
// the show sequence used to be written unconditionally after the hide branch:
// hide_cursor() then immediately re-showed the cursor, leaving a stray blinking
// caret parked over the frame whenever a menu, palette or popup was up.
TEST_CASE("Cursor sequences hide, or show with a steady shape", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(40, 10);

  // Visible: show plus the STEADY DECSCUSR shape. A blinking shape (1/5 q) here
  // would blink at the terminal's rate, on top of jot's own cursor_blink_ms
  // clock -- two clocks, which is the glitchy blink being fixed.
  ui.set_cursor(3, 3, UICursorShape::Bar);
  REQUIRE(ui.cursor_sequence() == "\033[?25h\033[6 q");
  ui.set_cursor(3, 3, UICursorShape::Block);
  REQUIRE(ui.cursor_sequence() == "\033[?25h\033[2 q");

  // Hidden: hide, and nothing else. No trailing show sequence.
  ui.hide_cursor();
  REQUIRE(ui.cursor_sequence() == "\033[?25l");

  // Blink phase off overrides the shape, and never emits a show.
  ui.set_cursor(3, 3, UICursorShape::Bar);
  ui.set_cursor_blink_visible(false);
  REQUIRE(ui.cursor_sequence() == "\033[?25l");
  // The hidden sequence must not contain a re-show anywhere in it.
  REQUIRE(ui.cursor_sequence().find("?25h") == std::string::npos);

  // Phase back on: the shape returns, and it is never a blinking DECSCUSR.
  ui.set_cursor_blink_visible(true);
  REQUIRE(ui.cursor_sequence() == "\033[?25h\033[6 q");
  REQUIRE(ui.cursor_sequence().find("1 q") == std::string::npos);
  REQUIRE(ui.cursor_sequence().find("5 q") == std::string::npos);
}
