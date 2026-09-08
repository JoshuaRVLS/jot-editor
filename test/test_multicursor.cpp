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
