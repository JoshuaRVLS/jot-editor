// The bottom panel is one dock that hosts two views: the shell and the
// diagnostics list. They share its frame, its tab bar and its height, so these
// cases pin the parts that are easy to get wrong: the height reservation, the
// bar's labels and action cluster (hit-tested, drawn and measured from the same
// geometry), the shell's session list down the panel's right side, and the
// focus handoff between the two views.
#include "editor.h"
#include "ui/text.h"
#include "ui/ui.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
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

  // Flat uppercase labels with a fixed gap between them: the bar is measured in
  // cells, and the two labels plus the gap are the whole strip.
  const int problems_w =
      ui_cell_count(Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_PROBLEMS));
  const int terminal_w =
      ui_cell_count(Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_TERMINAL));
  REQUIRE(problems_w == 8);
  REQUIRE(terminal_w == 8);
  REQUIRE(e.bottom_panel_view_tabs_width_for_test() == problems_w + terminal_w + 3);

  const int problems_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_PROBLEMS);
  const int terminal_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_TERMINAL);
  REQUIRE(problems_x == 1);
  REQUIRE(terminal_x == 1 + problems_w + 3);

  const int tab_y = e.bottom_panel_view_tab_y_for_test();

  // Clicking the Problems label moves focus to the list, so the shell stops
  // swallowing keys.
  REQUIRE(e.bottom_panel_mouse_for_test(problems_x, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE_FALSE(e.terminal_focused_for_test());
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);

  // ...and the Terminal label brings the shell back.
  REQUIRE(e.bottom_panel_mouse_for_test(terminal_x, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);
}

TEST_CASE("Bottom panel: one bar row serves both views", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);

  const int bar_y = e.bottom_panel_view_tab_y_for_test();
  const int view_tabs_w = e.bottom_panel_view_tabs_width_for_test();

  // Content starts right under the bar in both views, and the panel spends
  // frame + bar + content now: the shell's old strip row is a content row.
  REQUIRE(e.bottom_panel_content_y_for_test() == bar_y + 1);
  REQUIRE(e.bottom_panel_content_h_for_test() == e.terminal_panel_h_for_test() - 2);
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.bottom_panel_content_y_for_test() == bar_y + 1);
  REQUIRE(e.bottom_panel_content_h_for_test() == e.terminal_panel_h_for_test() - 2);
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);

  // The bar's first cell is the Problems label: a press there switches the view
  // instead of being inert chrome.
  REQUIRE(e.bottom_panel_mouse_for_test(1, bar_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);
  REQUIRE(e.bottom_panel_mouse_for_test(
      e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_TERMINAL), bar_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);

  // Past the labels and before the action cluster the row is inert chrome: the
  // panel declines it and the shell consumes it without focusing anything, so a
  // stray click up there can never activate the shell.
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(view_tabs_w + 1, bar_y, true));
  REQUIRE(e.terminal_mouse_for_test(view_tabs_w + 1, bar_y, true, false, false));
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);
}

