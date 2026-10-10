// Right dock tab management: panels open as tabs (git, git diff, symbols,
// debug, plugin), the active tab switches the dock content, closing the
// active tab activates a neighbor, and closing the last tab hides the dock.
// The dock is the secondary sidebar: Ctrl+Alt+B toggles it (Ctrl+Shift+B is a
// legacy alias), Ctrl+B the primary sidebar and Ctrl+J the bottom panel.
#include "core/editor.h"
#include "events/keybind_catalog.h"
#include "ui/text.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// The Plugin tab is one shared host for every Lua panel, so it is labelled
// with the name of the panel it is showing -- the strip said "Plugin" while
// the LeetCode panel was up -- and falls back to the generic name only when
// no panel is shown.
TEST_CASE("Right dock: the plugin tab is named after the panel it shows", "[jot]")
{
  char home[] = "/tmp/jot_right_panel_plugin_name_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  // No panel up: the tab opens generic, so the fallback is the first thing
  // pinned rather than what is left after a close.
  Editor e;
  e.open_right_panel_tab(RIGHT_PANEL_PLUGIN);
  std::vector<std::string> labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels.size() == 1);
  REQUIRE(labels[0].find("Plugin") != std::string::npos);

  // Showing a panel renames the tab after it.
  e.host().io.show_plugin_panel("LeetCode");
  labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels.size() == 1);
  REQUIRE(labels[0].find("LeetCode") != std::string::npos);
  REQUIRE(labels[0].find("Plugin") == std::string::npos);

  // The name follows the panel, not the LeetCode feature specifically.
  e.host().io.show_plugin_panel("probe_panel");
  labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels[0].find("probe_panel") != std::string::npos);

  // Leaving the tab for another one keeps its name: the strip says what the
  // tab will bring back when it is activated again.
  e.open_right_panel_tab(RIGHT_PANEL_GIT);
  labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels.size() == 2);
  REQUIRE(labels[0].find("probe_panel") != std::string::npos);
  REQUIRE(labels[1].find("Git") != std::string::npos);
  REQUIRE(labels[1].find("probe_panel") == std::string::npos);
}

