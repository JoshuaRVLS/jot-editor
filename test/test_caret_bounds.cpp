// Where a caret may sit after the text under it gets shorter
// (Editor::trim_trailing_whitespace in src/input/commands/actions.cpp, and the
// insert paths in src/text/edit.cpp).
//
// Two halves of one shape. `:trim` shortens the lines a caret sat at the end
// of, and the caret's column stays where it was -- past the end of its line.
// The next typed character then went through std::string::insert(), which
// throws for a position past the end (basic_string::insert: __pos ... >
// this->size()), and the editor aborted; the pty probe trim_caret_probe.py
// watches that abort through the real binary. These cases pin the command
// pulling its carets back, the insert paths answering an out-of-range column
// by appending instead of throwing, and the undo step a trim that trims
// nothing must not consume.

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
      char cfgdir[] = "/tmp/jot_caret_bounds_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A fresh buffer with the caret where the case asks for it. Setting the
  // content goes through save_state, so no case inherits history from the
  // previous one.
  void seed(const std::string &text, int line = 0, int col = 0)
  {
    Editor &e = probe_editor();
    e.set_home_menu_visible(false);
    auto &core = e.host().core;
    core.set_buffer_content(text);
    core.clear_extra_carets();
    core.set_cursor(line, col);
  }

  std::string content()
  {
    return probe_editor().host().core.buffer_content();
  }

  void type(char c)
  {
    probe_editor().host().core.insert_char_at_carets(c);
  }

  // A second caret at an exact position, set directly: what is under test is
  // what the trim does with it, not how it was placed.
  void add_caret(int line, int col)
  {
    auto &buf = probe_editor().buffer_for_test();
    const Cursor point{col, line};
    buf.extra_carets.push_back(Selection{point, point, false});
  }
} // namespace

TEST_CASE("Trim: the caret at the end of a trimmed line comes back onto it", "[jot][trim]")
{
  Editor &e = probe_editor();
  seed("alpha   \nbeta\ngamma\n", 0, 8);
  REQUIRE(e.buffer_for_test().cursor.x == 8); // the trailing spaces, at EOL

  e.trim_trailing_whitespace_for_test();

  REQUIRE(content() == "alpha\nbeta\ngamma\n");
  // The column the caret kept was past the shortened line; typing there has to
  // land at its new end, which is where the caret now is.
  REQUIRE(e.buffer_for_test().cursor.x == 5);
  type('Z');
  REQUIRE(content() == "alphaZ\nbeta\ngamma\n");
  REQUIRE(e.buffer_for_test().cursor.x == 6);
}

TEST_CASE("Trim: an extra caret past the trimmed end comes back too", "[jot][trim]")
{
  Editor &e = probe_editor();
  seed("alpha   \nbeta\n", 0, 0);
  add_caret(0, 8);

  e.trim_trailing_whitespace_for_test();

  REQUIRE(content() == "alpha\nbeta\n");
  REQUIRE(e.buffer_for_test().extra_carets.size() == 1);
  REQUIRE(e.buffer_for_test().extra_carets[0].start.x == 5);
  REQUIRE(e.buffer_for_test().extra_carets[0].end.x == 5);
}

TEST_CASE("Trim: undo restores the whitespace the trim removed", "[jot][trim]")
{
  Editor &e = probe_editor();
  seed("alpha   \nbeta   \n", 0, 8);

  e.trim_trailing_whitespace_for_test();
  REQUIRE(content() == "alpha\nbeta\n");

  e.host().core.undo();
  REQUIRE(content() == "alpha   \nbeta   \n");
}

TEST_CASE("Trim: a command that trims nothing does not consume an undo step", "[jot][trim]")
{
  Editor &e = probe_editor();
  seed("abc", 0, 0);
  type('d');
  REQUIRE(content() == "dabc");

  // Nothing here has trailing whitespace, so the command is not an edit: the
  // undo that follows the last real edit has to undo that edit, not a no-op
  // state this command pushed.
  e.trim_trailing_whitespace_for_test();
  REQUIRE(content() == "dabc");

  e.host().core.undo();
  REQUIRE(content() == "abc");
}

TEST_CASE("Insert: a column past the end of the line appends instead of throwing", "[jot][insert]")
{
  Editor &e = probe_editor();
  seed("alpha\n", 0, 5);
  REQUIRE(e.buffer_for_test().cursor.x == 5);

  // A path that shortened the line without moving the caret (an LSP edit, a
  // plugin) leaves this behind; the insert has to answer it, not abort.
  e.buffer_for_test().cursor.x = 8;
  type('Z');
  REQUIRE(content() == "alphaZ\n");
  REQUIRE(e.buffer_for_test().cursor.x == 6);

  e.buffer_for_test().cursor.x = 99;
  e.insert_string_for_test("!");
  REQUIRE(content() == "alphaZ!\n");
  REQUIRE(e.buffer_for_test().cursor.x == 7);
}
