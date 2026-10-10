// Floating terminal (Alt+Shift+T, :termfloat): the centered box geometry, the
// toggle and Escape paths, the grid it paints over the buffer, and the mouse
// selection. Headless: add_floating_terminal_for_test carries a live vterm with
// no process behind it, the way the docked panel's tests do.
#include "core/editor.h"
#include "core/panes.h" // pane_content_top, pane_viewport_h
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_termfloat_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A file long enough to sit behind the centered box on every row it covers,
  // and wide enough that the text reaches inside the frame's columns.
  void load_filler(Editor &e)
  {
    std::string text;
    for (int i = 0; i < 60; i++)
    {
      text += "int filler_" + std::to_string(i) + " = " + std::to_string(i)
              + ";  // filler line, padded out to reach under the floating box\n";
    }
    const std::string path = "/tmp/jot_termfloat_source.cpp";
    {
      // Closed before the load: an ofstream still open has the bytes in its own
      // buffer, and the editor would read an empty file.
      std::ofstream out(path);
      out << text;
    }
    e.load_file(path);
  }

  std::string screen_row(Editor &e, int y)
  {
    std::string row;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      row += e.ui_for_test()->cell_at(x, y)->ch;
    }
    return row;
  }
} // namespace

TEST_CASE("The floating terminal box centers inside the pane's text rows", "[jot]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(120, 40);
  e.set_terminal_state_for_test(false, false, 10);
  e.add_floating_terminal_for_test();

  const SplitPane &pane = e.pane_for_test();
  const int content_top = pane_content_top(pane);
  const int content_h = pane_viewport_h(pane);
  TerminalBox box = e.floating_terminal_rect_for_test();
  // The share the settings ask for, of the pane's own text rows.
  REQUIRE(box.w == pane.w * 85 / 100);
  REQUIRE(box.h == content_h * 75 / 100);
  // Centered in those rows and inside them: the pane chrome above the text (the
  // breadcrumb winbar) is painted after this overlay, so the frame has to stay
  // below it or the breadcrumb erases the border.
  REQUIRE(box.y >= content_top);
  REQUIRE(box.y + box.h <= content_top + content_h);
  REQUIRE(box.x >= pane.x);
  REQUIRE(box.x + box.w <= pane.x + pane.w);
  REQUIRE(std::abs((box.x - pane.x) - (pane.x + pane.w - (box.x + box.w))) <= 1);
  REQUIRE(std::abs((box.y - content_top) - (content_top + content_h - (box.y + box.h))) <= 1);

  // A screen smaller than the box: it gives way to the pane rather than
  // covering the docks or the status line, and never inverts.
  e.apply_resize_for_test(30, 12);
  box = e.floating_terminal_rect_for_test();
  const SplitPane &small = e.pane_for_test();
  const int small_top = pane_content_top(small);
  const int small_h = pane_viewport_h(small);
  REQUIRE(box.w > 0);
  REQUIRE(box.h > 0);
  REQUIRE(box.w <= small.w);
  REQUIRE(box.h <= small_h);
  REQUIRE(box.x >= small.x);
  REQUIRE(box.y >= small_top);
  REQUIRE(box.x + box.w <= small.x + small.w);
  REQUIRE(box.y + box.h <= small_top + small_h);
}

TEST_CASE("Hiding the floating terminal keeps its shell", "[jot]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(120, 40);
  e.add_floating_terminal_for_test();
  REQUIRE(e.floating_terminal_visible_for_test());
  REQUIRE(e.floating_terminal_alive_for_test());

  // Escape is the box's own key: the overlay goes, the process stays.
  e.floating_terminal_key_for_test(27);
  REQUIRE_FALSE(e.floating_terminal_visible_for_test());
  REQUIRE(e.floating_terminal_alive_for_test());

  // :termfloat and its long alias drive the same toggle, and the chord's path
  // through the editor reports the same state.
  e.run_ex_for_test("termfloat");
  REQUIRE(e.floating_terminal_visible_for_test());
  e.run_ex_for_test("terminalfloat");
  REQUIRE_FALSE(e.floating_terminal_visible_for_test());
  e.toggle_floating_terminal_for_test();
  REQUIRE(e.floating_terminal_visible_for_test());
  e.toggle_floating_terminal_for_test();
  REQUIRE_FALSE(e.floating_terminal_visible_for_test());
  REQUIRE(e.floating_terminal_alive_for_test());
}

