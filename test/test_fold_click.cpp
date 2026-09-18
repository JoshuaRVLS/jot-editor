// The gutter must not fold anything: its fold marker column is gone (a folded
// header says "… N lines" on the row instead), so the cells the marker used to
// occupy are the line number's first digit now. The click hit-test outlived the
// icon, which made a left click on the number silently fold the block under it
// -- a fold with no visible target.
//
// Folding itself is untouched: the context menu's Toggle Fold, the fold
// commands and the keyboard paths all still call toggle_fold_at_line.
#include "editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_fold_click_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // Two nested blocks, so several gutter rows sit on a foldable header.
  std::string load_foldable_file(Editor &e)
  {
    const std::string path = "/tmp/jot_fold_click_" + std::to_string(::getpid()) + ".cpp";
    std::ofstream out(path);
    out << "int main() {\n"
           "  if (ok) {\n"
           "    return 1;\n"
           "  }\n"
           "}\n";
    out.close();
    e.load_file(path);
    return path;
  }
} // namespace

TEST_CASE("Clicking the gutter's old fold column does not fold", "[jot]")
{
  Editor &e = probe_editor();
  load_foldable_file(e);
  e.apply_resize_for_test(100, 30);
  e.render_for_test();

  REQUIRE_FALSE(e.buffer_for_test().fold_ranges.empty());

  // The line number's first digit -- the fold marker's old cell -- on the rows
  // the foldable headers occupy. The first content row sits under the tab
  // strip, one below the pane's top.
  const int x = e.pane_for_test().x + 2;
  const int first_row = e.pane_for_test().y + 1;
  for (int y = first_row; y < first_row + 5; y++)
  {
    e.mouse_event_for_test(x, y, /*bstate=*/1); // left press
    e.render_for_test();
  }

  for (const auto &range : e.buffer_for_test().fold_ranges)
  {
    REQUIRE_FALSE(range.collapsed);
  }
}
