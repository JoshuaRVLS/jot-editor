// Occurrence highlight: the identifier under the caret is tinted at every other
// place it shows in the viewport, with the caret's own word on the stronger
// band, so a rename's blast radius is visible before a keystroke of it is typed.
//
// What counts as an occurrence is the whole question. A plain substring match
// would light the `print` inside `printf`, and a caret parked one cell past a
// word is still on that word (the same rule the edit commands use), while a
// caret sitting in the middle of a run of whitespace is on nothing at all. The
// tint is a band only: the token keeps the colour its syntax gave it, which is
// why the theme's fg slot is -1 unless a theme asks otherwise.
//
// The other question is when. A tint that re-lights on every keystroke's
// repaint is noise while the hands are still on the keys, so a buffer inside
// word_highlight_delay_ms of its last edit holds the highlight down (VS Code's
// occurrencesHighlightDelay); the frame loop asks for the repaint that ends the
// hold. Cursor moves and file opens are not edits, so they light it at once.
#include "editor.h"
#include "editor/model/panes.h" // pane_content_top
#include "render/gutter.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  // A fresh editor per case: the highlight is decided by the caret, the buffer
  // and the selection, and a shared editor would carry another case's state.
  void seed_config_home()
  {
    char home[] = "/tmp/jot_word_highlight_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file on disk the editor can load, one line per string, ending in a
  // newline. Names are unique per case: load_file focuses an already-open
  // buffer instead of re-reading it.
  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_word_highlight_" + name + ".cpp";
    std::ofstream out(path);
    for (const auto &line : lines)
    {
      out << line << "\n";
    }
    out.close();
    e.load_file(path);
  }

  // The cell a buffer position paints on this frame: the pane's own mapping
  // (border, gutter, the pane's first text row) at the current scroll.
  const UICell *cell_at(Editor &e, int line, int col)
  {
    const SplitPane &pane = e.pane_for_test();
    const int x = pane.x + 1 + gutter::width(e.buffer_for_test().line_count()) + col;
    const int y = pane_content_top(pane) + line;
    return e.ui_for_test()->cell_at(x, y);
  }

  // Repaints on demand: moving the caret through scroll_cursor_to_for_test does
  // not mark the frame dirty, and render() leaves an undirtied frame as it is.
  void repaint(Editor &e)
  {
    e.request_redraw_for_test();
    e.render_for_test();
  }

  // Whether every one of `len` cells at `col` wears the band the highlight
  // promises for that occurrence, read off the frame that was just rendered.
  bool wears(Editor &e, int line, int col, int len, bool strong)
  {
    const Theme &theme = e.theme_for_test();
    const int want = strong ? theme.bg_word_highlight_strong : theme.bg_word_highlight;
    for (int i = 0; i < len; i++)
    {
      const UICell *cell = cell_at(e, line, col + i);
      if (cell == nullptr || cell->bg != want)
      {
        return false;
      }
    }
    return true;
  }

  // Whether any occurrence band sits on the cells at all, either one.
  bool tinted(Editor &e, int line, int col, int len)
  {
    return wears(e, line, col, len, true) || wears(e, line, col, len, false);
  }
} // namespace

TEST_CASE("Word highlight: every occurrence is tinted, the caret's own strongest", "[jot]")
{
  seed_config_home();
  Editor e;
  // The typing hold (word_highlight_delay_ms) defaults to 500 ms and every case
  // here paints right after driving the caret, so the cases that do not test
  // the hold itself turn it off to stay deterministic.
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(
      e, {"int alpha = 1;", "int beta = alpha + 1;", "int gamma = alpha * beta;"}, "occurrences");
  e.apply_resize_for_test(100, 30);

  e.scroll_cursor_to_for_test(0, 4); // on the first `alpha`
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, true));
  REQUIRE(wears(e, 1, 11, 5, false));
  REQUIRE(wears(e, 2, 12, 5, false));
  // Another identifier on the same rows is untouched.
  REQUIRE_FALSE(tinted(e, 1, 4, 4));  // beta
  REQUIRE_FALSE(tinted(e, 2, 20, 4)); // beta

  // Moving onto `beta` moves both bands with it: its own row is the strong one,
  // and `alpha` is back to plain ink everywhere.
  e.scroll_cursor_to_for_test(1, 4);
  repaint(e);
  REQUIRE(wears(e, 1, 4, 4, true));
  REQUIRE(wears(e, 2, 20, 4, false));
  REQUIRE_FALSE(tinted(e, 0, 4, 5));
  REQUIRE_FALSE(tinted(e, 2, 12, 5));
}

