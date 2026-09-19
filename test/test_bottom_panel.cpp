// The bottom panel is one dock that hosts two views: the shell and the
// diagnostics list. They share its frame, its height and its view tabs, so
// these cases pin the parts that are easy to get wrong when a terminal-only
// panel grows a second tenant: the height reservation, the tab-strip
// hit-testing (the view tabs get their own row, the shell's strip the next
// one), and the focus handoff between the two.
#include "editor.h"
#include "ui/text.h"
#include "ui/ui.h"
#include <algorithm>
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
  const int terminal_tab_w = ui_cell_count(Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_TERMINAL));
  const int problems_tab_w = ui_cell_count(Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_PROBLEMS));
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

TEST_CASE("Bottom panel: the shell's tabs carry the shell glyph", "[jot]")
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
  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();
  const std::string first = e.integrated_terminal_tab_label_for_test(0);

  // The shell tab has the same shape as the view tabs one row up: a pad, the
  // glyph, a space, then the name.
  REQUIRE(first == " \uE795 term 1 ");
  const UICell *pad = ui->cell_at(1, tab_y);
  REQUIRE(pad != nullptr);
  REQUIRE(pad->ch == " ");
  const UICell *icon = ui->cell_at(2, tab_y);
  REQUIRE(icon != nullptr);
  REQUIRE(icon->ch == "\uE795");
  const UICell *name = ui->cell_at(4, tab_y);
  REQUIRE(name != nullptr);
  REQUIRE(name->ch == "t");

  // The label is 10 cells but 12 bytes: the glyph costs one cell, not three.
  // The strip lays the tabs out by that cell count, so the second tab starts
  // exactly where the first one's close marker ends, and the close marker of
  // the first sits one cell past its label -- the same offset the click
  // handler computes.
  const int second_x = 1 + ui_cell_count(first) + 2;
  REQUIRE(ui_cell_count(first) == 10);
  REQUIRE((int)first.size() == 12);
  const UICell *second_icon = ui->cell_at(second_x + 1, tab_y);
  REQUIRE(second_icon != nullptr);
  REQUIRE(second_icon->ch == "\uE795");
  const UICell *close = ui->cell_at(1 + ui_cell_count(first), tab_y);
  REQUIRE(close != nullptr);
  REQUIRE(close->ch == "x");

  // Pressing that close marker closes the first terminal, which is what pins
  // the glyph-inclusive width: a strip that still counted the label's bytes
  // would put the marker three cells to the right of this one, and the click
  // would land on the second tab instead.
  REQUIRE(e.terminal_mouse_for_test(1 + ui_cell_count(first), tab_y, true, false, false));
  REQUIRE(e.integrated_terminal_tab_label_for_test(0) == " \uE795 term 1 ");
  REQUIRE(e.integrated_terminal_tab_label_for_test(1).empty());
}

TEST_CASE("Bottom panel: a long custom tab name is elided to the strip's share", "[jot]")
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

  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();
  const int panel_w = e.terminal_panel_w_for_test();
  UI *ui = e.ui_for_test();

  // The name itself is untouched -- it is the identity the task runner, the
  // lazygit reuse check and the Lua API match on -- and only the label the
  // strip draws is shortened.
  REQUIRE(e.integrated_terminal_name_for_test(0) == full_name);

  // What is drawn is capped at the tab's share of the panel (a quarter, with a
  // 12-cell floor) and marks the cut with an ellipsis rather than a dot pair.
  const std::string label = e.integrated_terminal_tab_label_for_test(0);
  REQUIRE(ui_cell_count(label) == std::max(12, panel_w / 4));
  REQUIRE(label.substr(0, 5) == " \uE795 ");
  REQUIRE(label.find("\u2026") != std::string::npos);
  REQUIRE(label.find(full_name) == std::string::npos);
  // The head of the name survives, so the tab is still recognisable.
  REQUIRE(label.find(full_name.substr(0, 5)) != std::string::npos);

  // ...and the tabs after it are not squeezed out: the second tab (a generated
  // "term 2") and the "+" both still sit on the row.
  const std::string second = e.integrated_terminal_tab_label_for_test(1);
  const int second_x = 1 + ui_cell_count(label) + 2;
  const UICell *second_icon = ui->cell_at(second_x + 1, tab_y);
  REQUIRE(second_icon != nullptr);
  REQUIRE(second_icon->ch == "\uE795");
  const int plus_x = second_x + ui_cell_count(second) + 2;
  const UICell *plus = ui->cell_at(plus_x + 1, tab_y);
  REQUIRE(plus != nullptr);
  REQUIRE(plus->ch == "+");

  // Hit-testing walks the elided label too: the ellipsis moved the close marker
  // left, and pressing it closes this tab rather than landing on a neighbour.
  REQUIRE(e.terminal_mouse_for_test(1 + ui_cell_count(label), tab_y, true, false, false));
  REQUIRE(e.integrated_terminal_tab_label_for_test(0) == " \uE795 term 1 ");
  REQUIRE(ui_cell_count(e.integrated_terminal_tab_label_for_test(0)) == 10);
}

