// The in-place edits: Alt+Backspace takes the word the caret is sitting on,
// Alt+X S and Alt+X B clear the inside of the string or the bracket pair the
// caret is in. All three answer the same question -- which span the caret means
// -- and all three act on it where it stands, so what is pinned here is that
// span and where the caret is left once the text is gone.
//
// The bracket walk is unit-tested on its own too: it is the part with the
// failure modes worth naming (a bracket inside a string is not one, a stray
// comment bracket is not one, and the innermost pair is the one that counts),
// and it needs no editor to exercise.
#include "bracket_text_object.h"
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  // A fresh editor per case: the action reads the caret and the buffer, and a
  // shared editor would carry another case's caret into this one.
  void seed_config_home()
  {
    char home[] = "/tmp/jot_replace_inside_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file on disk the editor can load, one line per string. Names are unique
  // per case: load_file focuses an already-open buffer instead of re-reading it.
  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_replace_inside_" + name + ".cpp";
    std::ofstream out(path);
    for (const auto &line : lines)
    {
      out << line << "\n";
    }
    out.close();
    e.load_file(path);
  }

  std::vector<std::string> lines_of(Editor &e)
  {
    std::vector<std::string> lines;
    for (int i = 0; i < e.buffer_for_test().line_count(); i++)
    {
      lines.push_back(e.buffer_for_test().line(i));
    }
    return lines;
  }

  // The caret is a buffer position; every case below names one, so the walk
  // reads the same way the screen does.
  BracketTextObject::Range enclosing(const std::vector<std::string> &lines, int line, int col)
  {
    return BracketTextObject::find_inner_range(lines, line, col, 500);
  }
} // namespace

TEST_CASE("Word delete: the whole word the caret is inside goes", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"alpha beta gamma"}, "inside");
  e.scroll_cursor_to_for_test(0, 8); // inside beta
  REQUIRE(e.delete_word_at_cursor_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"alpha  gamma"});
  REQUIRE(e.buffer_for_test().cursor.x == 6);
}

TEST_CASE("Word delete: a caret on the word's first or last cell still takes it", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"alpha beta gamma"}, "edges");
  e.scroll_cursor_to_for_test(0, 6); // the b of beta
  REQUIRE(e.delete_word_at_cursor_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"alpha  gamma"});

  // ...and the cell just past the word is not inside it: the caret there is on
  // the next word's edge, so the next word is what goes.
  e.scroll_cursor_to_for_test(0, 11); // the g of gamma
  REQUIRE(e.delete_word_at_cursor_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"alpha  "});
}

TEST_CASE("Word delete: off a word it eats backwards like Ctrl+Backspace", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"alpha   beta"}, "gap");
  // Between two words there is no span of the caret's own; making the key do
  // nothing there would read as a dead key, so it takes the run to the left.
  e.scroll_cursor_to_for_test(0, 7);
  REQUIRE(e.delete_word_at_cursor_for_test());
  // It eats back through the gap to the end of the word before it, which is
  // the run Ctrl+Backspace would take: "alpha" and the two spaces it crossed.
  REQUIRE(lines_of(e) == std::vector<std::string>{" beta"});
  REQUIRE(e.buffer_for_test().cursor.x == 0);
}

TEST_CASE("Word delete: a live selection goes first", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"alpha beta gamma"}, "selection");
  e.scroll_cursor_to_for_test(0, 11);
  REQUIRE(e.select_word_at_cursor()); // selects gamma
  REQUIRE(e.delete_word_at_cursor_for_test());
  // gamma was the marked span, so it is what goes -- not the word the caret
  // ended up in once the selection was made.
  REQUIRE(lines_of(e) == std::vector<std::string>{"alpha beta "});
}

TEST_CASE("Bracket inside: the innermost pair around the caret", "[jot]")
{
  const std::vector<std::string> line = {"call(alpha, beta)"};
  const auto outer = enclosing(line, 0, 8);
  REQUIRE(outer.found);
  REQUIRE(outer.open_col == 4);
  REQUIRE(outer.close_col == 16);

  // A pair inside the caret's pair is nearer, and it is the one the caret is in.
  const std::vector<std::string> nested = {"outer(inner(a))"};
  const auto inner = enclosing(nested, 0, 12);
  REQUIRE(inner.found);
  REQUIRE(inner.open_col == 11);
  REQUIRE(inner.close_col == 13);

  // The caret on either bracket is inside that pair: the opening one is the
  // pair the caret is on, the closing one is the pair the caret just reached.
  const auto on_open = enclosing(line, 0, 4);
  REQUIRE(on_open.found);
  REQUIRE(on_open.open_col == 4);
  const auto on_close = enclosing(line, 0, 16);
  REQUIRE(on_close.found);
  REQUIRE(on_close.open_col == 4);

  // Past the closing bracket is past the pair.
  REQUIRE_FALSE(enclosing(line, 0, 17).found);
  REQUIRE_FALSE(enclosing({"plain text"}, 0, 5).found);
}

