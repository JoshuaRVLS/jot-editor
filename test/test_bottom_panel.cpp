// The bottom panel is one dock that hosts two views: the shell and the
// diagnostics list. They share its frame, its height and its view tabs, so
// these cases pin the parts that are easy to get wrong when a terminal-only
// panel grows a second tenant: the height reservation, the tab-strip
// hit-testing (the view tabs get their own row, the shell's strip the next
// one), and the focus handoff between the two.
#include "editor.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

namespace
{
  // A fresh editor per case: these drive real state transitions (view
  // switches, focus) and should not see another case's leftovers.
  void seed_config_home()
  {
    char home[] = "/tmp/jot_bottom_panel_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }
} // namespace

TEST_CASE("Bottom panel: the Problems view reserves height without a terminal", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);

  // The shell view with no terminal is not a panel at all: nothing to reserve.
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  REQUIRE(e.panel_reserved_h_for_test() == 0);

  // The list is as real as the shell. It has rows to show before any shell
  // exists, so it must take its height out of the pane area rather than
  // painting over it.
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.panel_reserved_h_for_test() >= 5);
}

TEST_CASE("Bottom panel: view tabs switch the view and hand off focus", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);

  // " Terminal " then " Problems ", then one gap cell before the shell's tabs.
  const int terminal_tab_w = (int)std::string(" Terminal ").size();
  const int problems_tab_w = (int)std::string(" Problems ").size();
  REQUIRE(e.bottom_panel_view_tabs_width_for_test() == terminal_tab_w + problems_tab_w + 1);

  const int tab_y = e.bottom_panel_view_tab_y_for_test();

  // Clicking the second view tab switches the panel and moves focus to the
  // list, so the shell stops swallowing keys.
  REQUIRE(e.bottom_panel_mouse_for_test(1 + terminal_tab_w, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE_FALSE(e.terminal_focused_for_test());
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);

  // ...and back again.
  REQUIRE(e.bottom_panel_mouse_for_test(2, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);
}

TEST_CASE("Bottom panel: the shell's own tabs get their own row", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);

  const int view_tab_y = e.bottom_panel_view_tab_y_for_test();
  const int shell_tab_y = e.bottom_panel_terminal_tab_y_for_test();
  const int view_tabs_w = e.bottom_panel_view_tabs_width_for_test();

  // The shell's content starts below both strips, and its second strip costs
  // one body row compared with the list (two strips, content, frame -- the top
  // frame is the pane area's rule, which the panel does not spend a row on).
  REQUIRE(e.bottom_panel_content_y_for_test() == shell_tab_y + 1);
  REQUIRE(e.bottom_panel_content_h_for_test() == e.terminal_panel_h_for_test() - 3);

  // The list spends the row on content instead, so it keeps one strip, content,
  // frame.
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.bottom_panel_content_y_for_test() == view_tab_y + 1);
  REQUIRE(e.bottom_panel_content_h_for_test() == e.terminal_panel_h_for_test() - 2);
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);

  // The first cell of the view row is the panel's: it is the "Terminal" view
  // tab, consumed before the shell's strip ever sees the click.
  REQUIRE(e.bottom_panel_mouse_for_test(1, view_tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);

  // Past the view labels that row is inert chrome: the panel declines it and
  // the shell consumes it without focusing anything, so a click above the
  // shell's own strip can never activate it.
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(1 + view_tabs_w, view_tab_y, true));
  REQUIRE(e.terminal_mouse_for_test(1 + view_tabs_w, view_tab_y, true, false, false));
  REQUIRE_FALSE(e.terminal_focused_for_test());

  // The shell's strip spans the panel from the left edge of the row below, and
  // the view row no longer steals clicks from it.
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(1, shell_tab_y, true));
  REQUIRE(e.terminal_mouse_for_test(1, shell_tab_y, true, false, false));
}

TEST_CASE("Bottom panel: hovering the view tabs leaves the view alone", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);

  const int tab_y = e.bottom_panel_view_tab_y_for_test();
  const int problems_x = 1 + (int)std::string(" Terminal ").size();

  // The dispatcher passes is_click=false for both a motion and a release, so
  // this is what sweeping the pointer across the labels used to do: switch the
  // panel under the cursor, and hand focus to the list while the shell was
  // still being used.
  REQUIRE(e.bottom_panel_mouse_for_test(problems_x, tab_y, false));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);

  // It is still a tab: a press switches it.
  REQUIRE(e.bottom_panel_mouse_for_test(problems_x, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);

  // ...and the same holds on the way back, so a hover cannot bounce the view
  // back and forth either.
  REQUIRE(e.bottom_panel_mouse_for_test(1, tab_y, false));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.bottom_panel_mouse_for_test(1, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);
}

