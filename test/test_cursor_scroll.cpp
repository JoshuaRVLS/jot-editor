// The caret must not be dragged around by scrolling.
//
// compute_code_cursor_screen_pos used to park the caret on the last row of the
// pane whenever its line scrolled out of view, and both render paths kept
// drawing it there -- so scrolling away from the caret made it travel to the
// bottom edge and ride it, which read as the caret being dragged along. The
// caret is now hidden when it has no cell on screen, and the GUI glide places it
// (instead of easing it on a second curve) while its pane is sliding.
#include "editor.h"
#include "ui/ui.h"

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
      char cfgdir[] = "/tmp/jot_cursor_scroll_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void load_long_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_cursor_scroll_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    for (int i = 0; i < 400; i++)
    {
      out << "int value_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
  }

  void make_wide(Editor &e)
  {
    // A grid big enough that the caret has cells to occupy.
    e.apply_resize_for_test(120, 40);
  }
} // namespace

TEST_CASE("A caret scrolled out of view is hidden, not parked", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  make_wide(e);

  // Caret on line 100, viewport parked far below it: the caret has no cell on
  // screen at all.
  e.scroll_cursor_to_for_test(100, 0);
  e.buffer_for_test().scroll_offset = 300;
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.ui_for_test()->cursor_is_hidden());

  // Caret brought back into view: shown again, in the pane's code area.
  e.scroll_cursor_to_for_test(305, 0);
  e.buffer_for_test().scroll_offset = 300;
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE_FALSE(e.ui_for_test()->cursor_is_hidden());
}

TEST_CASE("Scrolling the viewport away from the caret never reveals it", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  make_wide(e);

  e.scroll_cursor_to_for_test(10, 0);
  e.buffer_for_test().scroll_offset = 0;
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE_FALSE(e.ui_for_test()->cursor_is_hidden());

  // Scrolling down, exactly as the wheel handler does: only scroll_offset moves.
  // The caret must stay out of the way rather than being pulled along.
  const int cursor_line_before = e.buffer_for_test().cursor.y;
  for (int step = 0; step < 60; step++)
  {
    e.buffer_for_test().scroll_offset += 5;
    // The renderer clamps the offset to the buffer and re-derives from there.
    e.request_redraw_for_test();
    e.render_for_test();
  }
  REQUIRE(e.buffer_for_test().cursor.y == cursor_line_before);
  REQUIRE(e.ui_for_test()->cursor_is_hidden());

  // And scrolling back to the caret shows it again without having moved it.
  e.buffer_for_test().scroll_offset = 0;
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.buffer_for_test().cursor.y == cursor_line_before);
  REQUIRE_FALSE(e.ui_for_test()->cursor_is_hidden());
}