TEST_CASE("Bracket inside: brackets in strings and comments are not pairs", "[jot]")
{
  // The ( in the literal belongs to the string; the pair the caret is in is the
  // call's, so the whole literal is the inside.
  const std::vector<std::string> quoted = {"log(\"(\")"};
  const auto in_quote = enclosing(quoted, 0, 5);
  REQUIRE(in_quote.found);
  REQUIRE(in_quote.open_col == 3);
  REQUIRE(in_quote.close_col == 7);

  // A bracket pair behind a // is not one either: the call's own pair is what
  // encloses the caret.
  const std::vector<std::string> commented = {"int x = f(1); // (left"};
  const auto in_code = enclosing(commented, 0, 10);
  REQUIRE(in_code.found);
  REQUIRE(in_code.open_col == 9);
  REQUIRE(in_code.close_col == 11);

  // An apostrophe in a comment must not turn the rest of the file into a
  // literal, which would swallow the closing bracket below.
  const std::vector<std::string> apostrophe = {"call( // don't", "  arg)"};
  const auto across = enclosing(apostrophe, 1, 3);
  REQUIRE(across.found);
  REQUIRE(across.open_line == 0);
  REQUIRE(across.open_col == 4);
  REQUIRE(across.close_line == 1);
  REQUIRE(across.close_col == 5);
}

TEST_CASE("Bracket inside: a pair that closes further down is still the caret's", "[jot]")
{
  const std::vector<std::string> lines = {"call(", "  alpha,", "  beta)"};
  const auto range = enclosing(lines, 1, 3);
  REQUIRE(range.found);
  REQUIRE(range.open_line == 0);
  REQUIRE(range.open_col == 4);
  REQUIRE(range.close_line == 2);
  REQUIRE(range.close_col == 6);
}

TEST_CASE("Change inside a pair: the interior is cleared and the caret takes its place", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"call(alpha, beta);"}, "bracket_clear");
  e.scroll_cursor_to_for_test(0, 8);
  REQUIRE(e.change_inside_bracket_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"call();"});
  REQUIRE(e.buffer_for_test().cursor.x == 5);
  REQUIRE(e.buffer_for_test().cursor.y == 0);
}

TEST_CASE("Change inside a pair: a multi-line call collapses to the cleared call", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"call(", "  alpha,", "  beta);", "int tail = 1;"}, "bracket_multiline");
  e.scroll_cursor_to_for_test(1, 3);
  REQUIRE(e.change_inside_bracket_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"call();", "int tail = 1;"});
  REQUIRE(e.buffer_for_test().cursor.x == 5);
  REQUIRE(e.buffer_for_test().cursor.y == 0);
}

TEST_CASE("Change inside a pair: an empty pair reports instead of eating a line", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"call();"}, "bracket_empty");
  e.scroll_cursor_to_for_test(0, 5);
  REQUIRE_FALSE(e.change_inside_bracket_for_test());
  REQUIRE(e.message_for_test() == "Nothing inside the brackets");
  REQUIRE(lines_of(e) == std::vector<std::string>{"call();"});
}

TEST_CASE("Change inside a string: the literal the caret is in is cleared", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"x = \"hello\" + 'w';"}, "quote_clear");
  e.scroll_cursor_to_for_test(0, 8); // inside the double-quoted literal
  REQUIRE(e.change_inside_any_quote_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"x = \"\" + 'w';"});
  REQUIRE(e.buffer_for_test().cursor.x == 5);

  e.scroll_cursor_to_for_test(0, 10); // inside the single-quoted one
  REQUIRE(e.change_inside_any_quote_for_test());
  REQUIRE(lines_of(e) == std::vector<std::string>{"x = \"\" + '';"});
}

TEST_CASE("Change inside a string: a caret outside every literal leaves it alone", "[jot]")
{
  seed_config_home();
  Editor e;
  load_lines(e, {"x = \"hello\";"}, "quote_outside");
  // The caret sits past the closing quote. The nearest pair is the literal it
  // just left, and clearing that would be a deletion the caret never pointed
  // at, so it reports instead.
  e.scroll_cursor_to_for_test(0, 11);
  REQUIRE_FALSE(e.change_inside_any_quote_for_test());
  REQUIRE(e.message_for_test() == "No string pair found");
  REQUIRE(lines_of(e) == std::vector<std::string>{"x = \"hello\";"});
}