TEST_CASE("Bottom panel: the session list carries one row per shell", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  UI *ui = e.ui_for_test();
  const Theme &th = e.theme_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  const int list_w = e.bottom_panel_shell_list_w_for_test();
  const int list_x = e.bottom_panel_shell_list_x_for_test();
  const int content_y = e.bottom_panel_content_y_for_test();

  // The column is sized from the names ("term 1", 6 cells) with a 12-cell
  // floor, and it hugs the panel's right edge, leaving the panel's own last cell
  // free.
  REQUIRE(list_w == 12);
  REQUIRE(list_x == panel_w - 1 - list_w);
  REQUIRE(list_x > 0);

  // The shell beside it gives up exactly the column and its divider gutter.
  REQUIRE(e.docked_terminal_view_w_for_test() == panel_w - 2 - (list_w + 1));
  REQUIRE(e.docked_terminal_view_h_for_test() == e.bottom_panel_content_h_for_test());

  // Each row carries the shell glyph and the name, on the panel background, and
  // the gutter in front of the column marks it off from the shell.
  const std::string first = e.integrated_terminal_session_label_for_test(0);
  REQUIRE(first == " \uE795 term 1");
  REQUIRE(ui_cell_count(first) == 9);

  const UICell *pad = ui->cell_at(list_x, content_y);
  REQUIRE(pad != nullptr);
  REQUIRE(pad->ch == " ");
  const UICell *icon = ui->cell_at(list_x + 1, content_y);
  REQUIRE(icon != nullptr);
  REQUIRE(icon->ch == "\uE795");
  const UICell *name = ui->cell_at(list_x + 3, content_y);
  REQUIRE(name != nullptr);
  REQUIRE(name->ch == "t");
  const UICell *second_icon = ui->cell_at(list_x + 1, content_y + 1);
  REQUIRE(second_icon != nullptr);
  REQUIRE(second_icon->ch == "\uE795");
  const UICell *divider = ui->cell_at(list_x - 1, content_y);
  REQUIRE(divider != nullptr);
  REQUIRE(divider->ch == "\u2502");
  REQUIRE(divider->fg == th.fg_terminal_tab_separator);
  REQUIRE(second_icon->bg == th.bg_terminal);

  // The list is rows, not tabs: its own hit-test maps a cell to a session, and
  // a press on a row focuses that shell.
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x, content_y) == 0);
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x, content_y + 1) == 1);
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x - 1, content_y) == -1);
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x, content_y - 1) == -1);
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x + list_w, content_y) == -1);

  REQUIRE(e.terminal_mouse_for_test(list_x + 1, content_y + 1, true, false, false));
  REQUIRE(e.current_integrated_terminal_for_test() == 1);
  REQUIRE(e.terminal_focused_for_test());
  REQUIRE(e.terminal_mouse_for_test(list_x + 1, content_y, true, false, false));
  REQUIRE(e.current_integrated_terminal_for_test() == 0);

  // The focused shell wears the theme's chip; the merely-listed one stays on the
  // panel background.
  e.request_redraw_for_test();
  e.render_for_test();
  const UICell *focused = ui->cell_at(list_x + 1, content_y);
  REQUIRE(focused != nullptr);
  REQUIRE(focused->bg == th.bg_terminal_tab_focused);
  REQUIRE(focused->fg == th.fg_terminal_tab_focused);
  const UICell *idle = ui->cell_at(list_x + 1, content_y + 1);
  REQUIRE(idle != nullptr);
  REQUIRE(idle->bg == th.bg_terminal);
  REQUIRE(idle->fg == th.fg_terminal_tab_inactive);

  // The list is the terminal view's: with the Problems list showing, the shell
  // keeps the whole row.
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.bottom_panel_shell_list_w_for_test() == 0);
  REQUIRE(e.bottom_panel_shell_list_at_for_test(list_x, content_y) == -1);
  REQUIRE(e.docked_terminal_view_w_for_test() == panel_w - 2);
}

TEST_CASE("Bottom panel: a long custom name is elided to the column's share", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  const std::string full_name = "an-extremely-long-custom-terminal-name";
  e.add_terminal_for_test(full_name);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  const int panel_w = e.terminal_panel_w_for_test();
  const int list_w = e.bottom_panel_shell_list_w_for_test();
  const int list_x = e.bottom_panel_shell_list_x_for_test();
  UI *ui = e.ui_for_test();

  // The column never takes more than a third of the panel, however long the
  // name is.
  REQUIRE(list_w == panel_w / 3);

  // The name itself is untouched -- it is the identity the task runner, the
  // lazygit reuse check and the Lua API match on -- and only the row the list
  // draws is shortened.
  REQUIRE(e.integrated_terminal_display_name_for_test(0) == full_name);

  // What is drawn fills the column: the pad, the glyph, the space, then the
  // name elided with an ellipsis.
  const std::string label = e.integrated_terminal_session_label_for_test(0);
  REQUIRE(ui_cell_count(label) == list_w - 1);
  REQUIRE(label.substr(0, 5) == " \uE795 ");
  REQUIRE(label.find("\u2026") != std::string::npos);
  REQUIRE(label.find(full_name) == std::string::npos);
  // The head of the name survives, so the row is still recognisable.
  REQUIRE(label.find(full_name.substr(0, 5)) != std::string::npos);

  // ...and the sessions after it are not squeezed out: the second row (a
  // generated "term 2") still sits under the first.
  const UICell *second_icon = ui->cell_at(list_x + 1, e.bottom_panel_content_y_for_test() + 1);
  REQUIRE(second_icon != nullptr);
  REQUIRE(second_icon->ch == "\uE795");
}