TEST_CASE("Word highlight: only a whole word is an occurrence", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int print = 1;", "int printf = 2;", "int sprint = 3;"}, "wholeword");
  e.apply_resize_for_test(100, 30);

  e.scroll_cursor_to_for_test(0, 4); // on `print`
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, true));
  // Both neighbours contain the word without being it.
  REQUIRE_FALSE(tinted(e, 1, 4, 6)); // printf
  REQUIRE_FALSE(tinted(e, 2, 4, 6)); // sprint
}

TEST_CASE("Word highlight: a caret on whitespace lights nothing", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int  value = 1;", "int  value = value;"}, "noword");
  e.apply_resize_for_test(100, 30);

  // The second of the two spaces after `int`: a caret one cell past a word
  // still answers for that word, so being on nothing means the middle of a run
  // of whitespace.
  e.scroll_cursor_to_for_test(0, 4);
  repaint(e);
  REQUIRE_FALSE(tinted(e, 0, 5, 5));
  REQUIRE_FALSE(tinted(e, 1, 5, 5));
  REQUIRE_FALSE(tinted(e, 1, 13, 5));

  // One cell to the right is the word, and every occurrence lights from there.
  e.scroll_cursor_to_for_test(0, 5);
  repaint(e);
  REQUIRE(wears(e, 0, 5, 5, true));
  REQUIRE(wears(e, 1, 5, 5, false));
  REQUIRE(wears(e, 1, 13, 5, false));
}

TEST_CASE("Word highlight: the setting turns it off", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int alpha = 1;", "int beta = alpha + 1;"}, "setting");
  e.apply_resize_for_test(100, 30);

  e.scroll_cursor_to_for_test(0, 4);
  repaint(e);
  REQUIRE(wears(e, 1, 11, 5, false));

  e.set_config_for_test("word_highlight", "false");
  e.apply_config_live_for_test();
  repaint(e);
  REQUIRE_FALSE(tinted(e, 0, 4, 5));
  REQUIRE_FALSE(tinted(e, 1, 11, 5));
}

TEST_CASE("Word highlight: a selection's own text drives it", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int alpha = 1;", "int beta = alpha + 1;", "int gamma = alpha * 2;"}, "selection");
  e.apply_resize_for_test(100, 30);

  // Bare caret: its own word wears the strong band.
  e.scroll_cursor_to_for_test(0, 4);
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, true));

  // Selecting that word hands the highlight to the selection: the other two
  // occurrences light on the plain band, and the selected cells stay the
  // selection's own paint rather than anything the highlight asks for.
  REQUIRE(e.select_word_at_cursor());
  repaint(e);
  REQUIRE(wears(e, 1, 11, 5, false));
  REQUIRE(wears(e, 2, 12, 5, false));
  REQUIRE_FALSE(wears(e, 1, 11, 5, true));
  REQUIRE(cell_at(e, 0, 4)->bg == e.theme_for_test().bg_selection);

  // Clearing it hands the highlight back to the caret's word, which the caret is
  // still on: it sits one cell past the word the selection marked.
  e.buffer_for_test().selection = {{0, 0}, {0, 0}, false};
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, true));
  REQUIRE(wears(e, 1, 11, 5, false));
}

TEST_CASE("Word highlight: a selection matches the text it marks, not a word", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int alpha = 1;", "int alpha_beta = 2;"}, "partial");
  e.apply_resize_for_test(100, 30);

  // `alpha` is a whole word on the first row and only a prefix of `alpha_beta`
  // on the second. A range the user marked is matched as the text it is, so the
  // prefix being renamed shows the identifiers that carry it.
  e.buffer_for_test().selection = {{4, 0}, {9, 0}, true};
  repaint(e);
  REQUIRE(wears(e, 1, 4, 5, false));

  // The caret's word is held to the whole-word rule, so the same needle from the
  // caret leaves `alpha_beta` alone.
  e.buffer_for_test().selection = {{0, 0}, {0, 0}, false};
  e.scroll_cursor_to_for_test(0, 4);
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, true));
  REQUIRE_FALSE(tinted(e, 1, 4, 5));
}

