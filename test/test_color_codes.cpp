// The colour-literal scanner (src/features/color_codes.cpp).
//
// Ported from nvim-colorizer.lua, so these cases pin the rules that make the
// port behave like the original: which spellings count as colours, and -- just
// as important -- which near-misses must NOT match. A preview that lights up
// the wrong substrings is worse than none, because it misreports the value.
#include "features/color_codes.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace jot_color;

namespace
{
  std::vector<ColorSpan> scan(const std::string &line, Options opts = {})
  {
    return scan_line(line, -1, opts);
  }

  // The single span of `line`, requiring that exactly one was found.
  ColorSpan only(const std::string &line, Options opts = {})
  {
    const auto spans = scan(line, opts);
    REQUIRE(spans.size() == 1);
    return spans[0];
  }

  bool has_match(const std::string &line, Options opts = {})
  {
    return !scan(line, opts).empty();
  }

  std::string text_of(const std::string &line, const ColorSpan &s)
  {
    return line.substr((size_t)s.start, (size_t)s.len);
  }
} // namespace

TEST_CASE("Hex literals: every enabled width", "[jot][colorizer]")
{
  // #RGB doubles each nibble, so #f00 and #ff0000 are the same colour.
  const ColorSpan short_hex = only("color: #f00;");
  REQUIRE(short_hex.rgb == 0xFF0000u);
  REQUIRE(text_of("color: #f00;", short_hex) == "#f00");

  // #RGBA keeps the RGB and drops the alpha.
  const ColorSpan rgba = only("color: #f00f;");
  REQUIRE(rgba.rgb == 0xFF0000u);
  REQUIRE(rgba.len == 5);

  const ColorSpan full = only("color: #ff8800;");
  REQUIRE(full.rgb == 0xFF8800u);
  REQUIRE(full.len == 7);

  // Lowercase and uppercase digits both work.
  REQUIRE(only("x = #ABCDEF;").rgb == 0xABCDEFu);

  // #RRGGBBAA is off by default (upstream's hex.rrggbbaa default), and enabled
  // explicitly: the six RGB digits are what gets shown.
  REQUIRE_FALSE(has_match("color: #ff000080;"));
  Options with_alpha;
  with_alpha.hex8 = true;
  const ColorSpan eight = only("color: #ff000080;", with_alpha);
  REQUIRE(eight.rgb == 0xFF0000u);
  REQUIRE(eight.len == 9);
}

TEST_CASE("Hex near-misses are rejected", "[jot][colorizer]")
{
  // A run length that is not an enabled width is not a colour: this is what
  // keeps "#ffffff" from being read as "#fff" plus trailing junk.
  REQUIRE_FALSE(has_match("color: #ff;"));
  REQUIRE_FALSE(has_match("color: #fffff;"));
  REQUIRE_FALSE(has_match("color: #fffffff;"));

  // A longer run stays whole, so the six-digit form wins over the three.
  const ColorSpan six = only("color: #ffffff;");
  REQUIRE(six.len == 7);
  REQUIRE(six.rgb == 0xFFFFFFu);

  // Identifier characters after the digits mean it is an identifier, not a
  // colour (a hash-like id, a class name, a suffix).
  REQUIRE_FALSE(has_match("id: #ffffffz;"));
  REQUIRE_FALSE(has_match("id: #fff_bar;"));

  // 0x-prefixed numbers are a different (unported) parser's business.
  REQUIRE_FALSE(has_match("x = 0xabc123;"));

  // The token may not start inside a longer identifier.
  REQUIRE_FALSE(has_match("id#fff;"));
}

TEST_CASE("Named colours and their boundaries", "[jot][colorizer]")
{
  REQUIRE(only("color: red;").rgb == 0xFF0000u);
  REQUIRE(only("color: white;").rgb == 0xFFFFFFu);
  REQUIRE(only("border: LightBlue;").rgb == 0xADD8E6u);
  REQUIRE(only("color: RebeccaPurple;").rgb == 0x663399u);

  // Whole words only: a name glued into a longer identifier is not a colour.
  // This is the case upstream handles with extra_word_chars = "-".
  REQUIRE_FALSE(has_match("class=\"text-red-500\""));
  REQUIRE_FALSE(has_match("color: red_300;"));
  REQUIRE_FALSE(has_match("color: cardinal;"));
  REQUIRE_FALSE(has_match("my-red-thing"));
  REQUIRE_FALSE(has_match("notacolour"));

  // UPPERCASE is a separate switch, off by default, as upstream defaults it.
  REQUIRE_FALSE(has_match("color: RED;"));
  Options upper;
  upper.names_uppercase = true;
  REQUIRE(only("color: RED;", upper).rgb == 0xFF0000u);

  // Disabling the parser silences names but not hex.
  Options no_names;
  no_names.names = false;
  REQUIRE_FALSE(has_match("color: red;", no_names));
  REQUIRE(has_match("color: #ff0000;", no_names));
}