// The strip paints in terminal cells but its close zone used to be counted in
// bytes: a label carries a Nerd Font glyph and a two-byte ×, so every tab
// measured a couple of cells wider than it drew. Clicking the drawn × landed
// on the tab body (the tab re-opened) while a click past the tab's last glyph
// landed in the byte shadow and closed it.
TEST_CASE("Right dock: a tab's × closes where the strip paints it", "[jot]")
{
  char home[] = "/tmp/jot_right_panel_close_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 32);
  e.open_right_panel_tab(RIGHT_PANEL_GIT);

  // A file open puts the workspace strip up. The dock is full height: the strip
  // does not push it down a row, so its top border stays on row 0 and its tab
  // strip stays on row 1 (side_column_top() + 1). Reading the row off
  // topbar_height() used to be right only while no strip was shown.
  char ws[] = "/tmp/jot_right_panel_strip_XXXXXX";
  REQUIRE(mkdtemp(ws) != nullptr);
  std::ofstream(std::string(ws) + "/probe.txt") << "probe\n";
  e.host().io.open_workspace(ws);
  // The explorer stacks its header on row 0, the pinned root on row 1 and the
  // tree from row 2, so the file is the third row of the column.
  e.mouse_event_for_test(5, 2, /*bstate=*/1); // click the file in the tree
  REQUIRE(e.topbar_height_for_test() == 1);   // the strip is up
  REQUIRE(e.side_column_top_for_test() == 0);

  const int panel_x = e.ui_width_for_test() - e.right_panel_width_for_test();
  const int strip_y = e.side_column_top_for_test() + 1;
  std::vector<std::string> labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels.size() == 1);
  const int tab_x = panel_x + 1;
  const int tab_w = ui_cell_count(labels[0]);

  // The first cell past the painted tab is not the close button: on the byte
  // walk it used to be, so this click closed a tab nobody aimed at.
  e.mouse_event_for_test(tab_x + tab_w, strip_y, /*bstate=*/1);
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT));
  REQUIRE(e.right_panel_visible());

  // The drawn × is: its cell is the suffix's middle one (the label ends
  // with " × ", three cells).
  e.mouse_event_for_test(tab_x + tab_w - 2, strip_y, /*bstate=*/1);
  REQUIRE_FALSE(e.right_panel_tab_open(RIGHT_PANEL_GIT));
  REQUIRE_FALSE(e.right_panel_visible()); // the last tab closed hides the dock

  // A click on a tab's body switches the dock to it without closing it, and
  // the × then closes the tab it belongs to, not a neighbor.
  e.open_right_panel_tab(RIGHT_PANEL_GIT);
  e.open_right_panel_tab(RIGHT_PANEL_GIT_DIFF);
  labels = e.right_panel_tab_labels_for_test();
  REQUIRE(labels.size() == 2);
  const int git_x = panel_x + 1;
  const int diff_w = ui_cell_count(labels[1]);
  e.mouse_event_for_test(git_x + 3, strip_y, /*bstate=*/1);
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_GIT);
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT_DIFF));
  // The active suffix moved to the tab that just took the focus, so the close
  // cell is recomputed from the labels as they are painted now.
  labels = e.right_panel_tab_labels_for_test();
  const int git_w_now = ui_cell_count(labels[0]);
  REQUIRE(ui_cell_count(labels[1]) == diff_w - 2); // Diff lost its ×
  e.mouse_event_for_test(git_x + git_w_now - 2, strip_y, /*bstate=*/1);
  REQUIRE_FALSE(e.right_panel_tab_open(RIGHT_PANEL_GIT));
  REQUIRE(e.right_panel_tab_open(RIGHT_PANEL_GIT_DIFF));
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
// The two sidebar toggles share a letter and are told apart by Shift, so the
// whole path has to keep the case straight: a terminal that reports Ctrl+B via
// the kitty keyboard protocol sends "CSI 98;5u", and decoding that into an
// uppercase 'B' makes the dispatcher read it as Ctrl+Shift+B. That is exactly
// what happened -- Ctrl+B opened the right dock in the terminal while the GUI
// (which only uppercases for a real Shift) opened the left explorer.
TEST_CASE("Ctrl+B opens the left explorer, Ctrl+Shift+B the right dock", "[jot]")
{
  Editor &e = probe_editor();
  using jot::keybind_detail::decode_csi_u_key;

  // Exactly the bytes a kitty-protocol terminal sends. The decoder's output is a
  // raw key code, so the same two steps the backend performs are reproduced.
  // 98 = 'b', 115 = 's'; modifier 5 = Ctrl, 6 = Ctrl+Shift (bitmask + 1).
  const int ctrl_b = decode_csi_u_key("\x1b[98;5u");
  const int ctrl_shift_b = decode_csi_u_key("\x1b[98;6u");
  REQUIRE(ctrl_b >= 0);
  REQUIRE(ctrl_shift_b >= 0);
  // The unshifted chord must not carry the shift bit, in any spelling.
  REQUIRE((ctrl_b & 0x80000) == 0);
  REQUIRE((ctrl_shift_b & 0x80000) != 0);

  // Ctrl+B: the left explorer toggles and the dock is left alone.
  const bool sidebar_before = e.sidebar_visible_for_test();
  const bool dock_before = e.right_panel_visible();
  e.raw_key_for_test(ctrl_b);
  REQUIRE(e.sidebar_visible_for_test() != sidebar_before);
  REQUIRE(e.right_panel_visible() == dock_before);

  // Ctrl+Shift+B: the other way round.
  const bool sidebar_now = e.sidebar_visible_for_test();
  const bool dock_now = e.right_panel_visible();
  e.raw_key_for_test(ctrl_shift_b);
  REQUIRE(e.sidebar_visible_for_test() == sidebar_now);
  REQUIRE(e.right_panel_visible() != dock_now);

  // Restore the starting state for the other cases in this file.
  if (e.sidebar_visible_for_test() != sidebar_before)
  {
    e.toggle_sidebar_for_test();
  }
  if (e.right_panel_visible() != dock_before)
  {
    e.toggle_right_panel();
  }
}

// The dock's keys belong to the dock, not to the editor sitting next to it.
// `jot.ui.panel` opens the plugin tab without moving the focus there
// (show_plugin_panel), so the caret keeps typing while the LeetCode panel is
// up -- and with the panel merely visible, q and Esc were read as its close
// keys: the panel vanished mid-word and the keystroke was swallowed.
TEST_CASE("Right dock: Esc and q stay with the focused editor", "[jot]")
{
  char home[] = "/tmp/jot_right_panel_keys_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  Editor e;
  e.set_home_menu_visible(false);
  const std::string path = std::string(home) + "/typing.txt";
  {
    std::ofstream out(path);
    out << "alpha\nbeta\n";
  }
  e.load_file(path);
  e.apply_resize_for_test(120, 32);

  // The real flow: the LeetCode panel is shown by the feature itself, and
  // showing a tab does not focus it -- the caret stays in the buffer.
  e.host().io.show_plugin_panel("LeetCode");
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_PLUGIN);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);

  // Typing q types: it is not the dock's close key while the buffer has focus.
  e.scroll_cursor_to_for_test(0, 0);
  e.raw_key_for_test('q');
  REQUIRE(e.buffer_for_test().line(0) == "qalpha");
  REQUIRE(e.right_panel_visible());

  // Esc types nothing and closes nothing, so a stuck panel cannot eat the
  // first key of the next thing typed.
  e.raw_key_for_test(27);
  REQUIRE(e.buffer_for_test().line(0) == "qalpha");
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.active_right_panel_tab_for_test() == RIGHT_PANEL_PLUGIN);

  // Once the dock has the focus its own keys are back: a click on the dock
  // body focuses it (hover only highlights), and Esc closes the tab.
  e.mouse_event_for_test(e.ui_width_for_test() - 1, 5, /*bstate=*/1);
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_RIGHT_PANEL);
  e.raw_key_for_test(27);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE_FALSE(e.right_panel_tab_open(RIGHT_PANEL_PLUGIN));
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_EDITOR);
}

