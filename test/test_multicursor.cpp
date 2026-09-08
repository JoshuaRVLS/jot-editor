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
      char cfgdir[] = "/tmp/jot_multicursor_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void reset_probe_lines()
  {
    Editor &e = probe_editor();
    e.host().core.set_buffer_content("foo bar foo\nbaz foo qux\nnothing here");
    e.host().core.set_cursor(0, 0);
    e.host().core.clear_extra_carets();
  }
} // namespace

TEST_CASE("Ctrl+D selects word then next occurrence", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.selected_text() == "foo");
  REQUIRE_FALSE(core.multicursor_active());

  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.multicursor_active());
  REQUIRE(core.selected_text() == "foo");

  REQUIRE(core.select_next_occurrence());
}

TEST_CASE("Typing inserts at every caret with one undo step", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.selected_text() == "foo");
  REQUIRE(core.extra_caret_count() == 1);
  REQUIRE(core.extra_caret_text(0) == "foo");
  e.delete_selection_for_test();
  REQUIRE(core.buffer_content() == " bar \nbaz foo qux\nnothing here");
  e.insert_string_for_test("X");
  REQUIRE(core.buffer_content() == "X bar X\nbaz foo qux\nnothing here");
  core.undo();
  REQUIRE(core.buffer_content() == " bar \nbaz foo qux\nnothing here");
  core.undo();
  REQUIRE(core.buffer_content() == "foo bar foo\nbaz foo qux\nnothing here");
}

TEST_CASE("Alt+Click caret dedupes and Esc clears", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  REQUIRE(core.add_caret_at(1, 0));
  REQUIRE(core.multicursor_active());
  REQUIRE_FALSE(core.add_caret_at(1, 0));
  REQUIRE(core.extra_caret_count() == 1);
  core.clear_extra_carets();
  REQUIRE_FALSE(core.multicursor_active());
}

TEST_CASE("Undo restores extra carets", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.multicursor_active());
  e.delete_selection_for_test();
  core.undo();
  REQUIRE(core.multicursor_active());
  REQUIRE(core.selected_text() == "foo");
  core.undo();
  REQUIRE_FALSE(core.multicursor_active());
}

TEST_CASE("Backspace deletes one char at every point caret", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  // Carets at (0,4) and (1,4); the live cursor follows the last Alt+click.
  REQUIRE(core.add_caret_at(0, 4));
  REQUIRE(core.add_caret_at(1, 4));
  REQUIRE(core.extra_caret_count() == 2);
  e.delete_char_for_test(false);
  // One char before each caret goes: "foo bar foo" -> "foobar foo",
  // "baz foo qux" -> "bazfoo qux". The caret sharing its cell with the
  // cursor is not double-deleted.
  REQUIRE(core.buffer_content() == "foobar foo\nbazfoo qux\nnothing here");
}

TEST_CASE("Backspace joins lines at a line-start caret", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  // Caret at (0,4), caret+cursor at (1,0): the join spans line 1 into line 0.
  REQUIRE(core.add_caret_at(0, 4));
  REQUIRE(core.add_caret_at(1, 0));
  e.delete_char_for_test(false);
  // Line 1 joins onto line 0 at the caret, and (0,4) still deletes its char.
  REQUIRE(core.buffer_content() == "foobar foobaz foo qux\nnothing here");
  // Cursor lands on the join point: the end of the original line 0.
  REQUIRE(core.cursor().first == 0);
  REQUIRE(core.cursor().second == 11);
}

TEST_CASE("Forward delete erases at every point caret and joins at EOL", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  // Caret at (1,3) and caret+cursor at (0,11) (end of line 0).
  REQUIRE(core.add_caret_at(1, 3));
  REQUIRE(core.add_caret_at(0, 11));
  e.delete_char_for_test(true);
  // (1,3) deletes its next char ("baz foo qux" -> "bazfoo qux"), then
  // (0,11) at end of line joins the next line onto line 0.
  REQUIRE(core.buffer_content() == "foo bar foobazfoo qux\nnothing here");
}

TEST_CASE("Delete with selection + point carets erases spans and chars", "[jot][multicursor]")
{
  reset_probe_lines();
  Editor &e = probe_editor();
  auto &core = e.host().core;
  // Ctrl+D twice selects the first two "foo"s, then an Alt+click point.
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.select_next_occurrence());
  REQUIRE(core.add_caret_at(2, 4));
  e.delete_char_for_test(false);
  // Both "foo" selections on line 0 vanish ("foo bar foo" -> " bar ");
  // the point caret deletes one char before it ("nothing here" ->
  // "noting here").
  REQUIRE(core.buffer_content() == " bar \nbaz foo qux\nnoting here");
}