TEST_CASE("Bottom panel: a custom name that fits is drawn in full", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test("lazygit");
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  // A name inside its share is drawn exactly as set: no ellipsis, no padding out
  // to the budget.
  const std::string label = e.integrated_terminal_session_label_for_test(0);
  REQUIRE(label == " \uE795 lazygit");

  const int list_x = e.bottom_panel_shell_list_x_for_test();
  const int row_y = e.bottom_panel_content_y_for_test();
  UI *ui = e.ui_for_test();
  const UICell *first = ui->cell_at(list_x + 3, row_y);
  REQUIRE(first != nullptr);
  REQUIRE(first->ch == "l");
  const UICell *last = ui->cell_at(list_x + 3 + 6, row_y);
  REQUIRE(last != nullptr);
  REQUIRE(last->ch == "t");
}

TEST_CASE("Bottom panel: the bar's action cluster draws and hits its actions", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  UI *ui = e.ui_for_test();
  const Theme &th = e.theme_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  REQUIRE(panel_w >= 40);

  // The "+" (new shell), the zoom toggle and the close "x", right-aligned with a
  // two-cell gap. The colours are the ones the old strip used for the same
  // actions.
  const UICell *plus = ui->cell_at(panel_w - 8, e.bottom_panel_view_tab_y_for_test());
  REQUIRE(plus != nullptr);
  REQUIRE(plus->ch == "+");
  REQUIRE(plus->fg == th.fg_terminal_tab_plus);
  const UICell *zoom = ui->cell_at(panel_w - 5, e.bottom_panel_view_tab_y_for_test());
  REQUIRE(zoom != nullptr);
  REQUIRE(zoom->ch == "\uF065");
  REQUIRE(zoom->fg == th.fg_terminal_tab_inactive);
  const UICell *close = ui->cell_at(panel_w - 2, e.bottom_panel_view_tab_y_for_test());
  REQUIRE(close != nullptr);
  REQUIRE(close->ch == "\uF00D");
  REQUIRE(close->fg == th.fg_terminal_tab_close);

  // The gaps between the actions are not actions: the panel declines them, the
  // shell consumes them inertly.
  REQUIRE_FALSE(
      e.bottom_panel_mouse_for_test(panel_w - 6, e.bottom_panel_view_tab_y_for_test(), true));
  REQUIRE(e.terminal_mouse_for_test(
      panel_w - 6, e.bottom_panel_view_tab_y_for_test(), true, false, false));
  REQUIRE(e.terminal_visible_for_test());
  REQUIRE_FALSE(e.terminal_zoom_active_for_test());

  // Pressing the toggle maximizes the panel, and the glyph flips to the compress
  // double arrow.
  REQUIRE(e.bottom_panel_mouse_for_test(panel_w - 5, e.bottom_panel_view_tab_y_for_test(), true));
  REQUIRE(e.terminal_zoom_active_for_test());
  e.request_redraw_for_test();
  e.render_for_test();
  const UICell *compress = ui->cell_at(panel_w - 5, e.bottom_panel_view_tab_y_for_test());
  REQUIRE(compress != nullptr);
  REQUIRE(compress->ch == "\uF066");
  REQUIRE(e.bottom_panel_mouse_for_test(panel_w - 5, e.bottom_panel_view_tab_y_for_test(), true));
  REQUIRE_FALSE(e.terminal_zoom_active_for_test());

  // ...and the "x" closes the panel, exactly like Ctrl+J with it up.
  REQUIRE(e.bottom_panel_mouse_for_test(panel_w - 2, e.bottom_panel_view_tab_y_for_test(), true));
  REQUIRE_FALSE(e.terminal_visible_for_test());
}