TEST_CASE("rgb() and rgba() functions", "[jot][colorizer]")
{
  REQUIRE(only("rgb(255, 0, 0)").rgb == 0xFF0000u);
  REQUIRE(only("rgb(255,0,0)").rgb == 0xFF0000u);
  // Modern space-separated syntax with a slash alpha.
  REQUIRE(only("rgb(255 0 0 / 50%)").rgb == 0xFF0000u);
  REQUIRE(only("rgb(255 128 0)").rgb == 0xFF8000u);
  // Percentages scale to the full range, and alpha is ignored.
  REQUIRE(only("rgba(100%, 50%, 0%, 0.25)").rgb == 0xFF8000u);
  // Out-of-range channels clamp instead of wrapping. (A negative channel is
  // rejected outright, as upstream does -- CSS itself does not allow one.)
  REQUIRE(only("rgb(300, 0, 0)").rgb == 0xFF0000u);
  REQUIRE_FALSE(has_match("rgb(300, -20, 0)"));
  // The reported span covers the whole call, including the closing paren.
  const std::string line = "color: rgb(255, 0, 0);";
  const ColorSpan span = only(line);
  REQUIRE(text_of(line, span) == "rgb(255, 0, 0)");

  // Malformed calls do not match: too few channels, no closing paren, a
  // different function name.
  REQUIRE_FALSE(has_match("rgb(1,2)"));
  REQUIRE_FALSE(has_match("rgb(1,2,3"));
  REQUIRE_FALSE(has_match("rgbx(1,2,3)"));
  REQUIRE_FALSE(has_match("myrgb(1,2,3)"));
}

TEST_CASE("hsl() and hsla() functions", "[jot][colorizer]")
{
  // The primaries, which are the values a wrong hue conversion would break.
  REQUIRE(only("hsl(0, 100%, 50%)").rgb == 0xFF0000u);
  REQUIRE(only("hsl(120, 100%, 50%)").rgb == 0x00FF00u);
  REQUIRE(only("hsl(240, 100%, 50%)").rgb == 0x0000FFu);
  // Angles with units: 180deg, 0.5turn, 3.14159rad and 200grad all agree.
  REQUIRE(only("hsl(180deg, 100%, 50%)").rgb == 0x00FFFFu);
  REQUIRE(only("hsl(0.5turn, 100%, 50%)").rgb == 0x00FFFFu);
  REQUIRE(only("hsl(200grad, 100%, 50%)").rgb == 0x00FFFFu);
  // Zero saturation is a grey at the requested lightness.
  REQUIRE(only("hsl(0, 0%, 50%)").rgb == 0x808080u);
  REQUIRE(only("hsl(0, 0%, 0%)").rgb == 0x000000u);
  REQUIRE(only("hsl(0, 0%, 100%)").rgb == 0xFFFFFFu);
  // Alpha is ignored, and the percentage signs are required for s/l.
  REQUIRE(only("hsla(0, 100%, 50%, 0.5)").rgb == 0xFF0000u);
  REQUIRE_FALSE(has_match("hsl(0, 100, 50)"));

  // The parser is switchable on its own.
  Options no_fns;
  no_fns.functions = false;
  REQUIRE_FALSE(has_match("hsl(0, 100%, 50%)", no_fns));
  REQUIRE_FALSE(has_match("rgb(255, 0, 0)", no_fns));
}