TEST_CASE("Bottom panel: a custom tab name that fits is drawn in full", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.add_terminal_for_test("lazygit");
  e.set_bottom_panel_view_for_test(BOTTOM_PANEL_TERMINAL);
  e.set_terminal_state_for_test(true, false, 10);
  e.request_redraw_for_test();
  e.render_for_test();

  // A name inside its share is drawn exactly as set: no ellipsis, no padding
  // out to the budget.
  const std::string label = e.integrated_terminal_tab_label_for_test(0);
  REQUIRE(label == " \uE795 lazygit ");

  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();
  UI *ui = e.ui_for_test();
  const UICell *first = ui->cell_at(4, tab_y);
  REQUIRE(first != nullptr);
  REQUIRE(first->ch == "l");
  const UICell *last = ui->cell_at(10, tab_y);
  REQUIRE(last != nullptr);
  REQUIRE(last->ch == "t");
  const UICell *close = ui->cell_at(1 + ui_cell_count(label), tab_y);
  REQUIRE(close != nullptr);
  REQUIRE(close->ch == "x");
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
  const int problems_x =
      1 + ui_cell_count(Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_TERMINAL));

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

  // The panel's first row carries the view tabs, so nothing draws a second
  // rule (or a corner) directly under the first.
  const UICell *first_col = ui->cell_at(0, panel_y);
  REQUIRE(first_col != nullptr);
  REQUIRE(first_col->ch != "┌");
  REQUIRE(first_col->ch != "─");
  // The label keeps the one-cell pad the strip used before the glyphs, then the
  // view's icon, then the name: column 1 is blank, column 2 is the terminal
  // icon, and the "T" of " Terminal " is two cells past it.
  const UICell *pad = ui->cell_at(1, panel_y);
  REQUIRE(pad != nullptr);
  REQUIRE(pad->ch == " ");
  const UICell *icon = ui->cell_at(2, panel_y);
  REQUIRE(icon != nullptr);
  REQUIRE(icon->ch == "\uF120");
  const UICell *label = ui->cell_at(4, panel_y);
  REQUIRE(label != nullptr);
  REQUIRE(label->ch == "T");

  // The fullscreen toggle glyph is gone from the shell's tab strip; zooming is
  // keyboard-only (:termzoom, Alt+Shift+Z).
  const int tab_y = e.bottom_panel_terminal_tab_y_for_test();
  for (int x = 0; x < panel_w; x++)
  {
    const UICell *cell = ui->cell_at(x, tab_y);
    REQUIRE(cell != nullptr);
    REQUIRE(cell->ch != "□");
  }

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

  // The rule is the panel's own first row and spans exactly the panel's
  // columns; the right dock beside it makes the separator run up through the
  // rule row, so the two meet in a corner instead of the rule running into the
  // dock.
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

TEST_CASE("Bottom panel: the view tabs lead with an icon", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);

  const std::string terminal = Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_TERMINAL);
  const std::string problems = Editor::bottom_panel_view_label_for_test(BOTTOM_PANEL_PROBLEMS);

  // A Nerd Fonts glyph, then the view's name: the classic FontAwesome terminal
  // for the shell, and the warning triangle the status line already uses for
  // diagnostics.
  REQUIRE(terminal == std::string(" \uF120 Terminal "));
  REQUIRE(problems == std::string(" \uF071 Problems "));

  // The strip is measured in cells -- what the renderer lays the labels out by
  // -- and not in bytes: each glyph is three bytes, so a byte count would
  // report the strip two cells wider than it is drawn and put the second
  // label's hit-test past its text.
  REQUIRE(e.bottom_panel_view_tabs_width_for_test() == 25);
  REQUIRE(e.bottom_panel_view_tabs_width_for_test()
          == ui_cell_count(terminal) + ui_cell_count(problems) + 1);
  REQUIRE(e.bottom_panel_view_tabs_width_for_test()
          < (int)(terminal.size() + problems.size()) + 1);
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
  const int first_row = e.bottom_panel_content_y_for_test();

  // Otherwise the check below could pass on a theme whose selection color is
  // the panel's own background.
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
