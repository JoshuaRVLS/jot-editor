// Recordable macros (src/input/macros.cpp, runtime/lua/features/keymaps.lua).
//
// The gestures go through the frontend's key path (terminal_key_for_test ->
// handle_terminal_event -> EVENT_KEY), which is where the recorder listens, so
// what a case exercises is the same path a real terminal key takes: the
// recorder sees the key before any mode can consume it.
//
// Pins the contract the feature is built on:
//   * a macro is keystrokes, so playing one re-runs the keys -- the edits land
//     again, in whatever buffer is current, through the same keymaps;
//   * the macro chords themselves are not part of the recording (otherwise
//     playing a macro would stop the recording it was played from);
//   * an empty register reports instead of replaying something else, and a
//     replay in flight is refused rather than allowed to recurse;
//   * the registers stay apart: @a and @q are different lists.
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_macros_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  std::string write_file(const std::string &name, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_macro_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + name;
    std::ofstream out(path);
    out << text;
    out.close();
    return path;
  }

  std::string open_file(Editor &e, const std::string &text = "alpha\nbeta\n")
  {
    const std::string path = write_file("a.txt", text);
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.set_home_menu_visible(false);
    return path;
  }

  // Types one letter through the event funnel, the way a terminal key arrives.
  void type(Editor &e, char c)
  {
    e.terminal_key_for_test(c);
  }
} // namespace

TEST_CASE("Macros: a recording replays the keys it captured", "[jot]")
{
  seed_config_home();
  Editor e;
  open_file(e);
  e.scroll_cursor_to_for_test(0, 0);

  REQUIRE_FALSE(e.macro_recording_for_test());
  e.run_ex_for_test("macrorecord z");
  REQUIRE(e.macro_recording_for_test());

  type(e, 'h');
  type(e, 'i');
  REQUIRE(e.buffer_for_test().line(0) == "hialpha");
  REQUIRE(e.macro_keys_for_test('z') == 2);

  e.run_ex_for_test("macroplay z");
  REQUIRE_FALSE(e.macro_recording_for_test());
  // The two keys landed a second time -- the macro re-ran the keystrokes, it
  // did not replay a recorded edit.
  REQUIRE(e.buffer_for_test().line(0) == "hihialpha");
  REQUIRE(e.message_for_test().find("Played @z") != std::string::npos);
}

TEST_CASE("Macros: the chords that control the recorder stay out of it", "[jot]")
{
  seed_config_home();
  Editor e;
  open_file(e);
  e.scroll_cursor_to_for_test(0, 0);

  // Alt+Z q opens a recording from the chord (the Lua keymap when the bundled
  // runtime is loaded, the native prefix in dispatch.cpp when it is not; both
  // end in :macrorecord).
  e.terminal_key_for_test('z', false, false, true);
  e.terminal_key_for_test('q');
  REQUIRE(e.macro_recording_for_test());

  type(e, 'x');
  REQUIRE(e.buffer_for_test().line(0) == "xalpha");

  // ...and Alt+Z q stops it. Neither the Alt+Z press nor the letter after it is
  // part of the recording: replaying a macro must not start or stop a recording
  // of its own.
  e.terminal_key_for_test('z', false, false, true);
  e.terminal_key_for_test('q');
  REQUIRE_FALSE(e.macro_recording_for_test());
  REQUIRE(e.macro_keys_for_test('q') == 1);

  e.terminal_key_for_test('z', false, false, true);
  e.terminal_key_for_test('p');
  REQUIRE_FALSE(e.macro_recording_for_test());
  REQUIRE(e.buffer_for_test().line(0) == "xxalpha");
}

TEST_CASE("Macros: registers stay apart and an empty one reports", "[jot]")
{
  seed_config_home();
  Editor e;
  open_file(e);
  e.scroll_cursor_to_for_test(0, 0);

  e.run_ex_for_test("macrorecord a");
  type(e, 'A');
  e.run_ex_for_test("macrostop");
  e.run_ex_for_test("macrorecord b");
  type(e, 'B');
  type(e, 'B');
  e.run_ex_for_test("macrostop");

  REQUIRE(e.macro_keys_for_test('a') == 1);
  REQUIRE(e.macro_keys_for_test('b') == 2);
  REQUIRE(e.buffer_for_test().line(0) == "ABBalpha");

  // Replaying one register leaves the other alone: @a's single "A" lands at the
  // caret (after "ABB"), not at the start of the line.
  e.run_ex_for_test("macroplay a");
  REQUIRE(e.buffer_for_test().line(0) == "ABBAalpha");

  // A register that was never recorded says so, and changes nothing.
  e.run_ex_for_test("macroplay q");
  REQUIRE(e.buffer_for_test().line(0) == "ABBAalpha");
  REQUIRE(e.message_for_test().find("empty") != std::string::npos);

  const std::string list = e.macro_list_for_test();
  REQUIRE(list.find("@a 1") != std::string::npos);
  REQUIRE(list.find("@b 2") != std::string::npos);
  // The last register played is marked, which is the one Alt+Z p would re-run.
  REQUIRE(list.find("@a 1*") != std::string::npos);
}

TEST_CASE("Macros: a count replays the register that many times", "[jot]")
{
  seed_config_home();
  Editor e;
  open_file(e);
  e.scroll_cursor_to_for_test(0, 0);

  e.run_ex_for_test("macrorecord z");
  type(e, 'x');
  e.run_ex_for_test("macrostop");

  e.run_ex_for_test("macroplay z 3");
  REQUIRE(e.buffer_for_test().line(0) == "xxxxalpha");
  REQUIRE(e.message_for_test().find("x3") != std::string::npos);

  // A count that is not a number is refused rather than read as one.
  const std::string before = e.buffer_for_test().line(0);
  e.run_ex_for_test("macroplay z many");
  REQUIRE(e.buffer_for_test().line(0) == before);
  REQUIRE(e.message_for_test().find("count must be a number") != std::string::npos);
}

TEST_CASE("Macros: the recording survives an explicit stop and a re-record", "[jot]")
{
  seed_config_home();
  Editor e;
  open_file(e);
  e.scroll_cursor_to_for_test(0, 0);

  // Starting a recording on a register throws away what it held: the user asked
  // for a fresh take, and appending to the old one would be a surprise.
  e.run_ex_for_test("macrorecord z");
  type(e, 'o');
  type(e, 'l');
  e.run_ex_for_test("macrostop");
  REQUIRE(e.macro_keys_for_test('z') == 2);
  REQUIRE(e.buffer_for_test().line(0) == "olalpha");

  e.run_ex_for_test("macrorecord z");
  type(e, 'n');
  e.run_ex_for_test("macrostop");
  REQUIRE(e.macro_keys_for_test('z') == 1);
  REQUIRE(e.buffer_for_test().line(0) == "olnalpha");

  // The replay is the fresh take ("n"), not the pair the register held first
  // ("ol") -- which is what "a recording starts over" means.
  e.run_ex_for_test("macroplay z");
  REQUIRE(e.buffer_for_test().line(0) == "olnnalpha");
}
