// The colours in a command's output (:compile).
//
// A build tool that colourises writes SGR sequences around its own text. A log
// buffer has no emulator, so the sequences are taken out and reported as spans
// instead: the cases here pin the spellings the tools actually use (the 16
// base colours, the `;5;N` and `;2;R;G;B` extended forms and their `:` variant,
// bold, faint, underline and the resets), the sequences that are dropped
// because a log cannot honour them, and the one thing a row-by-row parser has
// to get right -- a colour set once stays set until something resets it, even
// across rows.
#include "text/ansi_spans.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace
{
  // One row, through its own parser: every case starts from the terminal's
  // defaults rather than from whatever the case before it left set.
  ansi_spans::Row parse_one(const std::string &raw)
  {
    ansi_spans::Parser parser;
    return parser.feed(raw);
  }
} // namespace

TEST_CASE("plain text has no colours and keeps its bytes", "[compile][ansi]")
{
  const ansi_spans::Row row = parse_one("built 3 files in 1.2s\ttab kept");
  CHECK(row.text == "built 3 files in 1.2s\ttab kept");
  CHECK(row.spans.empty());
}

TEST_CASE("an SGR sequence colours the text it wraps", "[compile][ansi]")
{
  const ansi_spans::Row row = parse_one("\x1b[31mred\x1b[0m plain");
  CHECK(row.text == "red plain");
  REQUIRE(row.spans.size() == 1);
  CHECK(row.spans[0].col == 0);
  CHECK(row.spans[0].width == 3);
  CHECK(row.spans[0].fg.set);
  CHECK(row.spans[0].fg.index == 1); // ANSI red
  CHECK_FALSE(row.spans[0].bg.set);
}

TEST_CASE("the extended colour forms are read, both spellings", "[compile][ansi]")
{
  // 38;5;N: a palette entry the terminal resolves.
  const ansi_spans::Row indexed = parse_one("\x1b[38;5;196mR\x1b[0m");
  REQUIRE(indexed.spans.size() == 1);
  CHECK(indexed.spans[0].fg.index == 196);

  // 38:5:N is the same code with the other separator.
  const ansi_spans::Row colon = parse_one("\x1b[38:5:196mR\x1b[0m");
  REQUIRE(colon.spans.size() == 1);
  CHECK(colon.spans[0].fg.index == 196);

  // 48;2;R;G;B: an exact colour on the background.
  const ansi_spans::Row exact = parse_one("\x1b[48;2;0;0;255mB\x1b[0m");
  REQUIRE(exact.spans.size() == 1);
  CHECK(exact.spans[0].bg.set);
  CHECK(exact.spans[0].bg.index == -1);
  CHECK(exact.spans[0].bg.b == 255);

  // The colon form carries an empty colour-space field between the mode and the
  // channels (`38:2::r:g:b`), which must not be read as a channel.
  const ansi_spans::Row colour_space = parse_one("\x1b[38:2::1:2:3mX\x1b[0m");
  REQUIRE(colour_space.spans.size() == 1);
  CHECK(colour_space.spans[0].fg.r == 1);
  CHECK(colour_space.spans[0].fg.g == 2);
  CHECK(colour_space.spans[0].fg.b == 3);
}

TEST_CASE("bold brightens one of the eight base colours", "[compile][ansi]")
{
  // `1;31` and `31;1` mean the same thing to a terminal, so they have to mean
  // the same thing here.
  const ansi_spans::Row first = parse_one("\x1b[1;31mX\x1b[0m");
  REQUIRE(first.spans.size() == 1);
  CHECK(first.spans[0].fg.index == 9);

  const ansi_spans::Row second = parse_one("\x1b[31;1mX\x1b[0m");
  REQUIRE(second.spans.size() == 1);
  CHECK(second.spans[0].fg.index == 9);

  // The bright eight (90-97) are already bright; bold leaves them alone.
  const ansi_spans::Row bright = parse_one("\x1b[1;91mX\x1b[0m");
  REQUIRE(bright.spans.size() == 1);
  CHECK(bright.spans[0].fg.index == 9);

  // And a bright colour is not promoted a second time.
  const ansi_spans::Row twice = parse_one("\x1b[1m\x1b[91mX\x1b[0m");
  REQUIRE(twice.spans.size() == 1);
  CHECK(twice.spans[0].fg.index == 9);
}

TEST_CASE("faint and underline are attributes, not colours", "[compile][ansi]")
{
  const ansi_spans::Row row = parse_one("\x1b[2;4;33mnote\x1b[22;24m");
  CHECK(row.text == "note");
  REQUIRE(row.spans.size() == 1);
  CHECK(row.spans[0].fg.index == 3);
  CHECK(row.spans[0].dim);
  CHECK(row.spans[0].underline);
}

TEST_CASE("a colour set once holds until something resets it", "[compile][ansi]")
{
  ansi_spans::Parser parser;
  const ansi_spans::Row first = parser.feed("\x1b[32mgreen starts here");
  REQUIRE(first.spans.size() == 1);
  CHECK(first.spans[0].fg.index == 2);

  // The next row is a fresh line but the same terminal: the colour is still on.
  const ansi_spans::Row second = parser.feed("green carries on");
  REQUIRE(second.spans.size() == 1);
  CHECK(second.spans[0].fg.index == 2);
  CHECK(second.spans[0].col == 0);

  // 39 is the default foreground, and a run starts from the defaults again.
  const ansi_spans::Row reset = parser.feed("\x1b[39mplain now");
  CHECK(reset.spans.empty());

  parser.feed("\x1b[35mmagenta");
  parser.reset();
  const ansi_spans::Row after_reset = parser.feed("default again");
  CHECK(after_reset.spans.empty());
}

TEST_CASE("a row can change colour part-way through", "[compile][ansi]")
{
  const ansi_spans::Row row = parse_one("\x1b[31merror\x1b[0m: \x1b[1;36mfixed\x1b[0m");
  CHECK(row.text == "error: fixed");
  REQUIRE(row.spans.size() == 2);
  CHECK(row.spans[0].col == 0);
  CHECK(row.spans[0].width == 5);
  CHECK(row.spans[0].fg.index == 1);
  CHECK(row.spans[1].col == 7);
  CHECK(row.spans[1].width == 5);
  CHECK(row.spans[1].fg.index == 14); // bold cyan
}

TEST_CASE("sequences a log cannot honour are dropped, not printed", "[compile][ansi]")
{
  // An erase and a cursor move: there is no screen here to erase or move on.
  const ansi_spans::Row erase = parse_one("before\x1b[2K\x1b[1Aafter");
  CHECK(erase.text == "beforeafter");

  // A window title, ended by BEL and by the two-byte ST.
  CHECK(parse_one("\x1b]0;a title\aafter").text == "after");
  CHECK(parse_one("\x1b]8;;http://x\x1b\\link").text == "link");

  // Charset selection is three bytes (`ESC ( 0`), and the byte after it is the
  // text again.
  CHECK(parse_one("\x1b(0q\x1b(B").text == "q");

  // Control characters other than a tab are not text either.
  CHECK(parse_one("\abell\bX").text == "bellX");
  CHECK(parse_one("a\tb").text == "a\tb");
}