TEST_CASE("The floating box paints over the buffer", "[jot]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(120, 40);
  e.set_terminal_state_for_test(false, false, 10);
  load_filler(e);
  // The probe editor starts on the home screen: with it up the frame paints the
  // home path and the pane's own cells are never drawn.
  e.set_home_menu_visible(false);
  e.render_for_test();

  const TerminalBox box = e.floating_terminal_rect_for_test();
  REQUIRE(e.ui_for_test()->cell_at(box.x, box.y)->ch != "┌");

  // The row above the pane's text (the breadcrumb, when the pane has one) is
  // read before the box opens and compared after: a box that reached into it
  // had its top border erased by the breadcrumb, which is painted after this
  // overlay.
  const SplitPane &pane = e.pane_for_test();
  const int content_top = pane_content_top(pane);
  REQUIRE(box.y >= content_top);
  const std::string bar_before =
      content_top > pane.y ? screen_row(e, content_top - 1) : std::string();
  if (content_top > pane.y)
  {
    REQUIRE(bar_before.find_first_not_of(' ') != std::string::npos);
  }

  // A cell inside the box's area that the buffer's text occupies. Scanned
  // rather than assumed: the filler's columns and the box's edges do not line
  // up, and which of the covered rows carries text is the buffer's business.
  int text_x = -1;
  int text_y = -1;
  for (int y = box.y + 1; y < box.y + box.h - 1 && text_x < 0; y++)
  {
    for (int x = box.x + 1; x < box.x + box.w - 1; x++)
    {
      if (e.ui_for_test()->cell_at(x, y)->ch != " ")
      {
        text_x = x;
        text_y = y;
        break;
      }
    }
  }
  REQUIRE(text_x >= 0);

  e.add_floating_terminal_for_test();
  e.render_for_test();

  // The frame and the title are the box's own...
  REQUIRE(e.ui_for_test()->cell_at(box.x, box.y)->ch == "┌");
  REQUIRE(e.ui_for_test()->cell_at(box.x + box.w - 1, box.y)->ch == "┐");
  REQUIRE(e.ui_for_test()->cell_at(box.x, box.y + box.h - 1)->ch == "└");
  for (int y = box.y + 1; y < box.y + box.h - 1; y++)
  {
    REQUIRE(e.ui_for_test()->cell_at(box.x, y)->ch == "│");
  }
  REQUIRE(screen_row(e, box.y).find("Floating terminal") != std::string::npos);
  // ...the row above the pane's text is the same one it read before, with the
  // frame nowhere near it...
  if (content_top > pane.y)
  {
    REQUIRE(screen_row(e, content_top - 1) == bar_before);
  }
  // ...and the buffer text underneath is gone, not tinted: the box is opaque.
  REQUIRE(e.ui_for_test()->cell_at(text_x, text_y)->ch == " ");
}

TEST_CASE("A click in the floating box selects there, a click away dismisses it", "[jot]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(120, 40);
  e.set_terminal_state_for_test(false, false, 10);
  e.add_floating_terminal_for_test();

  // A dock click records the dock as the selection's owner...
  e.add_terminal_for_test();
  e.set_terminal_state_for_test(true, false, 10);
  e.terminal_mouse_for_test(5, e.bottom_panel_content_y_for_test(), true, false, false);
  REQUIRE(e.terminal_sel_active_for_test());
  REQUIRE_FALSE(e.terminal_sel_in_float_for_test());

  // ...and a click in the box records the box, so the two bands cannot cross.
  const TerminalBox box = e.floating_terminal_rect_for_test();
  const int click_x = box.x + 4;
  const int click_y = box.y + 3;
  REQUIRE(e.floating_terminal_mouse_for_test(click_x, click_y, true, false, false));
  REQUIRE(e.terminal_sel_active_for_test());
  REQUIRE(e.terminal_sel_in_float_for_test());
  REQUIRE(e.terminal_sel_anchor_col_for_test() == click_x - (box.x + 1));
  REQUIRE(e.terminal_sel_dragging_for_test());

  // The drag extends, the release ends it, and the band stays for reading.
  REQUIRE(e.floating_terminal_mouse_for_test(click_x + 5, click_y + 1, false, true, false));
  REQUIRE(e.terminal_sel_cur_col_for_test() == click_x + 5 - (box.x + 1));
  REQUIRE(e.floating_terminal_mouse_for_test(click_x + 5, click_y + 1, false, false, true));
  REQUIRE_FALSE(e.terminal_sel_dragging_for_test());
  REQUIRE(e.terminal_sel_active_for_test());

  // A click outside dismisses the box: the click is consumed (no caret lands
  // under it) and the shell keeps running.
  REQUIRE(e.floating_terminal_mouse_for_test(1, 1, true, false, false));
  REQUIRE_FALSE(e.floating_terminal_visible_for_test());
  REQUIRE(e.floating_terminal_alive_for_test());
  REQUIRE_FALSE(e.terminal_sel_active_for_test());
}