TEST_CASE("Bottom panel: the action cluster sheds actions on a narrow panel", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);

  // A panel too narrow for the shell beside a session column folds the column
  // away rather than squeezing the shell to nothing.
  e.ui_for_test()->resize(30, 24);
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.terminal_panel_w_for_test() == 30);
  REQUIRE(e.bottom_panel_shell_list_w_for_test() == 0);
  REQUIRE(e.docked_terminal_view_w_for_test() == 30 - 2);

  // The actions shed from the inside out as the width shrinks: the cluster stops
  // where the labels it must clear are.
  const int bar_y = e.bottom_panel_view_tab_y_for_test();
  UI *ui = e.ui_for_test();
  REQUIRE(ui->cell_at(30 - 8, bar_y)->ch == "+");
  REQUIRE(ui->cell_at(30 - 5, bar_y)->ch == "\uF065");
  REQUIRE(ui->cell_at(30 - 2, bar_y)->ch == "\uF00D");

  e.ui_for_test()->resize(23, 24);
  e.request_redraw_for_test();
  e.render_for_test();
  // The labels never give way, so the cluster sheds from the inside out: both
  // the new-shell and the zoom action are gone, their cells now carrying the
  // TERMINAL label instead of an action.
  for (int x = 1; x < 23; x++)
  {
    REQUIRE(ui->cell_at(x, bar_y)->ch != "+");
    REQUIRE(ui->cell_at(x, bar_y)->ch != "\uF065");
  }
  // What is left between the last label and the close is inert, not an action.
  REQUIRE(ui->cell_at(20, bar_y)->ch == " ");
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(20, bar_y, true));
  REQUIRE(e.terminal_visible_for_test());
  // Only the close still fits, and pressing it closes the panel.
  REQUIRE(ui->cell_at(23 - 2, bar_y)->ch == "\uF00D");
  REQUIRE(e.bottom_panel_mouse_for_test(23 - 2, bar_y, true));
  REQUIRE_FALSE(e.terminal_visible_for_test());
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
  const int problems_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_PROBLEMS);
  const int terminal_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_TERMINAL);

  // The dispatcher passes is_click=false for both a motion and a release, so
  // this is what sweeping the pointer across the labels used to do: switch the
  // panel under the cursor, and hand focus to the list while the shell was still
  // being used.
  REQUIRE(e.bottom_panel_mouse_for_test(problems_x, tab_y, false));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);

  // It is still a tab: a press switches it.
  REQUIRE(e.bottom_panel_mouse_for_test(problems_x, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);

  // ...and the same holds on the way back, so a hover cannot bounce the view
  // back and forth either.
  REQUIRE(e.bottom_panel_mouse_for_test(terminal_x, tab_y, false));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.bottom_panel_mouse_for_test(terminal_x, tab_y, true));
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_TERMINAL);
}

TEST_CASE("Bottom panel: the view labels are flat, with the active one underlined", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test();
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  UI *ui = e.ui_for_test();
  const Theme &th = e.theme_for_test();
  const int bar_y = e.bottom_panel_view_tab_y_for_test();
  const int problems_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_PROBLEMS);
  const int terminal_x = e.bottom_panel_view_tab_x_for_test(BOTTOM_PANEL_TERMINAL);

  // Neither label wears a chip: both sit on the panel's own background, and the
  // inactive one is the muted colour with no underline.
  const UICell *inactive = ui->cell_at(problems_x, bar_y);
  REQUIRE(inactive != nullptr);
  REQUIRE(inactive->ch == "P");
  REQUIRE(inactive->fg == th.fg_terminal_tab_inactive);
  REQUIRE(inactive->bg == th.bg_terminal);
  REQUIRE(inactive->underline == 0);

  // The showing view reads by its brighter text and the accent underline.
  const UICell *active = ui->cell_at(terminal_x, bar_y);
  REQUIRE(active != nullptr);
  REQUIRE(active->ch == "T");
  REQUIRE(active->fg == th.fg_terminal_tab_active);
  REQUIRE(active->bg == th.bg_terminal);
  REQUIRE(active->underline == 1);
  REQUIRE(active->underline_fg == th.fg_terminal_tab_focused);

  // The gap between the labels is the panel's, not a label's: no underline, no
  // chip, no text.
  const UICell *gap = ui->cell_at(problems_x + 8, bar_y);
  REQUIRE(gap != nullptr);
  REQUIRE(gap->ch == " ");
  REQUIRE(gap->bg == th.bg_terminal);
  REQUIRE(gap->underline == 0);
}

