#include "column_utils.h"
#include "ui/cursor_probe.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

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

// invalidate() clears the physical screen -- it is what a caller reaches for
// when what the terminal shows cannot be trusted -- so the frame that follows
// has to write every row back out. Marking the rows dirty is not enough: the
// diff still skips a row whose cells equal last_grid, and last_grid describes
// exactly the picture this clear wiped. A settings change ran into it end to
// end: a config naming a legacy theme alias (jot-dark) re-applied the theme to
// the colours already up, cleared the screen, and then wrote nothing back, so
// the editor sat black until some other input changed a cell.
TEST_CASE("invalidate() makes the next frame repaint every row", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(40, 10);

  for (int y = 0; y < 10; y++)
  {
    UIRect r{0, y, 40, 1};
    ui.fill_rect(r, " ", 7, 30 + y);
  }
  ui.render();
  const int full_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(full_bytes > 500);

  // The same picture, painted onto a screen that was just cleared: the rows
  // are cell-identical to last_grid, so nothing short of a forced full repaint
  // can put them back on the terminal.
  ui.invalidate();
  for (int y = 0; y < 10; y++)
  {
    UIRect r{0, y, 40, 1};
    ui.fill_rect(r, " ", 7, 30 + y);
  }
  ui.render();
  const int after_invalidate = term.render_capture_bytes_since_last_flush();
  REQUIRE(after_invalidate >= full_bytes);

  // And the skip the frame above must not take is still there: with no
  // invalidate, the same byte-identical repaint writes nothing.
  for (int y = 0; y < 10; y++)
  {
    UIRect r{0, y, 40, 1};
    ui.fill_rect(r, " ", 7, 30 + y);
  }
  ui.render();
  REQUIRE(term.render_capture_bytes_since_last_flush() < 300);
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

// A frame the terminal did not take leaves the screen in an unknown state: the
// bytes that never went out are dropped rather than replayed, so the cells they
// carried are missing while the retained baseline believes they were painted.
// The next frame must repaint every row instead of diffing against that
// baseline -- the case a slow terminal hits when its pty buffer fills part-way
// through a frame, which used to leave those rows half-drawn until something
// unrelated happened to redraw them.
TEST_CASE("A frame the terminal could not take forces a full repaint", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(40, 10);

  const auto paint_every_row = [&]
  {
    for (int y = 0; y < 10; y++)
    {
      UIRect r{0, y, 40, 1};
      ui.fill_rect(r, " ", 7, 30 + y);
    }
  };

  // Frame 1: the baseline. Every row differs from the blank grid, so the whole
  // screen is written.
  paint_every_row();
  ui.render();
  const int full_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(full_bytes > 500);

  // Frame 2: identical content, so every row is skipped.
  paint_every_row();
  ui.render();
  const int skip_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE(skip_bytes < 300);
  REQUIRE_FALSE(ui.full_repaint_pending());

  // Frame 3: the terminal takes nothing from this frame.
  term.stall_next_flush_for_test();
  paint_every_row();
  ui.render();
  REQUIRE(ui.full_repaint_pending());

  // Frame 4: the same content again -- and it is written out in full anyway.
  // This is the recovery: the baseline is only a claim about the terminal, and
  // the failed frame invalidated it.
  paint_every_row();
  ui.render();
  const int recovery_bytes = term.render_capture_bytes_since_last_flush();
  REQUIRE_FALSE(ui.full_repaint_pending()); // consumed by this frame
  REQUIRE(recovery_bytes >= full_bytes - 64);
  REQUIRE(recovery_bytes > skip_bytes);

  // Frame 5: the baseline is accurate again, so idle frames go back to writing
  // almost nothing.
  paint_every_row();
  ui.render();
  REQUIRE(term.render_capture_bytes_since_last_flush() < 300);
}

// flush() reports whether the terminal took the whole frame; the renderer's
// response to "no" is the full repaint above, so the report has to be honest in
// both places bytes can go missing: the frame's own write, and a chunked flush
// that gave up part-way through building it.
TEST_CASE("flush reports a frame the terminal did not take", "[jot][ui]")
{
  Terminal term;
  term.write("frame bytes");
  REQUIRE(term.flush());

  // A stalled write drops its bytes rather than replaying them -- replaying
  // would re-send what the terminal may already have shown, which is its own
  // corruption -- and it reports the loss.
  term.stall_next_flush_for_test();
  term.write("frame bytes");
  REQUIRE_FALSE(term.flush());
  REQUIRE(term.pending_output_for_test().empty());

  // The seam is one-shot: the next frame is judged on its own.
  term.write("frame bytes");
  REQUIRE(term.flush());

  // A chunked flush (JOT_RENDER_CHUNK_BYTES) is called from inside the frame
  // builder, where there is nowhere to report a failure: the bytes it dropped
  // are only known to the frame it belonged to, so that frame's final flush has
  // to report it even though its own write lands in full.
  const char *saved_chunk = std::getenv("JOT_RENDER_CHUNK_BYTES");
  const std::string saved_chunk_value = saved_chunk ? saved_chunk : "";
  setenv("JOT_RENDER_CHUNK_BYTES", "4", 1);
  {
    Terminal chunked;
    if (saved_chunk)
      setenv("JOT_RENDER_CHUNK_BYTES", saved_chunk_value.c_str(), 1);
    else
      unsetenv("JOT_RENDER_CHUNK_BYTES");

    chunked.write("0123456789"); // past the 4-byte threshold
    chunked.stall_next_flush_for_test();
    chunked.flush_if_buffer_exceeds(); // drops this part of the frame
    chunked.write("tail"); // the frame keeps being built
    REQUIRE_FALSE(chunked.flush());
    // And the next frame is judged on its own again.
    chunked.flush_if_buffer_exceeds();
    REQUIRE(chunked.flush());
  }
}

// A hidden cursor is still *somewhere*. DECTCEM makes the terminal draw none, but
// the cursor is only ever at some cell, and every row write moves it: hiding it
// where the painting left it parks it at the end of the last row written -- the
// bottom-right corner of the status row. Anything that reveals it before the
// next frame puts it back on the caret (a repaint that is not this frame's, a
// read chunk boundary mid-frame, the window regaining focus) then shows the
// caret away from the text: the caret jumping to the corner of the screen. Both
// cursor paths therefore park it on the caret before hiding it.
TEST_CASE("A hidden cursor is parked on the caret rather than on the last row write", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(40, 10);

  // Where the cursor is put: the caret's own cell when this frame placed one, the
  // last cell a caret was placed on when it did not (the palette, a popup, a menu
  // -- those place no caret of their own), and always inside the right margin.
  int px = -1;
  int py = -1;
  ui.set_cursor(3, 2);
  ui.cursor_park_cell(px, py);
  REQUIRE(px == 3);
  REQUIRE(py == 2);
  ui.reset_cursor_state(); // nothing placed a caret: the frame is a menu's
  ui.cursor_park_cell(px, py);
  REQUIRE(px == 3);
  REQUIRE(py == 2);
  term.set_render_margin(1);
  ui.set_cursor(39, 9); // the right-edge cell the renderer never writes
  ui.cursor_park_cell(px, py);
  REQUIRE(px == 38);
  REQUIRE(py == 9);
  term.set_render_margin(0);

  // The blink's idle half is a cursor-only frame, and it carries the move as well
  // as the hide: a bare hide leaves the cursor wherever the last frame's rows put
  // it.
  ui.set_cursor(3, 2);
  ui.set_cursor_blink_visible(true);
  ui.flush_cursor();
  CHECK(term.render_capture_bytes_since_last_flush()
        == (int)std::string("\033[?7l\033[3;4H\033[?25h\033[2 q").size());
  ui.set_cursor_blink_visible(false);
  ui.flush_cursor();
  CHECK(term.render_capture_bytes_since_last_flush()
        == (int)std::string("\033[?7l\033[3;4H\033[?25l").size());
  // The terminal is told which of the two it just got: the DSR size probe parks
  // the cursor in the corner and has to put that visibility back.
  CHECK_FALSE(term.cursor_visible());
  ui.set_cursor_blink_visible(true);
  ui.flush_cursor();
  CHECK(term.cursor_visible());

  // A frame that paints every row and ends hidden. Both frames below write every
  // row (each row's colour changes), so their row bytes are the same width and
  // the only difference left is the caret: shown writes `?25h` plus the `[2 q`
  // shape (11 bytes) where hidden writes `?25l` (6) -- and the same move to the
  // caret, which is what keeps the hidden cursor off the last row it wrote.
  const auto paint_all = [&](int bg)
  {
    for (int y = 0; y < 10; y++)
    {
      UIRect r{0, y, 40, 1};
      ui.fill_rect(r, " ", 7, bg + y);
    }
  };
  ui.set_cursor(3, 2);
  ui.set_cursor_blink_visible(true);
  paint_all(30);
  ui.render();
  const int rows_shown = term.render_capture_bytes_since_last_flush();
  ui.set_cursor(3, 2);
  ui.set_cursor_blink_visible(false);
  paint_all(50);
  ui.render();
  const int rows_hidden = term.render_capture_bytes_since_last_flush();
  REQUIRE(rows_shown - rows_hidden == 5);
}

// The DSR size probe is the one thing that still moves the cursor by itself: it
// parks at (999, 999) -- the bottom-right corner, the only cell whose report is
// the whole viewport -- and main_loop's slow fallback runs it every couple of
// seconds. A visible cursor there is the caret flashing up in the corner, so the
// probe hides it for the round trip and puts back exactly the visibility it
// found. DECTCEM is not part of DECSC, so the restore has to say so itself.
TEST_CASE("A cursor probe parks hidden and restores the caret's visibility", "[jot][ui]")
{
  const std::string hide = "\033[?25l";
  const std::string save = "\0337"; // ESC 7: DECSC
  const std::string back = "\0338"; // ESC 8: DECRC
  const std::string park = jot_ui::cursor_probe_park_bytes(999, 999);
  // The hide leads the write: the park can never land with a cursor on screen.
  REQUIRE(park.rfind(hide) == 0);
  REQUIRE(park.find(save) != std::string::npos);            // the DECSC
  REQUIRE(park.find("\033[999;999H") != std::string::npos); // the park
  REQUIRE(park.find("\033[6n") != std::string::npos);       // DSR
  // Hide, save, park, ask -- in that order.
  REQUIRE(park.find(hide) < park.find(save));
  REQUIRE(park.find(save) < park.find("\033[999;999H"));
  REQUIRE(park.find("\033[999;999H") < park.find("\033[6n"));
  // The scale probe parks on the first cell instead; same shape of write.
  REQUIRE(jot_ui::cursor_probe_park_bytes(1, 1).find("\033[1;1H") != std::string::npos);

  // Restoring a cursor that was on screen shows it *after* the DECRC: a show
  // before it would flash the caret in the corner the probe parked it in.
  const std::string shown = jot_ui::cursor_probe_restore_bytes(true);
  REQUIRE(shown.rfind(back) == 0);
  REQUIRE(shown.find("\033[?25h") != std::string::npos);
  REQUIRE(shown.find(back) < shown.find("\033[?25h"));
  // And one that was hidden stays hidden: no show anywhere in the restore.
  const std::string hidden = jot_ui::cursor_probe_restore_bytes(false);
  REQUIRE(hidden == back);
  REQUIRE(hidden.find("\033[?25h") == std::string::npos);
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
