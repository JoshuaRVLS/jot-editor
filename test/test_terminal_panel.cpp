// Integrated terminal panel: geometry helpers, fullscreen zoom, and the
// top-border resize drag. Headless: the terminal state is configured via
// test accessors (no shell is spawned), and the panel rect + drag math is
// exercised against a real UI instance.
#include "editor.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_terminal_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("Terminal panel geometry: bottom panel vs fullscreen zoom", "[jot]")
{
  Editor &e = probe_editor();
  const int screen_h = e.ui_height_for_test();

  // Bottom panel: the stored height, capped at (screen - status - tab - 5).
  e.set_terminal_state_for_test(true, false, 10);
  REQUIRE_FALSE(e.terminal_zoom_active_for_test());
  int panel_h = e.terminal_panel_h_for_test();
  int panel_y = e.terminal_panel_y_for_test();
  int panel_w = e.terminal_panel_w_for_test();
  REQUIRE(panel_h >= 5);
  // The panel sits above the status line and never covers the tab strip.
  REQUIRE(panel_y >= 1);
  // Its own rows start with the view tabs: the separator along its top belongs
  // to the pane area above, so the panel has no border row to spend.
  REQUIRE(e.bottom_panel_view_tab_y_for_test() == panel_y);
  REQUIRE(e.bottom_panel_terminal_tab_y_for_test() == panel_y + 1);
  REQUIRE(panel_y + panel_h <= screen_h - 1);
  REQUIRE(panel_w >= 1);

  // Zoomed: the panel owns everything below the menu bar (0 rows in the
  // compact layout), full width.
  e.set_terminal_state_for_test(true, true, 10);
  REQUIRE(e.terminal_zoom_active_for_test());
  REQUIRE(e.terminal_panel_y_for_test() == 0);
  REQUIRE(e.terminal_panel_h_for_test() >= screen_h - 3);
  REQUIRE(e.terminal_panel_w_for_test() == e.ui_width_for_test());

  // Un-zooming restores the bottom-panel geometry.
  e.set_terminal_state_for_test(true, false, 10);
  REQUIRE(e.terminal_panel_y_for_test() == panel_y);
  REQUIRE(e.terminal_panel_h_for_test() == panel_h);
}

TEST_CASE("Terminal panel: hidden panel reserves no pane height", "[jot]")
{
  Editor &e = probe_editor();

  e.set_terminal_state_for_test(false, false, 10);
  REQUIRE(e.terminal_panel_h_for_test() == 10);

  // Zoomed panels also reserve nothing (they paint over the pane area).
  e.set_terminal_state_for_test(true, true, 10);
  REQUIRE(e.terminal_panel_h_for_test() >= 5);
}

TEST_CASE("Sidebar shrinks to leave the terminal its full height", "[jot]")
{
  Editor &e = probe_editor();
  const int screen_h = e.ui_height_for_test();
  const int status_h = 2; // status line height used by the headless UI

  // A real (shell-less) terminal so the reservation is active.
  e.add_terminal_for_test();

  // Small terminal: the sidebar keeps everything above it.
  e.set_terminal_state_for_test(true, false, 10);
  REQUIRE(e.sidebar_panel_h_for_test() == screen_h - status_h - 10);

  // Tall terminal (well beyond the old 50% cap): the sidebar must shrink
  // to the pane area above the panel's real footprint -- not the old
  // clamped half-screen reservation that let the explorer slide under
  // the terminal.
  e.set_terminal_state_for_test(true, false, screen_h);
  REQUIRE(e.terminal_panel_h_for_test() > screen_h / 2);
  REQUIRE(e.sidebar_panel_h_for_test() < screen_h / 2);
  REQUIRE(e.sidebar_panel_h_for_test()
          == screen_h - status_h - e.terminal_panel_h_for_test());

  // Hidden terminal: the sidebar spans the full pane area again.
  e.set_terminal_state_for_test(false, false, screen_h);
  REQUIRE(e.sidebar_panel_h_for_test() == screen_h - status_h);
}