TEST_CASE("Bottom panel: one rule along its top, none at its bottom", "[jot]")
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

  // Exactly one rule above the panel, and it is the panel's own first row: no
  // corner glyphs, since the panel is not a box of its own. The row being the
  // panel's is what keeps the pane area's last row a code row (see
  // pane_edges.h); the rule doubles as the resize handle.
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, panel_y - 1);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch == "─");
  }
  const UICell *pane_last = ui->cell_at(1, panel_y - 2);
  REQUIRE(pane_last != nullptr);
  REQUIRE(pane_last->ch != "─");

  // The panel's first row carries the tab bar, so nothing draws a second rule
  // (or a corner) directly under the first: it leads with the Problems label.
  const UICell *first_col = ui->cell_at(0, panel_y);
  REQUIRE(first_col != nullptr);
  REQUIRE(first_col->ch != "┌");
  REQUIRE(first_col->ch != "─");
  const UICell *pad = ui->cell_at(1, panel_y);
  REQUIRE(pad != nullptr);
  REQUIRE(pad->ch == "P");
  const UICell *second = ui->cell_at(2, panel_y);
  REQUIRE(second != nullptr);
  REQUIRE(second->ch == "R");

  // The shell's content starts on the row under the bar: no second strip, no
  // rule.
  const UICell *content = ui->cell_at(1, panel_y + 1);
  REQUIRE(content != nullptr);
  REQUIRE(content->ch != "─");
  REQUIRE(content->ch != "┌");

  // The panel does not end in a rule: its last row is content, marked off from
  // the status line by its own background (see pane_edges.h). The rule it does
  // own sits above its first row.
  const UICell *bottom_right = ui->cell_at(panel_w - 1, panel_y + panel_h - 1);
  REQUIRE(bottom_right != nullptr);
  REQUIRE(bottom_right->ch != "─");
  const UICell *top_rule = ui->cell_at(panel_w - 1, panel_y - 1);
  REQUIRE(top_rule != nullptr);
  REQUIRE(top_rule->ch == "─");

  // With the explorer up, the rule still runs unbroken: it is the panel's own
  // row, and the explorer's column ends one row above it, so there is no second
  // region inking a bottom edge on this row and no junction glyph.
  e.toggle_sidebar_for_test();
  REQUIRE(e.sidebar_visible_for_test());
  e.request_redraw_for_test();
  e.render_for_test();
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, panel_y - 1);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch == "─");
  }
  e.toggle_sidebar_for_test();
}

TEST_CASE("Bottom panel: the rule along its top is the dock's own row", "[jot]")
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
  const int rule_y = panel_y - 1;
  const int bottom_row = panel_y + panel_h - 1;
  UI *ui = e.ui_for_test();
  const Theme &theme = e.theme_for_test();

  // The rule is the panel's own first row and spans exactly the panel's columns;
  // the right dock beside it makes the separator run up through the rule row, so
  // the two meet in a corner instead of the rule running into the dock.
  for (int x = 0; x < panel_w - 1; x++)
  {
    const UICell *cell = ui->cell_at(x, rule_y);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch == "─");
  }
  const UICell *corner = ui->cell_at(panel_w - 1, rule_y);
  REQUIRE(corner != nullptr);
  REQUIRE(corner->ch == "┐");
  REQUIRE(corner->bg == theme.bg_terminal);
  const UICell *dock_row = ui->cell_at(panel_w, rule_y);
  REQUIRE(dock_row != nullptr);
  REQUIRE(dock_row->ch != "─");

  // The panel's own rows start under the rule, and the dock is a region beside
  // them, so the panel inks its right side all the way down.
  const UICell *side = ui->cell_at(panel_w - 1, panel_y);
  REQUIRE(side != nullptr);
  REQUIRE(side->ch == "│");
  const UICell *bottom_side = ui->cell_at(panel_w - 1, bottom_row);
  REQUIRE(bottom_side != nullptr);
  REQUIRE(bottom_side->ch == "│");

  // The panel does not end in a rule: its last row is content, marked off from
  // the status line by its own background.
  const UICell *bottom_inside = ui->cell_at(1, bottom_row);
  REQUIRE(bottom_inside != nullptr);
  REQUIRE(bottom_inside->ch != "─");
  const UICell *panel_body = ui->cell_at(1, panel_y + 5);
  REQUIRE(panel_body != nullptr);
  REQUIRE(bottom_inside->bg == panel_body->bg);
  REQUIRE(bottom_inside->bg != theme.bg_status);

  // The row above the rule is the pane area's last one, and it is a code row:
  // the pane's own background, no rule, no status colour.
  const UICell *pane_last = ui->cell_at(1, rule_y - 1);
  REQUIRE(pane_last != nullptr);
  REQUIRE(pane_last->ch != "─");
  REQUIRE(pane_last->bg != theme.bg_status);
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