TEST_CASE("Word highlight: typing holds the tint until the pause", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "500");
  load_lines(e, {"int alpha = 1;", "int beta = alpha + 1;", "int gamma = alpha * beta;"}, "hold");
  e.apply_resize_for_test(100, 30);

  // Nothing has edited the buffer (opening a file is not an edit): the caret's
  // word and its occurrences light the moment the frame paints.
  e.scroll_cursor_to_for_test(1, 11);
  repaint(e);
  REQUIRE(wears(e, 1, 11, 5, true));
  REQUIRE(wears(e, 2, 12, 5, false));

  // A keystroke restarts the hold, and the next frame leaves the highlight
  // down -- the caret's own band included, which after typing into the word is
  // the only band the new word (`Xalpha`, here and below) could wear.
  e.editor_input_for_test('X');
  repaint(e);
  REQUIRE_FALSE(tinted(e, 1, 11, 6));

  // The hold is a clock, not a keystroke count: winding the stamp back past the
  // delay is the pause, and the next frame lights the caret's word again.
  e.buffer_for_test().word_highlight_last_edit_ms -= 1000;
  repaint(e);
  REQUIRE(wears(e, 1, 11, 6, true));
}

TEST_CASE("Word highlight: an edit elsewhere holds the occurrences too", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "500");
  load_lines(
      e, {"int alpha = 1;", "int beta = alpha + 1;", "int gamma = alpha * beta;"}, "elsewhere");
  e.apply_resize_for_test(100, 30);

  // The caret stays on `alpha` while the edit goes through the bulk path
  // (replace_lines, the path undo's snapshot replacement uses) on the row
  // above, so the needle survives the edit and only the hold can hold the tint
  // back -- the plain-band occurrences and the caret's own strong band alike.
  e.scroll_cursor_to_for_test(1, 11);
  repaint(e);
  REQUIRE(wears(e, 2, 12, 5, false));

  e.buffer_for_test().replace_lines(0, 1, {"int alpha = 1; // touched"});
  repaint(e);
  REQUIRE_FALSE(tinted(e, 0, 4, 5));
  REQUIRE_FALSE(tinted(e, 1, 11, 5));
  REQUIRE_FALSE(tinted(e, 2, 12, 5));

  e.buffer_for_test().word_highlight_last_edit_ms -= 1000;
  repaint(e);
  REQUIRE(wears(e, 0, 4, 5, false));
  REQUIRE(wears(e, 1, 11, 5, true));
  REQUIRE(wears(e, 2, 12, 5, false));
}

TEST_CASE("Word highlight: a zero delay paints on the keystroke's frame", "[jot]")
{
  seed_config_home();
  Editor e;
  // 0 is the setting that keeps the old behaviour: the tint is up on the very
  // frame the keystroke lands on, no pause asked for. Same keystroke the hold
  // case above withholds.
  e.set_config_for_test("word_highlight_delay_ms", "0");
  load_lines(e, {"int alpha = 1;", "int beta = alpha + 1;"}, "nodelay");
  e.apply_resize_for_test(100, 30);

  e.scroll_cursor_to_for_test(1, 11);
  e.editor_input_for_test('X');
  repaint(e);
  REQUIRE(wears(e, 1, 11, 6, true));
}

TEST_CASE("Word highlight: the frame after the hold is asked for", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_config_for_test("word_highlight_delay_ms", "500");
  load_lines(e, {"int alpha = 1;", "int beta = alpha + 1;"}, "reveal");
  e.apply_resize_for_test(100, 30);

  // A keystroke opens the hold; the frame loop asks for nothing while it runs.
  e.scroll_cursor_to_for_test(1, 21);
  e.editor_input_for_test('X');
  REQUIRE_FALSE(e.word_highlight_due_soon_for_test());

  // The first sight of the clock past the delay is the one ask that cannot be
  // missed, however late the frame loop looks -- and it asks exactly once.
  e.buffer_for_test().word_highlight_last_edit_ms -= 1000;
  REQUIRE(e.word_highlight_due_soon_for_test());
  REQUIRE_FALSE(e.word_highlight_due_soon_for_test());
}
