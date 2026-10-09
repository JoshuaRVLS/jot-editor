// The pane row budget: the chrome rows a pane spends above its text.
//
// A pane pays for its header out of its own height (the local tab strip today,
// the breadcrumb winbar once it exists), and every consumer of "where does the
// text start" or "how many rows does it have" has to agree -- the renderer, the
// caret placement, the mouse hit-tests, folds, smooth scroll and the viewport
// API the Lua kit reads. They used to each do `pane.h - tab_height` on their
// own; these cases pin the single rule (SplitPane::header_height, resolved by
// Editor::update_pane_layout) against the rows actually painted, so a change
// that only updates some of the call sites fails here instead of on screen.
#include "editor.h"
#include "editor/model/panes.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_pane_budget_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A three-line file on disk: the buffer's own insertion path is for typing,
  // and a line break typed into it stays inside the line.
  std::string write_three_line_file()
  {
    const std::string path = "/tmp/jot_pane_budget_probe.txt";
    std::ofstream out(path);
    out << "ALPHA\nBETA\nGAMMA\n";
    out.close();
    return path;
  }

  // The row `needle` is painted on, or -1. Reads the whole grid, so the caller
  // must name text that cannot also be a file name on the strip (see the
  // callers' ALPHA/BETA/GAMMA words).
  int row_of_text(Editor &e, const std::string &needle)
  {
    UI *ui = e.ui_for_test();
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      std::string row;
      for (int x = 0; x < e.ui_width_for_test(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        row += cell ? cell->ch : " ";
      }
      if (row.find(needle) != std::string::npos)
      {
        return y;
      }
    }
    return -1;
  }
} // namespace

TEST_CASE("A pane's text starts below its header", "[jot][panes]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.load_file(write_three_line_file());
  e.render_for_test();

  const SplitPane &pane = e.pane_for_test();
  REQUIRE(pane.header_height == e.tab_height_for_test());
  REQUIRE(pane_viewport_h(pane) == pane.h - pane.header_height);
  REQUIRE(pane_content_top(pane) == pane.y + pane.header_height);

  // The file's first line is painted on the header's first content row, not on
  // the header row itself, and the lines follow one row apart.
  const int alpha = row_of_text(e, "ALPHA");
  const int beta = row_of_text(e, "BETA");
  const int gamma = row_of_text(e, "GAMMA");
  REQUIRE(alpha == pane_content_top(pane));
  REQUIRE(beta == alpha + 1);
  REQUIRE(gamma == alpha + 2);
}

TEST_CASE("The header is charged to every pane, whatever the layout", "[jot][panes]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.load_file(write_three_line_file());
  e.render_for_test();
  const int single_h = pane_viewport_h(e.pane_for_test());

  // Same pane area, split in two: both panes pay their own header, and the
  // rows they keep for text come out of the pane area, not out of each other's
  // header.
  e.split_pane_for_test(true);
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 2);
  for (size_t i = 0; i < e.panes_for_test().size(); i++)
  {
    const SplitPane &pane = e.panes_for_test()[i];
    // The header a split pane pays is its own strip row -- the workspace strip
    // yields row 0 to the groups then, so every one of them draws a strip,
    // single-tab group included. The winbar is not in play: a .txt buffer never
    // takes a breadcrumb row.
    REQUIRE(pane.header_height == (e.pane_shows_tab_strip_for_test((int)i) ? 1 : 0));
    REQUIRE(pane_viewport_h(pane) == pane.h - pane.header_height);
    REQUIRE(pane_content_top(pane) == pane.y + pane.header_height);
  }

  // Zoom parks the inactive panes off-screen; their budget still comes out of
  // their (1-row) box without going negative.
  e.toggle_pane_zoom_for_test();
  e.render_for_test();
  for (const SplitPane &pane : e.panes_for_test())
  {
    REQUIRE(pane_viewport_h(pane) >= 0);
  }

  // And the single-pane viewport comes back unchanged once the split is gone.
  e.close_current_pane_for_test();
  e.render_for_test();
  REQUIRE(e.panes_for_test().size() == 1);
  REQUIRE(pane_viewport_h(e.pane_for_test()) >= single_h - 1);
}
