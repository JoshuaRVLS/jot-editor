// Zen focus mode: sidebar, right dock, status line and tab strip off, panes
// across the full width, and the whole thing persisted as `zen_mode` so a start
// lands in the layout the editor was left in.
#include "core/editor.h"
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

  void leave_zen(Editor &e)
  {
    while (e.zen_active())
    {
      e.toggle_zen_mode();
    }
  }

  void show_sidebar(Editor &e)
  {
    while (!e.host().render.layout().sidebar_visible)
    {
      e.host().io.toggle_sidebar();
    }
  }
} // namespace

TEST_CASE("Zen hides chrome on enter and restores it on exit", "[jot]")
{
  Editor &e = probe_editor();
  leave_zen(e);
  e.apply_resize_for_test(120, 40);
  e.set_home_menu_visible(false);
  show_sidebar(e);
  e.host().io.show_plugin_panel("zen_test_panel");
  e.create_new_buffer_for_test(); // a real tab: the placeholder is not one
  e.render_for_test();           // the strip's order is reconciled by a frame
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.status_line_height() == 1);
  REQUIRE(e.topbar_height_for_test() == 1); // the workspace strip owns row 0
  const bool was_sidebar = e.host().render.layout().sidebar_visible;
  const bool was_panel = e.right_panel_visible();

  REQUIRE(e.toggle_zen_mode());
  REQUIRE(e.zen_active());
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.status_line_height() == 0);
  REQUIRE(e.topbar_height_for_test() == 0);

  // Leaving zen restores the pre-zen layout exactly.
  REQUIRE_FALSE(e.toggle_zen_mode());
  REQUIRE_FALSE(e.zen_active());
  REQUIRE(e.host().render.layout().sidebar_visible == was_sidebar);
  REQUIRE(e.right_panel_visible() == was_panel);
  REQUIRE(e.status_line_height() == 1);
  REQUIRE(e.topbar_height_for_test() == 1);
}

TEST_CASE("Zen spans the full width", "[jot]")
{
  Editor &e = probe_editor();
  leave_zen(e);
  e.apply_resize_for_test(120, 40);
  show_sidebar(e);
  const int docked_w = e.pane_for_test().w;
  REQUIRE(e.pane_for_test().x > 0);
  REQUIRE(docked_w < e.host().render.layout().width);

  e.set_zen_mode(true);
  REQUIRE(e.pane_for_test().x == 0);
  REQUIRE(e.pane_for_test().w == e.host().render.layout().width);

  e.set_zen_mode(false);
  REQUIRE(e.pane_for_test().w == docked_w);
}

TEST_CASE("Zen mode is remembered across starts", "[jot]")
{
  Editor &e = probe_editor();
  leave_zen(e);

  e.toggle_zen_mode();
  REQUIRE(e.zen_active());
  REQUIRE(e.config_bool_for_test("zen_mode"));
  e.toggle_zen_mode();
  REQUIRE_FALSE(e.config_bool_for_test("zen_mode"));

  // Starting up (and :reload) read the key back through apply_config_live.
  e.set_config_for_test("zen_mode", "true");
  e.apply_config_live_for_test();
  REQUIRE(e.zen_active());
  REQUIRE(e.status_line_height() == 0);
  REQUIRE(e.topbar_height_for_test() == 0);

  e.set_config_for_test("zen_mode", "false");
  e.apply_config_live_for_test();
  REQUIRE_FALSE(e.zen_active());
  REQUIRE(e.status_line_height() == 1);
}

TEST_CASE("The chrome keys do not climb out of zen", "[jot]")
{
  Editor &e = probe_editor();
  leave_zen(e);
  e.set_home_menu_visible(false);
  show_sidebar(e);
  e.create_new_buffer_for_test();
  e.render_for_test();
  REQUIRE(e.topbar_height_for_test() == 1);
  e.set_zen_mode(true);
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE(e.topbar_height_for_test() == 0);

  e.toggle_sidebar(); // Ctrl+B / Alt+B
  REQUIRE(e.zen_active());
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE(e.message_for_test().find("Zen mode") != std::string::npos);

  e.toggle_right_panel(); // Ctrl+Shift+B
  REQUIRE(e.zen_active());
  REQUIRE_FALSE(e.right_panel_visible());

  // F12 is the way out, and it gives back the layout zen was entered from.
  e.toggle_zen_mode();
  REQUIRE_FALSE(e.zen_active());
  REQUIRE(e.host().render.layout().sidebar_visible);
}

TEST_CASE("Opening a workspace does not bring the chrome back", "[jot]")
{
  Editor &e = probe_editor();
  leave_zen(e);
  e.set_home_menu_visible(false);
  e.set_zen_mode(true);

  char dir[] = "/tmp/jot_zen_workspace_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  e.open_workspace(dir, /*restore_session=*/false);

  REQUIRE(e.zen_active());
  REQUIRE_FALSE(e.host().render.layout().sidebar_visible);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.status_line_height() == 0);
  REQUIRE(e.topbar_height_for_test() == 0);

  e.set_zen_mode(false);
  REQUIRE(e.status_line_height() == 1);
}
