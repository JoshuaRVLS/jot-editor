// Auto-close: what a typed pair character does (src/features/autoclose.*,
// driven from Editor::insert_char in src/edit/edit.cpp).
//
// Two halves. The rules are pure functions, so each one is asserted directly:
// whether a character pairs at all, and whether it steps over the closer
// already under the caret. The editor cases then pin what the buffer and the
// carets actually end up as -- including at several carets at once, which used
// to bypass the rules entirely.

#include "editor.h"
#include "text/autoclose.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_autoclose_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void type_text(const std::string &text)
  {
    Editor &e = probe_editor();
    for (char c : text)
    {
      e.host().core.insert_char_at_carets(c);
    }
  }

  void seed(const std::string &text, int line = 0, int col = 0)
  {
    Editor &e = probe_editor();
    auto &core = e.host().core;
    core.set_buffer_content(text);
    core.clear_extra_carets();
    core.set_cursor(line, col);
  }

  // A second caret at an exact position. Alt+click and Ctrl+D both reach the
  // same place, but they move the main cursor on the way and a multi-selection
  // delete does not carry the other carets with it, so the position is set
  // directly: what is under test is what a caret does with it.
  void add_caret(int line, int col)
  {
    auto &buf = probe_editor().buffer_for_test();
    const Cursor point{col, line};
    buf.extra_carets.push_back(Selection{point, point, false});
  }
} // namespace

// A pair decision as the editor asks for it: a character at a position on a
// line.
static bool pairs_at(char c, const std::string &line, int pos)
{
  return AutoClose::should_insert_pair(c, line, pos);
}

TEST_CASE("Auto-close pairs a bracket unless an unclaimed closer is under the caret",
          "[jot][autoclose]")
{
  REQUIRE(pairs_at('(', "xy", 1));
  REQUIRE(pairs_at('[', "", 0));
  // An opener to the left is still waiting on the closer at the caret, so that
  // closer is its partner and typing another opener nests inside it.
  REQUIRE(pairs_at('(', "()", 1));
  REQUIRE(pairs_at('{', "{}", 1));
  REQUIRE(pairs_at('(', "(a)", 2));
  // Nothing is waiting on the closer at the caret, so the typed opener takes it
  // rather than leaving a spare beside it.
  REQUIRE_FALSE(pairs_at('(', "a)b", 1));
  REQUIRE_FALSE(pairs_at('(', "(a))", 3));
  // A closer of another kind is not a claim on this one.
  REQUIRE_FALSE(pairs_at('(', "{)", 1));
}

TEST_CASE("Auto-close leaves a quote beside a word alone", "[jot][autoclose]")
{
  // A word character after the quote: the string is already open, so this one
  // closes it.
  REQUIRE_FALSE(pairs_at('"', " a", 1));
  REQUIRE_FALSE(pairs_at('\'', " z", 1));
  // A word character before it: an apostrophe (`don't`) or a quote closing
  // text (`word"`), neither of which wants a partner.
  REQUIRE_FALSE(pairs_at('\'', "don't", 3));
  REQUIRE_FALSE(pairs_at('"', "note", 4));
  REQUIRE_FALSE(pairs_at('\'', "a'", 1));
  // An escaped quote is literal text.
  REQUIRE_FALSE(pairs_at('"', "a\\", 2));
  // An even number of backslashes before it does not escape it.
  REQUIRE(pairs_at('"', "a\\\\", 3));
  // Standing on its own, a quote opens a pair.
  REQUIRE(pairs_at('\'', "  ", 1));
  REQUIRE(pairs_at('"', "(", 1));
  REQUIRE(pairs_at('"', "", 0));
}

TEST_CASE("Auto-close never pairs a quote inside an open string", "[jot][autoclose]")
{
  // The closer follows a bracket rather than a word, which the neighbour rules
  // alone would read as an opening quote and answer with a second pair.
  REQUIRE(AutoClose::open_quote_at("f\"{x}", 5) == '"');
  REQUIRE_FALSE(pairs_at('"', "f\"{x}", 5));
  REQUIRE_FALSE(pairs_at('\'', "f'{x}", 5));
  // A quote of the other kind inside a string is text, not a nesting string.
  REQUIRE(AutoClose::open_quote_at("f\"{x}", 5) == '"');
  REQUIRE_FALSE(pairs_at('\'', "\"it's", 5));
  REQUIRE(AutoClose::open_quote_at("\"it's", 5) == '"');
  // A string that was closed is closed, so the next quote opens its own pair.
  REQUIRE(AutoClose::open_quote_at("f\"{x}\"", 6) == '\0');
  REQUIRE(pairs_at('"', "f\"{x}\"", 6));
  // An escaped quote cannot close the string it is inside.
  REQUIRE(AutoClose::open_quote_at("\"a\\\"", 4) == '"');
  // Nor does it open one on a line that has none.
  REQUIRE(AutoClose::open_quote_at("a\\\"b", 4) == '\0');
}

