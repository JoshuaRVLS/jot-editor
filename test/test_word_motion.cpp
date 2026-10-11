// Moving through a file: where the caret lands when moving by word, which
// chords ask for it, and what the reading chords (file ends, viewport scroll)
// do to the caret and the view.
//
// The motion is the one a reader's eye makes, not the one a tokenizer would: an
// identifier is several stops rather than one (`fooBar` stops at the capital,
// `snake_case_name` at each name), the end of a line is a separator rather than
// a wall (the motion crosses to the next line's first word, blank lines and
// indentation included), and Ctrl+Left / Ctrl+Right are the chords for it,
// extending with Shift. Those three are what make moving around code fast, so
// each has a case here, and the chords are driven through the real modeless
// dispatch rather than through the motion, so a rebinding cannot pass unnoticed.
#include "core/editor.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_word_motion_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A file taller than any pane a headless editor gives a case, so a viewport
  // scroll has somewhere to go (the scroll scenes are about the view moving at
  // all, and a short file clamps a one-line step to zero).
  std::vector<std::string> tall_file()
  {
    std::vector<std::string> lines;
    for (int i = 0; i < 40; i++)
    {
      lines.push_back("line_" + std::to_string(i));
    }
    return lines;
  }

  // One buffer with the caret where the case put it. The motions are read back
  // through the caret's own coordinates, which is what the reader sees move.
  struct Lines
  {
    Editor &e = probe_editor();

    void place(const std::vector<std::string> &text, int x, int y)
    {
      FileBuffer &buf = e.buffer_for_test();
      buf.lines = text;
      if (buf.lines.empty())
      {
        buf.lines.push_back("");
      }
      buf.cursor = {x, y};
      buf.preferred_x = x;
      buf.selection = {{0, 0}, {0, 0}, false};
      buf.extra_carets.clear();
      buf.scroll_offset = 0;
      buf.scroll_x = 0;
    }

    std::pair<int, int> caret() const
    {
      const FileBuffer &buf = e.buffer_for_test();
      return {buf.cursor.x, buf.cursor.y};
    }
  };
} // namespace

TEST_CASE("A word motion stops at humps, not only at separators", "[jot][motion]")
{
  Lines t;

  // `fooBarBaz` is three stops, not one.
  t.place({"fooBarBaz"}, 0, 0);
  for (int expected : {3, 6, 9})
  {
    CAPTURE(expected);
    t.e.move_word_forward_for_test();
    REQUIRE(t.caret() == std::make_pair(expected, 0));
  }

  // And back: each stop is the start of the hump it walked over, so the motion
  // is the forward one's inverse, and the start of the buffer stops it. The
  // caret sits past the line's end to also pin the clamp: 10 becomes 9, and the
  // walk back from there still lands on `Bar`'s hump rather than its last cell.
  t.place({"fooBarBaz"}, 10, 0);
  for (int expected : {6, 3, 0, 0, 0})
  {
    CAPTURE(expected);
    t.e.move_word_backward_for_test();
    REQUIRE(t.caret() == std::make_pair(expected, 0));
  }

  // Underscores are crossed rather than landed on: `snake_case_name` walks name
  // by name.
  t.place({"snake_case_name"}, 0, 0);
  for (int expected : {6, 11, 15})
  {
    CAPTURE(expected);
    t.e.move_word_forward_for_test();
    REQUIRE(t.caret() == std::make_pair(expected, 0));
  }

  // A digit run is a stop of its own, in both directions.
  t.place({"x1y2"}, 0, 0);
  for (int expected : {1, 2, 3, 4})
  {
    CAPTURE(expected);
    t.e.move_word_forward_for_test();
    REQUIRE(t.caret() == std::make_pair(expected, 0));
  }

  // A caret on a separator moves to the word after it.
  t.place({"call(a, b)"}, 4, 0); // on the '('
  t.e.move_word_forward_for_test();
  REQUIRE(t.caret() == std::make_pair(5, 0));
  t.e.move_word_forward_for_test();
  REQUIRE(t.caret() == std::make_pair(8, 0));
}

TEST_CASE("A word motion crosses lines, blank ones included", "[jot][motion]")
{
  // From the last word of a line the motion lands on the next line's first
  // word, skipping the blank line and the indentation between them.
  Lines t;
  t.place({"first second", "", "   third"}, 6, 0); // inside "second"
  t.e.move_word_forward_for_test();
  REQUIRE(t.caret() == std::make_pair(3, 2));
  t.e.move_word_forward_for_test();
  REQUIRE(t.caret() == std::make_pair(8, 2)); // the end of the buffer

  // Backwards: from the first character of a line the motion reaches the last
  // word of the line above, and stops at the top of the buffer.
  t.place({"first second", "", "third"}, 0, 2);
  t.e.move_word_backward_for_test();
  REQUIRE(t.caret() == std::make_pair(6, 0));
  t.e.move_word_backward_for_test();
  REQUIRE(t.caret() == std::make_pair(0, 0));
  t.e.move_word_backward_for_test();
  REQUIRE(t.caret() == std::make_pair(0, 0));
}