TEST_CASE("Bottom panel: the pane area inks the rule along its top", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  const int panel_y = e.terminal_panel_y_for_test();
  const int panel_h = e.terminal_panel_h_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  UI *ui = e.ui_for_test();

  // Exactly one rule above the panel, drawn by the pane area: no corner glyphs,
  // since the panel is not a box of its own.
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, panel_y - 1);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch == "─");
  }

  // The panel's first row carries the view tabs, so nothing draws a second
  // rule (or a corner) directly under the first.
  const UICell *first_col = ui->cell_at(0, panel_y);
  REQUIRE(first_col != nullptr);
  REQUIRE(first_col->ch != "┌");
  REQUIRE(first_col->ch != "─");
  const UICell *label = ui->cell_at(2, panel_y);
  REQUIRE(label != nullptr);
  REQUIRE(label->ch == "T"); // " Terminal "

  // The fullscreen toggle glyph is gone from the shell's tab strip; zooming is
  // keyboard-only (:termzoom, Alt+Shift+Z).
  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, tab_y);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch != "□");
  }

  // The bottom rule closes the panel without a corner against the screen edge.
  const UICell *bottom_right = ui->cell_at(panel_w - 1, panel_y + panel_h - 1);
  REQUIRE(bottom_right != nullptr);
  REQUIRE(bottom_right->ch == "─");

  // With the explorer up, that same row is the sidebar's bottom edge running
  // into the pane's: the vertical separator between them ends in a T instead of
  // the two boxes drawing a corner each.
  e.toggle_sidebar_for_test();
  REQUIRE(e.sidebar_visible_for_test());
  e.request_redraw_for_test();
  e.render_for_test();
  const int sidebar_w = e.sidebar_width_for_test();
  int corners = 0;
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, panel_y - 1);
    REQUIRE(cell != nullptr);
    REQUIRE((cell->ch == "─" || cell->ch == "┴"));
    if (cell->ch == "┴")
    {
      corners++;
      REQUIRE(x == sidebar_w - 1);
    }
  }
  REQUIRE(corners == 1);
  e.toggle_sidebar_for_test();
}

TEST_CASE("Bottom panel: the dock meets its bottom rule in a T", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  e.request_redraw_for_test();
  e.render_for_test();

  const int panel_y = e.terminal_panel_y_for_test();
  const int panel_h = e.terminal_panel_h_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  const int bottom_row = panel_y + panel_h - 1;
  UI *ui = e.ui_for_test();

  // The dock is a region beside the panel, so the panel inks its right side.
  const UICell *side = ui->cell_at(panel_w - 1, panel_y);
  REQUIRE(side != nullptr);
  REQUIRE(side->ch == "│");

  // Both end on the same row, so the shared corner is a T (the dock's own rule
  // continues to the right) rather than an L.
  const UICell *junction = ui->cell_at(panel_w - 1, bottom_row);
  REQUIRE(junction != nullptr);
  REQUIRE(junction->ch == "┴");
  const UICell *dock_rule = ui->cell_at(panel_w, bottom_row);
  REQUIRE(dock_rule != nullptr);
  REQUIRE(dock_rule->ch == "─");

  // ...and the row takes the status background on both sides of the junction,
  // so it reads as one band of chrome instead of two panels' edges.
  REQUIRE(junction->bg == e.theme_for_test().bg_status);
  REQUIRE(dock_rule->bg == e.theme_for_test().bg_status);
}

TEST_CASE("Bottom panel: Problems navigation clamps to the list", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp", "/tmp/bp_a.cpp", 3, 1, "first");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp", "/tmp/bp_b.cpp", 5, 2, "second");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp", "/tmp/bp_c.cpp", 9, 3, "third");
  REQUIRE(e.workspace_diagnostics_for_test().size() == 3);

  e.show_problems_panel_for_test();
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.problems_selected_for_test() == 0);

  // k at the top stays put.
  REQUIRE(e.bottom_panel_key_for_test('k'));
  REQUIRE(e.problems_selected_for_test() == 0);

  REQUIRE(e.bottom_panel_key_for_test('j'));
  REQUIRE(e.problems_selected_for_test() == 1);
  // 1009 is the decoded Down arrow.
  REQUIRE(e.bottom_panel_key_for_test(1009));
  REQUIRE(e.problems_selected_for_test() == 2);
  // ...and j at the bottom stays put rather than running off the list.
  REQUIRE(e.bottom_panel_key_for_test('j'));
  REQUIRE(e.problems_selected_for_test() == 2);

  REQUIRE(e.bottom_panel_key_for_test('g'));
  REQUIRE(e.problems_selected_for_test() == 0);
  REQUIRE(e.bottom_panel_key_for_test('G'));
  REQUIRE(e.problems_selected_for_test() == 2);
}

TEST_CASE("Bottom panel: the list owns the keyboard while focused", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.show_problems_panel_for_test();

  // A stray letter belongs to the list, not the buffer underneath it.
  REQUIRE(e.bottom_panel_key_for_test('x'));
  REQUIRE(e.bottom_panel_key_for_test('i'));

  // Esc hands focus back to the editor.
  REQUIRE(e.bottom_panel_key_for_test(27));
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);
}