TEST_CASE("Auto-close reads escape parity, not just the character before",
          "[jot][autoclose]")
{
  // The character at `pos` is escaped by an odd run of backslashes before it.
  REQUIRE(AutoClose::is_escaped("\\", 1));
  REQUIRE_FALSE(AutoClose::is_escaped("\\\\", 2));
  REQUIRE(AutoClose::is_escaped("a\\", 2));
  REQUIRE_FALSE(AutoClose::is_escaped("a\\\\", 3));
  REQUIRE_FALSE(AutoClose::is_escaped("ab", 1));
  // So a quote after an even run is the closer to step over, and one after an
  // odd run is content inside the string.
  REQUIRE(AutoClose::should_skip_closing('"', "a\\\\\"", 3));
  REQUIRE_FALSE(AutoClose::should_skip_closing('"', "a\\\"", 2));
}

TEST_CASE("Auto-close only steps over a closer, never over an opener", "[jot][autoclose]")
{
  REQUIRE(AutoClose::should_skip_closing(')', ")", 0));
  REQUIRE(AutoClose::should_skip_closing('"', "\"", 0));
  REQUIRE_FALSE(AutoClose::should_skip_closing('(', "(", 0));
  REQUIRE_FALSE(AutoClose::should_skip_closing(')', "x", 0));
  REQUIRE_FALSE(AutoClose::should_skip_closing(')', ")", 1));
  // The quote under the caret is escaped, so it is content inside a string.
  REQUIRE_FALSE(AutoClose::should_skip_closing('"', "\\\"", 1));
}

TEST_CASE("Typing a bracket leaves the caret inside its pair", "[jot][autoclose]")
{
  seed("abc", 0, 1);
  type_text("(");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a()bc");
  REQUIRE(core.cursor() == std::make_pair(0, 2));
}

TEST_CASE("Typing an opener inside a pair nests instead of eating the closer",
          "[jot][autoclose]")
{
  // The `)` under the caret is the partner of the `(` in front of it, so the
  // typed `(` opens a pair of its own and the outer closer stays put.
  seed("", 0, 0);
  type_text("((");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "(())");
  REQUIRE(core.cursor() == std::make_pair(0, 2));

  // And the outer closer is still there to step over.
  type_text(")");
  REQUIRE(core.buffer_content() == "(())");
  REQUIRE(core.cursor() == std::make_pair(0, 3));
}

TEST_CASE("An opener adopts a closer nothing is waiting on", "[jot][autoclose]")
{
  seed("a)b", 0, 1);
  type_text("(");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a()b");
  REQUIRE(core.cursor() == std::make_pair(0, 2));
}

TEST_CASE("Every caret nests inside the pair it sits in", "[jot][autoclose]")
{
  seed("() ()", 0, 1);
  add_caret(0, 4);
  type_text("(");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "(()) (())");
  REQUIRE(core.cursor().second == 2);
  REQUIRE(probe_editor().buffer_for_test().extra_carets[0].end.x == 7);
}

TEST_CASE("Typing a closer steps over the one already there", "[jot][autoclose]")
{
  seed("a()bc", 0, 2);
  type_text(")");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a()bc");
  REQUIRE(core.cursor() == std::make_pair(0, 3));
}

TEST_CASE("Backspace between an auto-closed pair takes both", "[jot][autoclose]")
{
  seed("abc", 0, 1);
  type_text("(");
  probe_editor().delete_char_for_test(false);

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "abc");
  REQUIRE(core.cursor() == std::make_pair(0, 1));
}

TEST_CASE("An apostrophe inside a word does not open a pair", "[jot][autoclose]")
{
  seed("don", 0, 3);
  type_text("'");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "don'");
  REQUIRE(core.cursor() == std::make_pair(0, 4));
}