TEST_CASE("Ctrl+Left and Ctrl+Right move by word, Shift extends", "[jot][motion]")
{
  // The chord is claimed from the bare arrows, which used to take it as a
  // one-cell step.
  Lines t;
  t.place({"alpha beta gamma"}, 0, 0);
  t.e.modeless_input_for_test(1010, true); // Ctrl+Right
  REQUIRE(t.caret() == std::make_pair(6, 0));

  // Ctrl+Shift+Right extends the selection instead of only moving: the anchor
  // stays where it was and the far end walks.
  t.place({"alpha beta gamma"}, 0, 0);
  t.e.modeless_input_for_test(1010, true, true);
  REQUIRE(t.caret() == std::make_pair(6, 0));
  const FileBuffer &buf = t.e.buffer_for_test();
  REQUIRE(buf.selection.active);
  REQUIRE(buf.selection.start.x == 0);
  REQUIRE(buf.selection.start.y == 0);
  REQUIRE(buf.selection.end.x == 6);
  REQUIRE(buf.selection.end.y == 0);

  t.e.modeless_input_for_test(1011, true); // Ctrl+Left
  REQUIRE(t.caret() == std::make_pair(0, 0));

  // Without Ctrl the arrows are the plain one-cell step they always were.
  t.place({"alpha beta gamma"}, 0, 0);
  t.e.modeless_input_for_test(1010);
  REQUIRE(t.caret() == std::make_pair(1, 0));
}

TEST_CASE("Ctrl+Home and Ctrl+End reach the file's ends", "[jot][motion]")
{
  Lines t;
  t.place({"alpha", "beta", "gamma", "delta"}, 3, 2); // inside "gamma"
  t.e.modeless_input_for_test(1012, true);               // Ctrl+Home
  REQUIRE(t.caret() == std::make_pair(0, 0));
  t.e.modeless_input_for_test(1013, true); // Ctrl+End
  REQUIRE(t.caret() == std::make_pair(5, 3));

  // Shift extends rather than jumps: the anchor holds, the moving end walks.
  t.place({"alpha", "beta", "gamma", "delta"}, 2, 1);
  t.e.modeless_input_for_test(1013, true, true); // Ctrl+Shift+End
  REQUIRE(t.caret() == std::make_pair(5, 3));
  const FileBuffer &buf = t.e.buffer_for_test();
  REQUIRE(buf.selection.active);
  REQUIRE(buf.selection.start.x == 2);
  REQUIRE(buf.selection.start.y == 1);
  REQUIRE(buf.selection.end.x == 5);
  REQUIRE(buf.selection.end.y == 3);
}

TEST_CASE("Ctrl+Up and Ctrl+Down scroll the view, not the caret", "[jot][motion]")
{
  // Reading a file without losing the place: the viewport moves a line at a
  // time and the caret stays where it was -- the wheel's gesture, on the keys.
  Lines t;
  t.place(tall_file(), 0, 0);

  t.e.modeless_input_for_test(1009, true); // Ctrl+Down
  REQUIRE(t.e.buffer_for_test().scroll_offset == 1);
  REQUIRE(t.caret() == std::make_pair(0, 0));

  t.e.modeless_input_for_test(1008, true); // Ctrl+Up
  REQUIRE(t.e.buffer_for_test().scroll_offset == 0);

  // At the top of the file there is nothing further to scroll: the chord is a
  // no-op rather than a jump of the caret.
  t.e.modeless_input_for_test(1008, true);
  REQUIRE(t.e.buffer_for_test().scroll_offset == 0);
  REQUIRE(t.caret() == std::make_pair(0, 0));
}

TEST_CASE("The docs' navigation chords survive the dispatcher", "[jot][motion]")
{
  // The cases above drive the modeless handler, which is where the motions
  // live; these go through handle_input, the entry a terminal key actually
  // takes. What can swallow a chord sits in front of the handler -- the
  // pane-resize block claims Ctrl+Arrow, the pane-focus block claims
  // Alt+H/J/K/L -- so a motion reachable only from the handler is a motion no
  // keypress can make. Both were exactly that before these chords moved.
  Lines t;
  const std::string line = "int alpha_beta = fooBarBaz + 1;";

  t.place({line}, 0, 0);
  t.e.editor_input_for_test(1010, true); // Ctrl+Right: the word motion
  REQUIRE(t.caret() == std::make_pair(4, 0));
  t.e.editor_input_for_test(1010, true); // ... and the hump inside it
  REQUIRE(t.caret() == std::make_pair(10, 0));

  // Ctrl+Up / Ctrl+Down are the reading pair. The resize chord they used to be
  // still exists, one modifier over, and reports rather than moving the caret
  // when the pane has nothing to grow into.
  t.place(tall_file(), 0, 0);
  t.e.editor_input_for_test(1009, true); // Ctrl+Down
  REQUIRE(t.e.buffer_for_test().scroll_offset == 1);
  REQUIRE(t.caret() == std::make_pair(0, 0));
  t.e.editor_input_for_test(1010, true, true); // Ctrl+Shift+Right
  CAPTURE(t.e.message_for_test());
  REQUIRE(t.caret() == std::make_pair(0, 0));
  REQUIRE(t.e.message_for_test().find("Resize") != std::string::npos);

  // Alt+H / Alt+L are the other documented word chords, and reach the motion
  // when nothing in that direction can take the focus (one pane, no sidebar).
  t.place({line}, 0, 0);
  t.e.editor_input_for_test('l', false, false, true); // Alt+L
  REQUIRE(t.caret() == std::make_pair(4, 0));
  t.e.editor_input_for_test('h', false, false, true); // Alt+H
  REQUIRE(t.caret() == std::make_pair(0, 0));
}
