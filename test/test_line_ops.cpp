// The pure halves of the editor's line commands: the line-range edits
// (src/text/line_ops.*) and the counts behind `:stats`
// (src/text/buffer_stats.*).
//
// Two halves, for the two ways this can go wrong. The rules cases pin the
// algorithms: what a range edit does to the lines, what it leaves outside the
// range, and what it reports back. The editor cases then run the commands
// themselves -- `:trimblank`, `:joinlines`, `:sortdesc` -- through a real
// editor, so the range worked out from the selection and the buffer the user
// ends up with are asserted rather than assumed.
#include "editor.h"
#include "text/buffer_stats.h"
#include "text/line_ops.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
  using Lines = std::vector<std::string>;

  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_line_ops_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("Line ops: sort folds case and keeps equal lines in order", "[line_ops][jot]")
{
  Lines lines{"banana", "Apple", "apple", "Cherry"};

  SECTION("ascending")
  {
    line_ops::sort_lines(lines, 0, 3, false);
    // `Apple` before `apple`: the two are equal once folded, and a stable sort
    // is what leaves them the way the buffer had them.
    REQUIRE(lines == Lines{"Apple", "apple", "banana", "Cherry"});
  }

  SECTION("descending")
  {
    line_ops::sort_lines(lines, 0, 3, true);
    REQUIRE(lines == Lines{"Cherry", "banana", "Apple", "apple"});
  }
}

TEST_CASE("Line ops: an edit stays inside the range it is given", "[line_ops][jot]")
{
  SECTION("sort")
  {
    Lines lines{"z", "banana", "apple", "Cherry"};
    line_ops::sort_lines(lines, 1, 3, false);
    REQUIRE(lines == Lines{"z", "apple", "banana", "Cherry"});
  }

  SECTION("reverse")
  {
    Lines lines{"a", "b", "c", "d"};
    line_ops::reverse_lines(lines, 1, 2);
    REQUIRE(lines == Lines{"a", "c", "b", "d"});
  }

  SECTION("unique keeps the drops local")
  {
    Lines lines{"a", "a", "a", "a", "b"};
    REQUIRE(line_ops::remove_duplicate_lines(lines, 1, 2) == 1);
    REQUIRE(lines == Lines{"a", "a", "a", "b"});
  }

  SECTION("trim blank keeps the drops local")
  {
    Lines lines{"", "x", "", ""};
    REQUIRE(line_ops::remove_blank_lines(lines, 1, 2) == 1);
    REQUIRE(lines == Lines{"", "x", ""});
  }
}

TEST_CASE("Line ops: a range that names fewer than two lines is left alone", "[line_ops][jot]")
{
  Lines lines{"only", "one", "line"};

  line_ops::sort_lines(lines, 1, 1, false);
  line_ops::sort_lines(lines, 2, 1, false);
  line_ops::sort_lines(lines, 0, 3, false);
  line_ops::reverse_lines(lines, 3, 1);
  line_ops::shuffle_lines(lines, -1, 1);
  REQUIRE(line_ops::remove_duplicate_lines(lines, 0, 0) == 0);
  REQUIRE(line_ops::join_lines(lines, 1, 1) == 0);
  REQUIRE(lines == Lines{"only", "one", "line"});
}

TEST_CASE("Line ops: shuffle keeps every line and the lines around them", "[line_ops][jot]")
{
  Lines lines{"head", "a", "b", "c", "d", "tail"};
  line_ops::shuffle_lines(lines, 1, 4);

  REQUIRE(lines.front() == "head");
  REQUIRE(lines.back() == "tail");
  Lines middle(lines.begin() + 1, lines.begin() + 5);
  std::sort(middle.begin(), middle.end());
  REQUIRE(middle == Lines{"a", "b", "c", "d"});
}

TEST_CASE("Line ops: unique keeps the first of each line and counts the rest", "[line_ops][jot]")
{
  Lines lines{"a", "b", "a", "b", "c", "c"};
  REQUIRE(line_ops::remove_duplicate_lines(lines, 0, 5) == 3);
  REQUIRE(lines == Lines{"a", "b", "c"});

  Lines already_unique{"a", "b", "c"};
  REQUIRE(line_ops::remove_duplicate_lines(already_unique, 0, 2) == 0);
  REQUIRE(already_unique == Lines{"a", "b", "c"});
}

