// The jumplist: every navigation records where it landed, and Ctrl+O / Ctrl+I
// walk those places.
//
// The editor used to keep a single one-way stack that only go-to-definition
// pushed to, so Ctrl+O came back from a definition and nothing else had any
// history. These tests pin the general behaviour: recording, walking back and
// forward, forking (a jump after going back drops the forward tail), and the cap.
#include "editor.h"

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
      char cfgdir[] = "/tmp/jot_jumplist_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A file big enough that a cursor line inside it is meaningful.
  std::string write_file(const std::string &tag)
  {
    const std::string path = "/tmp/jot_jumplist_" + std::to_string(::getpid()) + "_" + tag + ".txt";
    std::ofstream out(path);
    for (int i = 0; i < 60; i++)
    {
      out << "line " << i << " of " << tag << "\n";
    }
    out.close();
    return path;
  }
} // namespace

TEST_CASE("Jumplist records where a jump lands and walks back and forward", "[jot][jumplist]")
{
  Editor &e = probe_editor();
  const std::string file = write_file("walk");
  e.load_file(file);
  e.reset_jumplist_for_test();

  e.scroll_cursor_to_for_test(10, 0);
  e.record_jump_for_test();
  e.scroll_cursor_to_for_test(30, 0);
  e.record_jump_for_test();

  REQUIRE(e.jump_count_for_test() == 2);
  REQUIRE(e.jump_position_for_test() == 1);
  REQUIRE(e.jump_line_for_test(0) == 10);
  REQUIRE(e.jump_line_for_test(1) == 30);

  // Back: the cursor returns to the earlier place, the position index moves with
  // it, and the history is untouched (forward still has somewhere to go).
  e.jump_back_for_test();
  REQUIRE(e.jump_position_for_test() == 0);
  REQUIRE(e.buffer_for_test().cursor.y == 10);
  REQUIRE(e.jump_count_for_test() == 2);

  e.jump_forward_for_test();
  REQUIRE(e.jump_position_for_test() == 1);
  REQUIRE(e.buffer_for_test().cursor.y == 30);

  // Already at the newest entry: forward is a no-op rather than an error.
  e.jump_forward_for_test();
  REQUIRE(e.jump_position_for_test() == 1);
  REQUIRE(e.buffer_for_test().cursor.y == 30);
}

TEST_CASE("Jumplist forking drops the forward tail", "[jot][jumplist]")
{
  Editor &e = probe_editor();
  const std::string file = write_file("fork");
  e.load_file(file);
  e.reset_jumplist_for_test();

  e.scroll_cursor_to_for_test(5, 0);
  e.record_jump_for_test();
  e.scroll_cursor_to_for_test(15, 0);
  e.record_jump_for_test();
  e.scroll_cursor_to_for_test(25, 0);
  e.record_jump_for_test();
  REQUIRE(e.jump_count_for_test() == 3);

  e.jump_back_for_test(); // position 1 (line 15)
  REQUIRE(e.buffer_for_test().cursor.y == 15);

  // A new jump from here replaces the entry we had walked back from instead of
  // leaving a stale forward tail pointing at line 25.
  e.scroll_cursor_to_for_test(45, 0);
  e.record_jump_for_test();
  REQUIRE(e.jump_count_for_test() == 3);
  REQUIRE(e.jump_position_for_test() == 2);
  REQUIRE(e.jump_line_for_test(2) == 45);

  e.jump_forward_for_test(); // nothing ahead any more
  REQUIRE(e.jump_position_for_test() == 2);
  REQUIRE(e.buffer_for_test().cursor.y == 45);
}

TEST_CASE("Jumplist crosses files and caps its length", "[jot][jumplist]")
{
  Editor &e = probe_editor();
  const std::string a = write_file("a");
  const std::string b = write_file("b");

  e.load_file(a);
  e.reset_jumplist_for_test();
  e.scroll_cursor_to_for_test(3, 0);
  e.record_jump_for_test();
  e.load_file(b);
  e.scroll_cursor_to_for_test(7, 0);
  e.record_jump_for_test();

  // Walking back reopens the other file, not just repositions this one.
  e.jump_back_for_test();
  REQUIRE(fs::path(e.jump_path_for_test(e.jump_position_for_test())) == fs::path(a));
  REQUIRE(e.buffer_for_test().cursor.y == 3);

  // The history is bounded: past the cap the oldest entries fall off the front
  // and the position index keeps pointing at the newest one.
  for (int i = 0; i < 150; i++)
  {
    e.record_jump_for_test();
  }
  REQUIRE(e.jump_count_for_test() == 100);
  REQUIRE(e.jump_position_for_test() == e.jump_count_for_test() - 1);
}