TEST_CASE("Several colours on one line, in order and without overlap", "[jot][colorizer]")
{
  const std::string line = "gradient: #ff0000 0%, red 50%, rgb(0, 0, 255) 100%;";
  const auto spans = scan(line);
  REQUIRE(spans.size() == 3);
  REQUIRE(spans[0].rgb == 0xFF0000u);
  REQUIRE(spans[1].rgb == 0xFF0000u);
  REQUIRE(spans[2].rgb == 0x0000FFu);
  for (size_t i = 1; i < spans.size(); i++)
  {
    REQUIRE(spans[i - 1].start + spans[i - 1].len <= spans[i].start);
  }
  REQUIRE(text_of(line, spans[1]) == "red");
  REQUIRE(text_of(line, spans[2]) == "rgb(0, 0, 255)");
}

TEST_CASE("The scan honours its byte window", "[jot][colorizer]")
{
  // A long line is only scanned over the visible window: a colour past the
  // limit is not reported, which is what keeps minified files cheap.
  std::string line = "x";
  line += std::string(200, ' ');
  line += "#ff0000";
  REQUIRE(scan_line(line, (int)line.size(), {}).size() == 1);
  REQUIRE(scan_line(line, 100, {}).empty());
  // A literal straddling the limit is not half-reported.
  const std::string edge = std::string(10, ' ') + "#ff0000";
  REQUIRE(scan_line(edge, 12, {}).empty());
  REQUIRE(scan_line(edge, 17, {}).size() == 1);
}

TEST_CASE("A scope mask restricts where a match may start", "[jot][colorizer]")
{
  // "only in strings and comments" is expressed as a per-byte mask.
  const std::string line = "#ff0000 plain #00ff00";
  std::vector<std::uint8_t> scope(line.size(), 0);
  for (size_t i = 0; i < 7; i++)
  {
    scope[i] = 1; // only the first literal is "inside a string"
  }
  const auto spans = scan_line(line, -1, {}, &scope);
  REQUIRE(spans.size() == 1);
  REQUIRE(spans[0].start == 0);
  REQUIRE(spans[0].rgb == 0xFF0000u);

  // With the mask pointing elsewhere, the other literal is the match.
  std::vector<std::uint8_t> other(line.size(), 0);
  for (size_t i = 14; i < line.size(); i++)
  {
    other[i] = 1;
  }
  const auto only_second = scan_line(line, -1, {}, &other);
  REQUIRE(only_second.size() == 1);
  REQUIRE(only_second[0].rgb == 0x00FF00u);
}

TEST_CASE("Contrast text colour on a filled swatch", "[jot][colorizer]")
{
  // Dark fills get white text, bright fills get black: this is what keeps the
  // preview legible instead of painting dark-on-dark.
  REQUIRE(contrast_text_color(0x000000u) == 0xFFFFFFu);
  REQUIRE(contrast_text_color(0x0000FFu) == 0xFFFFFFu);
  REQUIRE(contrast_text_color(0xFFFFFFu) == 0x000000u);
  REQUIRE(contrast_text_color(0xFFFF00u) == 0x000000u);
  // Mid-tones have to fall somewhere; assert the boundary is monotonic rather
  // than pinning a specific empirical value.
  REQUIRE(contrast_text_color(0xFF8800u) == 0x000000u);
  REQUIRE(contrast_text_color(0x880000u) == 0xFFFFFFu);
}

TEST_CASE("The span cache returns what a fresh scan would", "[jot][colorizer]")
{
  SpanCache cache;
  const std::string line = "a: #ff0000; b: red;";

  const auto &first = cache.spans_for(3, line, -1, {}, nullptr);
  REQUIRE(first.size() == 2);

  // Same content: the memo is reused and still equal to a fresh scan.
  const auto &again = cache.spans_for(3, line, -1, {}, nullptr);
  REQUIRE(again.size() == 2);
  REQUIRE(again[0].rgb == 0xFF0000u);

  // An edit invalidates by content, without anyone telling the cache.
  const std::string edited = "a: #00ff00; b: red;";
  const auto &after_edit = cache.spans_for(3, edited, -1, {}, nullptr);
  REQUIRE(after_edit.size() == 2);
  REQUIRE(after_edit[0].rgb == 0x00FF00u);

  // A different option set must not reuse the previous answer.
  Options no_names;
  no_names.names = false;
  const auto &restricted = cache.spans_for(3, edited, -1, no_names, nullptr);
  REQUIRE(restricted.size() == 1);

  // Different lines are independent.
  REQUIRE(cache.spans_for(4, "plain text", -1, {}, nullptr).empty());
}