TEST_CASE("A quote before a word does not open a pair", "[jot][autoclose]")
{
  seed("abcd", 0, 0);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "\"abcd");
  REQUIRE(core.cursor() == std::make_pair(0, 1));
}

TEST_CASE("A quote after a word does not open a pair", "[jot][autoclose]")
{
  seed("note", 0, 4);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "note\"");
  REQUIRE(core.cursor() == std::make_pair(0, 5));
}

TEST_CASE("A closing quote after a brace does not bring a partner", "[jot][autoclose]")
{
  // `f"{x}` is a string waiting for its closer. The character before the
  // caret is a brace, so the neighbour rules alone would read the closer as an
  // opening quote and leave a spare `"` behind.
  seed("f\"{x}", 0, 5);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "f\"{x}\"");
  REQUIRE(core.cursor() == std::make_pair(0, 6));
}

TEST_CASE("A closing single quote after a brace does not bring a partner", "[jot][autoclose]")
{
  seed("f'{x}", 0, 5);
  type_text("'");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "f'{x}'");
  REQUIRE(core.cursor() == std::make_pair(0, 6));
}

TEST_CASE("A quote typed inside a string is text, not a nested pair", "[jot][autoclose]")
{
  // The apostrophe is content of the open double-quoted string, so pairing it
  // would leave a quote to delete.
  seed("\"hello ", 0, 7);
  type_text("'");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "\"hello '");
  REQUIRE(core.cursor() == std::make_pair(0, 8));
}

TEST_CASE("A quote after a string that was closed still opens its own pair",
          "[jot][autoclose]")
{
  seed("f\"{x}\"", 0, 6);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "f\"{x}\"\"\"");
  REQUIRE(core.cursor() == std::make_pair(0, 7));
}

TEST_CASE("An escaped quote does not open a pair", "[jot][autoclose]")
{
  seed("a\\", 0, 2);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a\\\"");
  REQUIRE(core.cursor() == std::make_pair(0, 3));
}

TEST_CASE("An escaped quote is not stepped over", "[jot][autoclose]")
{
  seed("a\\\"", 0, 2);
  type_text("\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a\\\"\"");
  REQUIRE(core.cursor() == std::make_pair(0, 3));
}

TEST_CASE("Typing a quote over paired quotes steps through them", "[jot][autoclose]")
{
  seed("", 0, 0);
  // `"` opens ``""``, and the next `"` steps over its partner rather than
  // adding a third.
  type_text("\"\"");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "\"\"");
  REQUIRE(core.cursor() == std::make_pair(0, 2));
}

TEST_CASE("Selecting text and typing a bracket wraps it", "[jot][autoclose]")
{
  seed("foo bar");
  auto &core = probe_editor().host().core;
  REQUIRE(core.select_next_occurrence());
  type_text("(");

  REQUIRE(core.buffer_content() == "(foo) bar");
}

TEST_CASE("Every caret gets the pair it types", "[jot][autoclose]")
{
  // Two carets on one line, the second to the right of the first: each opens
  // its own pair and ends up inside it.
  seed("ab ab", 0, 0);
  add_caret(0, 3);
  type_text("(");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "()ab ()ab");
  REQUIRE(core.cursor().second == 1);
  REQUIRE(probe_editor().buffer_for_test().extra_carets[0].end.x == 6);
}

TEST_CASE("Every caret gets its pair on its own line", "[jot][autoclose]")
{
  seed("ab\nab", 0, 0);
  add_caret(1, 0);
  type_text("(");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "()ab\n()ab");
  REQUIRE(core.cursor().second == 1);
  REQUIRE(probe_editor().buffer_for_test().extra_carets[0].end == (Cursor{1, 1}));
}

TEST_CASE("Every caret steps over the closer it types", "[jot][autoclose]")
{
  // Each caret sits before its own `)`: both step over it, and neither inserts
  // a second one.
  seed("a) b)", 0, 1);
  add_caret(0, 4);
  type_text(")");

  auto &core = probe_editor().host().core;
  REQUIRE(core.buffer_content() == "a) b)");
  REQUIRE(core.cursor().second == 2);
  REQUIRE(probe_editor().buffer_for_test().extra_carets[0].end.x == 5);
}
