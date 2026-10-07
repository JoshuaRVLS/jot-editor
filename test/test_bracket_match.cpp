// Bracket-match highlight: while the caret is on a bracket, that bracket and
// its partner are painted in the theme's BracketMatch colors, so a closing
// brace three rows down answers for the opening one the caret is parked on.
//
// Which cell is "under the caret" is the whole question. The caret is a block
// on one character, and insert mode advances past the bracket it has just typed
// (the edit inserts at the caret, then steps one cell right), so the pair is
// the caret's own cell when that is a bracket, otherwise the cell just left of
// it. A bracket to the *right* of the caret is not one the caret is on, and an
// unmatched bracket under the caret must not fall through to a neighbour's
// pair -- both are pinned here, because they are the cases that look like
// highlights in the wrong place.
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
  // A fresh editor per case: the highlight is decided by the caret and the
  // buffer, and a shared editor would carry another case's caret and scroll.
  void seed_config_home()
  {
    char home[] = "/tmp/jot_bracket_match_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file on disk the probe can load, one line per string, ending in a
  // newline. Names are unique per case: load_file focuses an already-open
  // buffer instead of re-reading it.
  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_bracket_match_" + name + ".cpp";
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

  // Whether the cell carries the theme's match pair -- the paint the highlight
  // promises, read off the frame that was just rendered.
  bool is_match_cell(Editor &e, int line, int col)
  {
    const UICell *cell = cell_at(e, line, col);
    const Theme &theme = e.theme_for_test();
    return cell != nullptr && cell->fg == theme.fg_bracket_match
           && cell->bg == theme.bg_bracket_match;
  }
} // namespace

TEST_CASE("Bracket match: the caret's pair is boxed on both cells", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e,
             {"int alpha = 1;", "int run() {", "  int inner = 2;", "  return inner;", "}"},
             "pair");
  e.apply_resize_for_test(100, 30);

  // Caret on the opening brace: the closing one four rows down lights up with
  // it, and the brace under the caret keeps the same paint (the caret itself is
  // drawn by the backend, not as a cell).
  e.scroll_cursor_to_for_test(1, 10);
  repaint(e);
  REQUIRE(is_match_cell(e, 1, 10));
  REQUIRE(is_match_cell(e, 4, 0));
  REQUIRE_FALSE(is_match_cell(e, 1, 0));

  // From the closing side, the opening one is the far cell.
  e.scroll_cursor_to_for_test(4, 0);
  repaint(e);
  REQUIRE(is_match_cell(e, 4, 0));
  REQUIRE(is_match_cell(e, 1, 10));

  // A caret that is not on a bracket lights nothing.
  e.scroll_cursor_to_for_test(0, 0);
  repaint(e);
  REQUIRE_FALSE(is_match_cell(e, 1, 10));
  REQUIRE_FALSE(is_match_cell(e, 4, 0));
}

TEST_CASE("Bracket match: the bracket the caret has just typed counts", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"int call(int value) {", "  return value;", "}"}, "typed");
  e.apply_resize_for_test(100, 30);

  // The pair on the first line: `(` at 8, `)` at 18, and the `{` at 20 with no
  // partner this far down the file.
  // Insert mode left the caret one cell past the `(` it typed: that bracket is
  // still the one the caret is on.
  e.scroll_cursor_to_for_test(0, 9);
  repaint(e);
  REQUIRE(is_match_cell(e, 0, 8));
  REQUIRE(is_match_cell(e, 0, 18));
  REQUIRE_FALSE(is_match_cell(e, 0, 20));

  // A bracket to the *right* of the caret is not: the caret cell decides, and
  // an eager neighbour would light pairs the caret never touched.
  e.scroll_cursor_to_for_test(0, 7);
  repaint(e);
  REQUIRE_FALSE(is_match_cell(e, 0, 8));
  REQUIRE_FALSE(is_match_cell(e, 0, 18));
}

TEST_CASE("Bracket match: an unmatched bracket lights nothing", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"int broken = (1;", "int fine = 2;"}, "unmatched");
  e.apply_resize_for_test(100, 30);

  e.scroll_cursor_to_for_test(0, 13);
  repaint(e);
  REQUIRE_FALSE(is_match_cell(e, 0, 13));
}

TEST_CASE("Bracket match: the highlight does not need rainbow brackets", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"int run() {", "  return 1;", "}"}, "norainbow");
  e.apply_resize_for_test(100, 30);
  e.set_config_for_test("rainbow_brackets", "false");
  e.apply_config_live_for_test();

  e.scroll_cursor_to_for_test(0, 10);
  repaint(e);
  REQUIRE(is_match_cell(e, 0, 10));
  REQUIRE(is_match_cell(e, 2, 0));
}
