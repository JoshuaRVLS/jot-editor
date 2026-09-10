// Mouse motion must never scroll the viewport.
//
// Editor::handle_mouse ends with a caret reveal (ensure_cursor_visible) that is
// meant for presses/drags/releases, where the pointer actually places the caret.
// It ran for every event that reached the tail, so plain mouse motion scrolled
// the viewport back to the caret whenever the user had scrolled away from it.
// Ordinary motion only escaped that by accident: the LSP-hover block above
// returns early on motion, and both of its branches are gated on "Ctrl not held"
// -- so with Ctrl held (the Ctrl+hover goto-definition affordance) execution fell
// through and the view "teleported" to the cursor on every motion cell, in both
// the terminal and the GUI.
#include "editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_mouse_hover_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A file long enough that the caret can sit far outside the viewport.
  std::string load_long_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_mouse_hover_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    for (int i = 0; i < 400; i++)
    {
      out << "int value_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
    return path;
  }

  // Caret at `caret_line` but the viewport parked `rows_away` lines above it, so
  // the caret is off-screen and any caret reveal would visibly move the view.
  void scroll_away_from_caret(Editor &e, int caret_line, int top_line)
  {
    e.scroll_cursor_to_for_test(caret_line, 0);
    e.buffer_for_test().scroll_offset = top_line;
    e.render_for_test();
  }

  // A point inside the pane's code area (past the 9-cell gutter, below the tab
  // strip), on the pane the editor is actually drawing.
  int code_x(Editor &e)
  {
    return e.pane_for_test().x + 20;
  }
  int code_y_upper(Editor &e)
  {
    return e.pane_for_test().y + 4;
  }
} // namespace

TEST_CASE("Ctrl + mouse motion never scrolls the viewport", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  // Caret at line 200, viewport showing lines 150..176: the caret is off-screen.
  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int scroll_before = e.buffer_for_test().scroll_offset;
  const int caret_line_before = e.buffer_for_test().cursor.y;
  REQUIRE(scroll_before == 150);
  REQUIRE(caret_line_before == 200);

  // Ctrl+motion: the goto-definition underline follows the pointer, and the view
  // stays exactly where the user left it.
  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/32, /*ctrl=*/true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
  REQUIRE(e.buffer_for_test().cursor.y == caret_line_before);
  // The affordance itself still works (this is what Ctrl+motion is for).
  REQUIRE(e.ctrl_hover_active_for_test());

  // A second motion cell must not drift either (the bug re-armed on every cell).
  e.mouse_event_for_test(code_x(e), code_y_upper(e) + 1, /*bstate=*/32, /*ctrl=*/true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
}

TEST_CASE("Plain mouse motion never scrolls the viewport", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int scroll_before = e.buffer_for_test().scroll_offset;
  REQUIRE(scroll_before == 150);

  // No Ctrl: this path is the one that used to escape the reveal by accident
  // (the LSP-hover early returns). Pin it so it stays true on purpose.
  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/32, /*ctrl=*/false);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
  REQUIRE_FALSE(e.ctrl_hover_active_for_test());
}

TEST_CASE("A press still reveals the caret", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int mouse_line =
      e.buffer_for_test().scroll_offset + (code_y_upper(e) - e.pane_for_test().y - 1);
  REQUIRE(mouse_line < 200); // the click lands well above the old caret

  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/1); // press
  e.render_for_test();

  // The pointer placed the caret on the clicked row, and that is the behaviour
  // the caret reveal exists for -- the fix must not disable it.
  REQUIRE(e.buffer_for_test().cursor.y == mouse_line);
}
