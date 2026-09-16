// The bottom panel is one dock that hosts two views: the shell and the
// diagnostics list. They share its frame, its height and its view tabs, so
// these cases pin the parts that are easy to get wrong when a terminal-only
// panel grows a second tenant: the height reservation, the tab-strip
// hit-testing (including the inset on the shell's own tabs), and the focus
// handoff between the two.
#include "editor.h"
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

  const int tab_y = e.terminal_panel_y_for_test() + 1;

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

TEST_CASE("Bottom panel: the shell's own tabs start after the view tabs", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);

  const int tab_y = e.terminal_panel_y_for_test() + 1;
  const int view_tabs_w = e.bottom_panel_view_tabs_width_for_test();

  // The leading columns are the panel's: the first cell is the "Terminal" view
  // tab, consumed before the shell's strip ever sees the click.
  REQUIRE(e.bottom_panel_mouse_for_test(1, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);

  // The shell's first tab is inset past them, and the panel must decline there
  // so the click reaches the shell. Without the inset the two strips overlap
  // and a click on a shell tab reads as a view tab instead.
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(1 + view_tabs_w, tab_y, true));
  REQUIRE(e.terminal_mouse_for_test(1 + view_tabs_w, tab_y, true, false, false));
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
