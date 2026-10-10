// Alt chords are shortcuts, wherever the focus happens to be
// (src/input/modes/dispatch.cpp, src/input/modes/modeless.cpp).
//
// Two failures are pinned here, and they look like one from the outside ("Alt
// does not work in my terminal") while both are the editor's own doing:
//
//   * the file explorer dropped every Alt chord while it had focus. `jot <dir>`
//     starts with the explorer focused and clicking a file in the tree leaves
//     the focus there, so in the ordinary flow Alt+S / Alt+W / Alt+P did
//     nothing -- while Ctrl chords were routed through, which is what made it
//     read as a terminal or shell problem rather than a focus one;
//   * an Alt chord that no keybind claims typed its own letter. Alt+X is not
//     bound to anything, and the modeless fallback inserted "x" into the
//     buffer.
//
// The gestures go through the frontend's decode path (raw_key_for_test), so
// what is asserted is the key as the terminal delivers it: the Alt bit is
// 0x40000, exactly what a kitty CSI-u report and a legacy ESC-prefixed letter
// both decode to.
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
  // The input path's Alt modifier bit (see src/ui/terminal.h).
  const int ALT = 0x40000;

  void seed_config_home()
  {
    char home[] = "/tmp/jot_alt_chords_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  std::string write_file(const std::string &name, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_alt_chords_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + name;
    std::ofstream out(path);
    out << text;
    out.close();
    return path;
  }

  // An editor with one real file open, in normal/modeless mode, sized so the
  // buffer has rows.
  std::string open_file(Editor &e, const std::string &text = "alpha\nbeta\ngamma\n")
  {
    const std::string path = write_file("a.txt", text);
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    return path;
  }
} // namespace

TEST_CASE("Alt chords: the explorer keeps global Alt shortcuts alive", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_file(e);
  e.toggle_sidebar_for_test();
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_SIDEBAR);
  REQUIRE(e.sidebar_visible_for_test());

  // Alt+N opens a scratch buffer, exactly as it does from the editor. This is
  // the gesture that used to be swallowed: with the explorer focused the tab
  // count stayed where it was and the status line said nothing at all.
  const int before = e.buffer_count_for_test();
  e.raw_key_for_test('n' | ALT);
  REQUIRE(e.buffer_count_for_test() == before + 1);

  // ...and Alt+W closes it again, so the routing is not a one-way accident of
  // the one command (nor of a chord that happened to be a prefix somewhere).
  e.raw_key_for_test('w' | ALT);
  REQUIRE(e.buffer_count_for_test() == before);
}

TEST_CASE("Alt chords: an unclaimed Alt chord types nothing", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_file(e);

  // Not a single letter below is bound to Alt: each one must be inert rather
  // than land in the buffer (they used to insert "x", "z" and "q").
  for (const char chord : {'x', 'z', 'q', 'v', 'k'})
  {
    e.scroll_cursor_to_for_test(0, 0);
    e.raw_key_for_test(chord | ALT);
    REQUIRE(e.buffer_for_test().line(0) == "alpha");
  }

  // ...and the same in the explorer, which now forwards Alt through the same
  // path: no stray letter may reach the buffer from there either.
  e.toggle_sidebar_for_test();
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_SIDEBAR);
  for (const char chord : {'x', 'z', 'q'})
  {
    e.raw_key_for_test(chord | ALT);
    REQUIRE(e.buffer_for_test().line(0) == "alpha");
  }
}

TEST_CASE(
    "Alt chords: a plain letter is still text, and a plain letter in the explorer still is not",
    "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_file(e);

  // The guard for a modified chord must not swallow the ordinary typing path.
  e.scroll_cursor_to_for_test(0, 0);
  e.raw_key_for_test('x');
  REQUIRE(e.buffer_for_test().line(0) == "xalpha");

  // While the explorer has focus a plain key belongs to the tree: the buffer
  // underneath must not see it.
  e.toggle_sidebar_for_test();
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_SIDEBAR);
  e.raw_key_for_test('z');
  REQUIRE(e.buffer_for_test().line(0) == "xalpha");
}

TEST_CASE("Alt chords: the Problems panel is not a keyboard dead zone", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_file(e);
  e.show_problems_panel_for_test();
  REQUIRE(e.focus_state_for_test() == (int)FOCUS_BOTTOM_PANEL);

  // A plain key still belongs to the list, not to the buffer behind it.
  e.raw_key_for_test('x');
  REQUIRE(e.buffer_for_test().line(0) == "alpha");

  // A modified chord is global again: Alt+N opens a buffer even though the
  // list has focus (before this, the panel returned true for every key, so the
  // chord died on the panel).
  const int before = e.buffer_count_for_test();
  e.raw_key_for_test('n' | ALT);
  REQUIRE(e.buffer_count_for_test() == before + 1);
}
