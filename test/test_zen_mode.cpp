// Zen focus mode: toggle hides sidebar / right panel and suppresses the
// status line (reclaiming its rows), then restores the exact pre-zen layout;
// the pane area narrows to zen_content_width and centers while active.
#include "editor.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_zen_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("Zen mode hides chrome on enter and restores it on exit", "[jot]")
{
  Editor &e = probe_editor();
  // Deterministic setup through the public host APIs: make the sidebar and
  // right panel visible, then remember what zen must restore.
  while (!e.host().render.layout().sidebar_visible)
  {
    e.host().io.toggle_sidebar();
  }
  e.host().io.show_plugin_panel("zen_test_panel");
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.status_line_height() == 2);
  const bool was_sidebar = e.host().render.layout().sidebar_visible;
  const bool was_panel = e.right_panel_visible();

  REQUIRE(e.toggle_zen_mode() == true);
  REQUIRE(e.zen_active());
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.status_line_height() == 0);

  // Leaving zen restores the pre-zen layout exactly.
  REQUIRE(e.toggle_zen_mode() == false);
  REQUIRE_FALSE(e.zen_active());
  REQUIRE(e.host().render.layout().sidebar_visible == was_sidebar);
  REQUIRE(e.right_panel_visible() == was_panel);
  REQUIRE(e.status_line_height() == 2);
}

TEST_CASE("Zen mode with no chrome still restores cleanly", "[jot]")
{
  Editor &e = probe_editor();
  while (e.host().render.layout().sidebar_visible)
  {
    e.host().io.toggle_sidebar();
  }
  const bool was_panel = e.right_panel_visible();

  e.toggle_zen_mode();
  REQUIRE(e.status_line_height() == 0);
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  e.toggle_zen_mode();
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE(e.right_panel_visible() == was_panel);
  REQUIRE(e.status_line_height() == 2);
}

TEST_CASE("Zen content margin centers the pane area", "[jot]")
{
  Editor &e = probe_editor();
  while (e.zen_active())
  {
    e.toggle_zen_mode();
  }
  REQUIRE(e.zen_content_margin(140) == 0); // zen off: no margin

  e.toggle_zen_mode();
  // Default zen_content_width is 100 columns.
  REQUIRE(e.zen_content_margin(140) == 20); // (140 - 100) / 2 each side
  REQUIRE(e.zen_content_margin(100) == 0);  // exactly the target width
  REQUIRE(e.zen_content_margin(60) == 0);   // narrower: no centering
  REQUIRE(e.zen_content_margin(0) == 0);
  REQUIRE(e.zen_content_margin(102) == 1);
  e.toggle_zen_mode();
  REQUIRE_FALSE(e.zen_active());
}