TEST_CASE("Bottom panel: the list leads with a per-file header", "[jot]")
{
  seed_config_home();
  // The C++ definition checks supply the findings: they live in their own store
  // (opening a file does not clear them the way a server's slice is dropped when
  // the file has no client), and they are workspace-wide, which is what the
  // header tallies. Real files, so a click's cursor lands on the finding's line.
  char dir[] = "/tmp/jot_bp_header_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const std::string root = dir;
  {
    std::ofstream out(root + "/widget.hpp");
    out << "#pragma once\nvoid reset();\nint missing();\n";
  }
  {
    std::ofstream out(root + "/widget.cpp");
    out << "#include \"widget.hpp\"\nvoid reset() {}\nint twice() { return 1; }\n";
  }
  {
    std::ofstream out(root + "/other.cpp");
    out << "int twice() { return 2; }\n";
  }
  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root, false);
  e.run_cpp_definitions_scan_for_test();
  e.show_problems_panel_for_test();
  e.request_redraw_for_test();
  e.render_for_test();

  auto row_text = [&](int y)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  };

  // The header names the totals, the severities and the per-file tally.
  const int header_y = e.bottom_panel_content_y_for_test();
  const std::string header = row_text(header_y);
  REQUIRE(header.find("Problems") != std::string::npos);
  REQUIRE(header.find("2 findings in 2 files") != std::string::npos);
  REQUIRE(header.find("1 error, 1 warning") != std::string::npos);
  REQUIRE(header.find("widget.cpp 1") != std::string::npos);
  // The rows follow, one per finding, in the list's order (the error first).
  REQUIRE(row_text(header_y + 1).find("defined more than once") != std::string::npos);
  REQUIRE(row_text(header_y + 2).find("No definition found") != std::string::npos);

  // The header is text, not a finding: a press on it opens nothing, while the
  // rows below it open the file at the finding (the accept path names the file
  // and the line in the message).
  REQUIRE(e.bottom_panel_mouse_for_test(2, header_y + 2, true));
  REQUIRE(e.message_for_test() == "widget.hpp:3");
  REQUIRE(e.bottom_panel_mouse_for_test(2, header_y, true));
  REQUIRE(e.message_for_test() == "widget.hpp:3");
  REQUIRE(e.bottom_panel_mouse_for_test(2, header_y + 1, true));
  REQUIRE(e.message_for_test() == "widget.cpp:3");
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

TEST_CASE("Bottom panel: the list answers only over its own rows", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_hit", "/tmp/bp_hit_a.cpp", 3, 1, "first");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_hit", "/tmp/bp_hit_b.cpp", 5, 2, "second");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_hit", "/tmp/bp_hit_c.cpp", 9, 3, "third");
  e.show_problems_panel_for_test();
  REQUIRE(e.bottom_panel_view_for_test() == (int)BOTTOM_PANEL_PROBLEMS);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);

  // A code cell: inside the pane, on its first text row (below the workspace
  // strip, which is outside the pane) and above the panel.
  const SplitPane &pane = e.pane_for_test();
  const int code_x = pane.x + 3;
  const int code_y = pane_content_top(pane);
  REQUIRE(code_y < e.bottom_panel_view_tab_y_for_test());

  // The list is a dock, not a mode: a press or a motion in the buffer is not its
  // to claim, so the event reaches the editor instead of dying on the panel --
  // and the keys stay with the buffer, which is what made it uneditable while
  // the list was up.
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(code_x, code_y, true));
  REQUIRE_FALSE(e.bottom_panel_mouse_for_test(code_x, code_y, false));

  // The wheel follows the same split: over the buffer it belongs to the code,
  // not the list...
  REQUIRE_FALSE(e.problems_scroll_input_for_test(code_x, code_y, false, true));
  REQUIRE(e.problems_scroll_for_test() == 0);
  // ...and over the list's own rows it still scrolls the list.
  const int content_y = e.bottom_panel_content_y_for_test();
  REQUIRE(e.problems_scroll_input_for_test(2, content_y, false, true));
  REQUIRE(e.problems_scroll_for_test() == 3);
  REQUIRE(e.problems_scroll_input_for_test(2, content_y, true, false));
  REQUIRE(e.problems_scroll_for_test() == 0);

  // The whole path: a press in the buffer lands and brings the focus with it.
  // The key dispatch hands keys to the panel only while it holds focus
  // (input/modes/dispatch.cpp), so this is what makes the buffer typeable.
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);
  e.mouse_event_for_test(code_x, code_y, 1);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);
}