// The dock's separator is the pane's last column, drawn on the dock's left. A
// press on that line must grab the dock and a drag from it must resize the dock;
// the grab used to sit one column right of the line, so a press there fell into
// the pane and the border could be neither clicked nor dragged.
TEST_CASE("Right dock: the drawn separator grabs and drags the resize", "[jot]")
{
  char home[] = "/tmp/jot_dock_resize_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 32);
  e.host().io.show_plugin_panel("LeetCode");
  REQUIRE(e.right_panel_visible());

  const int dock_x = e.ui_width_for_test() - e.right_panel_width_for_test();
  const int start_w = e.right_panel_width_for_test();
  REQUIRE(e.right_panel_resize_hit_for_test(dock_x - 1, 5)); // the drawn line
  REQUIRE(e.right_panel_resize_hit_for_test(dock_x, 5));     // the dock's own column
  REQUIRE_FALSE(e.right_panel_resize_hit_for_test(dock_x - 2, 5));

  // Press on the line, drag six cells to the left: the dock grows by six.
  e.mouse_event_for_test(dock_x - 1, 5, /*bstate=*/1);
  e.mouse_event_for_test(dock_x - 7, 5, /*bstate=*/32);
  e.mouse_event_for_test(dock_x - 7, 5, /*bstate=*/2);
  REQUIRE(e.right_panel_width_for_test() == start_w + 6);
}

// The three dock chords VS Code uses: Ctrl+B for the primary sidebar,
// Ctrl+Alt+B for the secondary sidebar and Ctrl+J for the bottom panel. The
// primary/secondary pair share the letter B, so Alt has to be read from the
// modifier bits rather than inferred from the key case -- Ctrl+Alt+B arrives
// as lowercase 'b' plus Alt.
TEST_CASE("Dock chords: Ctrl+Alt+B secondary sidebar, Ctrl+J bottom panel", "[jot]")
{
  char home[] = "/tmp/jot_dock_chords_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);

  Editor e;
  // A fresh editor opens on the home surface, which owns every key until it is
  // dismissed; close it so the chords reach the dispatcher.
  e.set_home_menu_visible(false);
  using jot::keybind_detail::decode_csi_u_key;

  // Protocol modifiers are a bitmask + 1: Ctrl=5, Ctrl+Alt=7. 98 = 'b',
  // 106 = 'j'.
  const int ctrl_alt_b = decode_csi_u_key("\x1b[98;7u");
  const int ctrl_j = decode_csi_u_key("\x1b[106;5u");
  REQUIRE((ctrl_alt_b & 0xFFFF) == 'b');
  REQUIRE((ctrl_alt_b & (0x20000 | 0x40000)) == (0x20000 | 0x40000));
  REQUIRE((ctrl_j & 0xFFFF) == 'j');
  REQUIRE((ctrl_j & 0x20000) != 0);
  REQUIRE((ctrl_j & 0x40000) == 0);

  // Ctrl+Alt+B drives the secondary sidebar and leaves the primary alone.
  e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  e.toggle_right_panel();
  REQUIRE_FALSE(e.right_panel_visible());
  const bool sidebar_before = e.sidebar_visible_for_test();
  e.raw_key_for_test(ctrl_alt_b);
  REQUIRE(e.right_panel_visible());
  REQUIRE(e.sidebar_visible_for_test() == sidebar_before);
  e.raw_key_for_test(ctrl_alt_b);
  REQUIRE_FALSE(e.right_panel_visible());
  REQUIRE(e.sidebar_visible_for_test() == sidebar_before);

  // Ctrl+J drives the bottom panel. A shell-less terminal keeps the case
  // headless: with the panel hidden and a terminal already live, the chord
  // reveals it without anything having to spawn a process. (The hiding half
  // of the cycle needs a terminal that survives a poll, so it is not
  // reachable here.)
  e.add_terminal_for_test();
  e.set_terminal_state_for_test(false, false, 10);
  REQUIRE_FALSE(e.terminal_visible_for_test());
  e.raw_key_for_test(ctrl_j);
  REQUIRE(e.terminal_visible_for_test());
}
