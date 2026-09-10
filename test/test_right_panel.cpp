// Right dock tab management: panels open as tabs (git, git diff, symbols,
// debug, plugin), the active tab switches the dock content, closing the
// active tab activates a neighbor, and closing the last tab hides the dock.
// Ctrl+Shift+B toggles the dock itself.
#include "editor.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_right_panel_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("Right dock: opening a tab shows the dock and activates it", "[jot]")
{
  Editor &e = probe_editor();
  // Leave the previous test case with a clean dock.
  while (!e.right_panel_tabs_for_test().empty())
  {
    e.close_right_panel_tab(e.right_panel_tabs_for_test().back());
  }

  e.open_right_panel_tab(RIGHT_PANEL_GIT);
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT));
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_GIT);

  // Opening a second tab switches to it without duplicating.
  e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT));
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_SYMBOLS));
  REQUIRE(e.right_panel_tabs_for_test().size() == 2);
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_SYMBOLS);

  // Re-opening the active tab is a no-op (no duplicates).
  e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  REQUIRE(e.right_panel_tabs_for_test().size() == 2);
}

TEST_CASE("Right dock: closing the active tab activates a neighbor", "[jot]")
{
  Editor &e = probe_editor();
  e.open_right_panel_tab(RIGHT_PANEL_GIT);
  e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  e.open_right_panel_tab(RIGHT_PANEL_DEBUG);
  REQUIRE(e.right_panel_tabs_for_test().size() == 3);

  // Close the middle tab while it is active -> neighbor takes over.
  e.close_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  REQUIRE_FALSE(e.right_panel_tab_open(RIGHT_PANEL_SYMBOLS));
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.right_panel_tab_open(e.active_right_panel_tab_for_test()));

  // Closing a non-active tab leaves the active one untouched.
  const auto before = e.active_right_panel_tab_for_test();
  e.close_right_panel_tab(RIGHT_PANEL_GIT);
  REQUIRE(e.active_right_panel_tab_for_test() == before);
}

TEST_CASE("Right dock: closing the last tab hides the dock", "[jot]")
{
  Editor &e = probe_editor();
  while (!e.right_panel_tabs_for_test().empty())
  {
    e.close_right_panel_tab(e.right_panel_tabs_for_test().back());
  }

  e.open_right_panel_tab(RIGHT_PANEL_PLUGIN);
  REQUIRE(e.right_panel_visible());
  e.close_right_panel_tab(RIGHT_PANEL_PLUGIN);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.right_panel_tabs_for_test().empty());
}

TEST_CASE("Right dock: toggle opens with the last active tab", "[jot]")
{
  Editor &e = probe_editor();
  while (!e.right_panel_tabs_for_test().empty())
  {
    e.close_right_panel_tab(e.right_panel_tabs_for_test().back());
  }

  // Dock closed: toggle opens it with the remembered active tab. The
  // remembered tab comes from the last opened tab (closing the dock does
  // not erase the tab list).
  e.open_right_panel_tab(RIGHT_PANEL_GIT_DIFF);
  e.toggle_right_panel();
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT_DIFF));

  e.toggle_right_panel();
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_GIT_DIFF);

  // Dock open: toggle closes it again.
  e.toggle_right_panel();
  REQUIRE_FALSE(e.right_panel_visible());
}

TEST_CASE("Right dock: tabs and active tab persist across sessions", "[jot]")
{
  // Isolated config home + workspace root; each case gets its own root so
  // the session files never collide.
  char home[] = "/tmp/jot_right_panel_session_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);
  const std::string root = std::string(home) + "/ws";
  fs::create_directories(root);

  // Save: dock closed (Ctrl+Shift+B) but three tabs open, git diff active
  // (the last opened tab).
  {
    Editor e;
    e.enable_workspace_session_for_test(root);
    e.open_right_panel_tab(RIGHT_PANEL_GIT);
    e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
    e.open_right_panel_tab(RIGHT_PANEL_GIT_DIFF);
    e.toggle_right_panel();
    REQUIRE_FALSE(e.right_panel_visible());
    e.save_workspace_session_for_test();
  }

  // Restore in a fresh editor: same tabs, same order, same active tab, and
  // the dock stays closed until toggled.
  {
    Editor e2;
    e2.enable_workspace_session_for_test(root);
    (void)e2.restore_workspace_session_for_test();
    REQUIRE(e2.right_panel_tabs_for_test().size() == 3);
    REQUIRE(e2.right_panel_tab_open(RIGHT_PANEL_GIT));
    REQUIRE(e2.right_panel_tab_open(RIGHT_PANEL_SYMBOLS));
    REQUIRE(e2.right_panel_tab_open(RIGHT_PANEL_GIT_DIFF));
    REQUIRE(e2.active_right_panel_tab_for_test() == RIGHT_PANEL_GIT_DIFF);
    REQUIRE_FALSE(e2.right_panel_visible());

    e2.toggle_right_panel();
    REQUIRE(e2.right_panel_visible());
    REQUIRE(e2.active_right_panel_tab_for_test() == RIGHT_PANEL_GIT_DIFF);
  }
}

TEST_CASE("Right dock: open dock state restores open", "[jot]")
{
  char home[] = "/tmp/jot_right_panel_session2_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);
  const std::string root = std::string(home) + "/ws";
  fs::create_directories(root);

  {
    Editor e;
    e.enable_workspace_session_for_test(root);
    e.open_right_panel_tab(RIGHT_PANEL_DEBUG);
    REQUIRE(e.right_panel_visible());
    e.save_workspace_session_for_test();
  }

  {
    Editor e2;
    e2.enable_workspace_session_for_test(root);
    (void)e2.restore_workspace_session_for_test();
    REQUIRE(e2.right_panel_visible());
    REQUIRE(e2.active_right_panel_tab_for_test() == RIGHT_PANEL_DEBUG);
  }
}