TEST_CASE("Line ops: trim blank never empties the buffer", "[line_ops][jot]")
{
  Lines lines{"x", "", "y", "   ", "z"};
  REQUIRE(line_ops::remove_blank_lines(lines, 0, 4) == 2);
  REQUIRE(lines == Lines{"x", "y", "z"});

  Lines blanks{"", "   "};
  REQUIRE(line_ops::remove_blank_lines(blanks, 0, 1) == 1);
  REQUIRE(blanks == Lines{""});

  Lines none{"a", "b"};
  REQUIRE(line_ops::remove_blank_lines(none, 0, 1) == 0);
}

TEST_CASE("Line ops: join left-trims each continuation and spaces it", "[line_ops][jot]")
{
  Lines lines{"foo", "   bar", "", "baz"};
  REQUIRE(line_ops::join_lines(lines, 0, 3) == 3);
  REQUIRE(lines == Lines{"foo bar baz"});

  // A line that already ends in whitespace does not get a second space, which is
  // what keeps a re-join from padding a wrapped line every time it runs.
  Lines spaced{"foo   ", "bar"};
  REQUIRE(line_ops::join_lines(spaced, 0, 1) == 1);
  REQUIRE(spaced == Lines{"foo   bar"});
}

TEST_CASE("Buffer stats: lines, characters and words", "[buffer_stats][jot]")
{
  const Lines lines{"hello world", "foo_bar 42", ""};
  buffer_stats::Counter counter(false, 0, 0, 0, 0);
  for (int i = 0; i < (int)lines.size(); i++)
  {
    counter.add_line(i, lines[(size_t)i]);
  }
  const buffer_stats::Counts counts = counter.totals();

  REQUIRE(counts.lines == 3);
  REQUIRE(counts.chars == 21);
  // `foo_bar` is one word: the underscore is a word character.
  REQUIRE(counts.words == 4);
  REQUIRE(counts.selected_chars == 0);
}

TEST_CASE("Buffer stats: the selection's characters, whatever way round it is",
          "[buffer_stats][jot]")
{
  const Lines lines{"hello world", "foo_bar 42", "third"};

  buffer_stats::Counter forward(true, 0, 0, 1, 3);
  buffer_stats::Counter backward(true, 1, 3, 0, 0);
  for (int i = 0; i < (int)lines.size(); i++)
  {
    forward.add_line(i, lines[(size_t)i]);
    backward.add_line(i, lines[(size_t)i]);
  }

  // The whole first line plus `foo` of the second.
  REQUIRE(forward.totals().selected_chars == 14);
  REQUIRE(backward.totals().selected_chars == 14);

  // A selection inside one line counts that line's span only.
  buffer_stats::Counter inside(true, 1, 0, 1, 2);
  inside.add_line(0, lines[0]);
  inside.add_line(1, lines[1]);
  inside.add_line(2, lines[2]);
  REQUIRE(inside.totals().selected_chars == 2);
}

TEST_CASE("The line commands work a real buffer end to end", "[line_ops][jot]")
{
  Editor &e = probe_editor();
  auto &core = e.host().core;

  SECTION(":trimblank trims the whole buffer when nothing is selected")
  {
    core.set_buffer_content("x\n \ny\n");
    e.run_ex_for_test(":trimblank");
    REQUIRE(core.buffer_content() == "x\ny");
  }

  SECTION(":joinlines joins the cursor's line with the one under it")
  {
    core.set_buffer_content("foo\n   bar\nbaz\n");
    core.set_cursor(0, 0);
    e.run_ex_for_test(":joinlines");
    REQUIRE(core.buffer_content() == "foo bar\nbaz\n");
  }

  SECTION(":sortdesc sorts the selected lines and leaves the rest alone")
  {
    core.set_buffer_content("banana\nApple\napple\nCherry\ntail\n");
    core.set_cursor(0, 0);
    // A selection from the top of line 0 to the top of line 3, which is the
    // lines 0 to 3 the command works from (Cursor is {x, y}).
    e.buffer_for_test().selection = {{0, 0}, {0, 3}, true};
    e.run_ex_for_test(":sortdesc");
    REQUIRE(core.buffer_content() == "Cherry\nbanana\nApple\napple\ntail\n");
  }
}