TEST_CASE("Bottom panel: a press on a list row still opens the file", "[jot]")
{
  seed_config_home();
  char dir[] = "/tmp/jot_bp_rows_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const std::string path = std::string(dir) + "/only.cpp";
  {
    std::ofstream out(path);
    out << "int answer = 42;\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_rows", path, 1, 1, "only one");
  e.show_problems_panel_for_test();

  // The list's own rows are still the list's: the first finding under the header
  // row opens the file behind it, through the same accept path the picker uses.
  REQUIRE(e.bottom_panel_mouse_for_test(2, e.bottom_panel_content_y_for_test() + 1, true));
  REQUIRE(e.buffer_for_test().filepath == path);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);
}

TEST_CASE("Bottom panel: the Problems list keeps its text on the panel background", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_bg", "/tmp/bp_bg_a.cpp", 3, 1, "first error");
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/bp_bg", "/tmp/bp_bg_b.cpp", 5, 2, "a warning");
  REQUIRE(e.workspace_diagnostics_for_test().size() == 2);
  e.show_problems_panel_for_test();
  e.request_redraw_for_test();
  e.render_for_test();

  UI *ui = e.ui_for_test();
  const Theme &th = e.theme_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  // The findings start under the header row, which the tally owns.
  const int first_row = e.bottom_panel_content_y_for_test() + 1;

  // Otherwise the check below could pass on a theme whose selection color is the
  // panel's own background.
  REQUIRE(th.bg_selection != th.bg_terminal);

  // The selected row is marked by an accent sliver in a column of its own, the
  // way the pane tabs mark the active one.
  const int selected_row = e.problems_selected_for_test();
  REQUIRE(selected_row == 0);
  const UICell *marker = ui->cell_at(1, first_row + selected_row);
  REQUIRE(marker != nullptr);
  REQUIRE(marker->ch == "▌");
  REQUIRE(marker->fg == th.fg_active_border);
  const UICell *dot = ui->cell_at(2, first_row + selected_row);
  REQUIRE(dot != nullptr);
  REQUIRE(dot->ch == "●");

  // No row paints the selection color behind its text: every cell of every
  // listed row stays on the panel's own background, so the severity-colored
  // message and the secondary-colored location are never fighting a fill.
  const int rows = e.bottom_panel_content_h_for_test() >= 2 ? 2 : 1;
  for (int row = 0; row < rows; row++)
  {
    for (int x = 0; x < panel_w; x++)
    {
      const UICell *cell = ui->cell_at(x, first_row + row);
      REQUIRE(cell != nullptr);
      REQUIRE(cell->bg != th.bg_selection);
      REQUIRE(cell->bg == th.bg_terminal);
    }
  }

  // Moving the selection down moves the sliver with it and leaves the row it
  // came from filled with nothing but the panel background.
  REQUIRE(e.bottom_panel_key_for_test('j'));
  REQUIRE(e.problems_selected_for_test() == 1);
  e.request_redraw_for_test();
  e.render_for_test();
  const UICell *moved = ui->cell_at(1, first_row + 1);
  REQUIRE(moved != nullptr);
  REQUIRE(moved->ch == "▌");
  const UICell *vacated = ui->cell_at(1, first_row);
  REQUIRE(vacated != nullptr);
  REQUIRE(vacated->ch == " ");
}
