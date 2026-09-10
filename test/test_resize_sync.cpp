// Grid resize plumbing: the UI's one-shot full repaint, pane re-fitting, and
// the sidebar's width-driven auto-hide.
//
// The terminal and GUI resize handlers both funnel through Editor::apply_resize
// (jot/app/resize.cpp), so exercising it here covers what a live window resize
// does without needing a real terminal or display.
#include "editor.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_resize_cfg_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  std::string scratch_workspace()
  {
    static std::string root;
    if (root.empty())
    {
      char tmpl[] = "/tmp/jot_resize_ws_XXXXXX";
      REQUIRE(mkdtemp(tmpl) != nullptr);
      root = tmpl;
      fs::create_directory(root + "/src");
      std::ofstream(root + "/src/main.cpp") << "int main() { return 0; }\n";
      std::ofstream(root + "/README.md") << "# hi\n";
    }
    return root;
  }
} // namespace

TEST_CASE("A resize schedules exactly one full repaint", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  // A real size change must force a full paint: newly exposed cells are
  // default-constructed in both the live and the retained grid, so the cell
  // diff would skip them and leave whatever the terminal restored there.
  ui->resize(ui->get_width() + 7, ui->get_height() + 3);
  REQUIRE(ui->full_repaint_pending());
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE_FALSE(ui->full_repaint_pending()); // consumed by the frame

  // Re-applying the same size (the editor does this at startup) needs no full
  // paint.
  ui->resize(ui->get_width(), ui->get_height());
  REQUIRE_FALSE(ui->full_repaint_pending());
}

TEST_CASE("A resize re-fits the panes to the new grid", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  e.apply_resize_for_test(120, 40);
  const SplitPane wide = e.pane_for_test();
  REQUIRE(wide.w > 0);
  REQUIRE(wide.x + wide.w <= ui->get_render_width());

  // Growing the window must grow the pane area, not leave it at the old size.
  e.apply_resize_for_test(160, 50);
  const SplitPane wider = e.pane_for_test();
  REQUIRE(wider.w > wide.w);
  REQUIRE(wider.x + wider.w <= ui->get_render_width());

  // And shrinking must bring it back in bounds rather than overflow.
  e.apply_resize_for_test(60, 20);
  const SplitPane narrow = e.pane_for_test();
  REQUIRE(narrow.w > 0);
  REQUIRE(narrow.x + narrow.w <= ui->get_render_width());
}

TEST_CASE("The sidebar comes back when the window fits again", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  // A workspace shows the explorer.
  e.host().io.open_workspace(scratch_workspace());
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE(e.sidebar_visible_for_test());

  // Too narrow: the explorer is dropped so the code keeps a usable width.
  e.apply_resize_for_test(20, 20);
  e.render_for_test();
  REQUIRE_FALSE(e.sidebar_visible_for_test());
  REQUIRE(e.sidebar_auto_hidden_for_test());

  // Room again: it returns. Shrinking once must not hide it for the session.
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE(e.sidebar_visible_for_test());
  REQUIRE_FALSE(e.sidebar_auto_hidden_for_test());

  // An explicit toggle still wins: hiding the sidebar by hand and then
  // resizing must not resurrect it.
  e.toggle_sidebar_for_test(); // hide
  REQUIRE_FALSE(e.sidebar_visible_for_test());
  e.apply_resize_for_test(20, 20);
  e.render_for_test();
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE_FALSE(e.sidebar_visible_for_test());

  // Restore for the rest of the suite.
  e.toggle_sidebar_for_test();
  e.render_for_test();
}