TEST_CASE("Terminal resize drag clamps to the pane-preserving range", "[jot]")
{
  Editor &e = probe_editor();
  const int screen_h = e.ui_height_for_test();
  e.set_terminal_state_for_test(true, false, 10);
  // The handle is the rule the pane area inks above the panel.
  const int border_y = e.terminal_panel_y_for_test() - 1;

  // Dragging that rule up (start at the rule, end near the top) grows the
  // terminal.
  REQUIRE(e.terminal_resize_begin_for_test(10, border_y));
  REQUIRE(e.terminal_resize_dragging_for_test());
  REQUIRE(e.terminal_resize_update_for_test(5));
  REQUIRE(e.terminal_panel_h_for_test() > 10);

  // The stored height can never exceed the clamp: dragging to the top of
  // the screen caps at screen - status - tab - 5. (update() returns false
  // once the clamp is reached -- already at max -- so don't REQUIRE it.)
  e.terminal_resize_update_for_test(1);
  const int max_h = e.terminal_panel_h_for_test();
  REQUIRE(max_h <= screen_h - 2);

  // Dragging below the panel's own top edge shrinks it again, and the
  // 5-row floor holds.
  e.terminal_resize_begin_for_test(10, e.terminal_panel_y_for_test() - 1);
  REQUIRE(e.terminal_resize_update_for_test(200));
  REQUIRE(e.terminal_panel_h_for_test() >= 5);

  e.terminal_resize_end_for_test();
  REQUIRE_FALSE(e.terminal_resize_dragging_for_test());

  // A click on the panel's own rows never starts a drag: its first row is the
  // view tabs, which the bottom-panel handler owns.
  REQUIRE_FALSE(e.terminal_resize_begin_for_test(10, e.terminal_panel_y_for_test()));
  REQUIRE_FALSE(e.terminal_resize_begin_for_test(10, e.terminal_panel_y_for_test() + 1));
  REQUIRE_FALSE(e.terminal_resize_dragging_for_test());
  // Leave the drag state clean so later test cases start from a known
  // state even if a REQUIRE above aborted this case early.
  e.terminal_resize_end_for_test();
}

TEST_CASE("Terminal resize drag is disabled while zoomed", "[jot]")
{
  Editor &e = probe_editor();
  e.set_terminal_state_for_test(true, true, 10);
  REQUIRE_FALSE(e.terminal_resize_begin_for_test(10, e.terminal_panel_y_for_test()));
  REQUIRE_FALSE(e.terminal_resize_dragging_for_test());
}

TEST_CASE("Terminal mouse selection: click starts, drag extends, release copies", "[jot]")
{
  Editor &e = probe_editor();
  e.set_terminal_state_for_test(true, false, 10);
  e.add_terminal_for_test();
  const int content_y = e.bottom_panel_content_y_for_test();

  // A click in the content area anchors the selection there and starts the
  // drag (col = x - 1: the content starts at screen column 1).
  REQUIRE(e.terminal_mouse_for_test(5, content_y, true, false, false));
  REQUIRE(e.terminal_sel_active_for_test());
  REQUIRE(e.terminal_sel_dragging_for_test());
  REQUIRE(e.terminal_sel_anchor_col_for_test() == 4);
  REQUIRE(e.terminal_sel_anchor_row_for_test() == e.terminal_sel_cur_row_for_test());

  // Dragging down-right extends the selection (clamped to the panel rows).
  REQUIRE(e.terminal_mouse_for_test(9, content_y + 1, false, true, false));
  REQUIRE(e.terminal_sel_dragging_for_test());
  REQUIRE(e.terminal_sel_cur_col_for_test() == 8);
  REQUIRE(e.terminal_sel_cur_row_for_test() > e.terminal_sel_anchor_row_for_test());

  // Releasing keeps the selection but ends the drag (and copies: the
  // headless terminal has no rows yet, so the copy is a no-op).
  REQUIRE(e.terminal_mouse_for_test(9, content_y + 1, false, false, true));
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());
  REQUIRE(e.terminal_sel_active_for_test());

  // A second click re-anchors; clearing resets everything.
  REQUIRE(e.terminal_mouse_for_test(3, content_y, true, false, false));
  REQUIRE(e.terminal_sel_anchor_col_for_test() == 2);
  e.clear_terminal_selection();
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());
}

TEST_CASE("Terminal selection clears when the panel closes", "[jot]")
{
  Editor &e = probe_editor();
  e.set_terminal_state_for_test(true, false, 10);
  e.add_terminal_for_test();
  const int content_y = e.bottom_panel_content_y_for_test();

  e.terminal_mouse_for_test(5, content_y, true, false, false);
  REQUIRE(e.terminal_sel_active_for_test());

  // Closing the last terminal hides the panel and drops the selection so a
  // later :terminalnew can't resurrect stale highlights.
  e.close_terminal_for_test(0);
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());
}

TEST_CASE("Terminal clicks on the tab strip never start a selection", "[jot]")
{
  Editor &e = probe_editor();
  e.set_terminal_state_for_test(true, false, 10);
  e.add_terminal_for_test();
  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();

  // The first tab starts at x=1 and leads with the shell glyph (x=2) before
  // its "term 1" name; pressing it activates the terminal, and it must not
  // anchor a selection in the content area.
  REQUIRE(e.terminal_mouse_for_test(2, tab_y, true, false, false));
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());

  // Motions over the tab row are consumed but inert too.
  e.terminal_mouse_for_test(3, tab_y, false, true, false);
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
}

