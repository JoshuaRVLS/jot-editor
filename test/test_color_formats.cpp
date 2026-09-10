// The colour-preview parsers added after the first pass: the rest of the hex
// family, terminal codes, Tailwind, xcolor, and the CSS function family beyond
// rgb()/hsl().
//
// Each case is written the way the parser is wired into the scanner (same
// Options flags), so a parser that is implemented but not reachable still fails
// here.
#include "features/color_codes.h"
#include "features/color_functions.h"
#include "features/color_terminal_codes.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace jot_color;

namespace
{
  std::vector<ColorSpan> scan(const std::string &line, Options opts)
  {
    return scan_line(line, -1, opts, nullptr, nullptr);
  }

  // The single span of `line` under `opts`, requiring exactly one.
  ColorSpan only(const std::string &line, Options opts)
  {
    const auto spans = scan(line, opts);
    REQUIRE(spans.size() == 1);
    return spans[0];
  }

  std::string text_of(const std::string &line, const ColorSpan &s)
  {
    return line.substr((size_t)s.start, (size_t)s.len);
  }

  // Options with one parser enabled on top of the always-on hex/name defaults.
  struct With
  {
    static Options qml()
    {
      Options o;
      o.hex_aarrggbb = true;
      return o;
    }
    static Options no_hash()
    {
      Options o;
      o.hex_no_hash = true;
      return o;
    }
    static Options zero_x()
    {
      Options o;
      o.hex_0x = true;
      return o;
    }
    static Options xterm()
    {
      Options o;
      o.xterm = true;
      return o;
    }
    static Options ls_colors()
    {
      Options o;
      o.ls_colors = true;
      return o;
    }
    static Options tailwind()
    {
      Options o;
      o.tailwind = true;
      return o;
    }
    static Options xcolor()
    {
      Options o;
      o.xcolor = true;
      return o;
    }
  };
} // namespace

TEST_CASE("Hex: the alpha-first and hash-free forms", "[jot][colorizer]")
{
  // #AARRGGBB puts alpha first, as QML and Android do; the RGB digits are what
  // get shown. It is a separate switch from the trailing-alpha form, so the two
  // can never both claim an 8-digit run.
  const std::string qml_line = "color: #80FF8800;";
  const ColorSpan qml = only(qml_line, With::qml());
  REQUIRE(qml.rgb == 0xFF8800u);
  REQUIRE(text_of(qml_line, qml) == "#80FF8800");
  // With both 8-digit forms on, the trailing-alpha reading wins (it is the CSS
  // one); the QML form only applies when it is the enabled reading.
  Options both = With::qml();
  both.hex8 = true;
  REQUIRE(only(qml_line, both).rgb == 0x80FF88u);

  // Bare hex, no "#": only a whole word of 6 or 8 hex digits.
  const std::string bare = "color ff8800 here";
  const ColorSpan bare_span = only(bare, With::no_hash());
  REQUIRE(bare_span.rgb == 0xFF8800u);
  REQUIRE(text_of(bare, bare_span) == "ff8800");
  // An 8-digit bare run still reads as RGB and drops its alpha.
  REQUIRE(only("x = ff880080;", With::no_hash()).rgb == 0xFF8800u);
  // The wrong length, or a word with a non-hex letter, is not a colour. (Note a
  // word like "deface" *is* valid hex, and is intentionally painted -- this is
  // the same behaviour upstream has.)
  REQUIRE(scan("color ff880 here", With::no_hash()).empty());
  REQUIRE(scan("color ff88000 here", With::no_hash()).empty());
  REQUIRE(scan("color defacement here", With::no_hash()).empty());
  // Off by default, so a bare hex-looking word is left alone.
  Options plain;
  REQUIRE(scan(bare, plain).empty());
}

TEST_CASE("Hex: 0x-prefixed Android/Java forms", "[jot][colorizer]")
{
  // 0xRGB, 0xRRGGBB and 0xAARRGGBB, with alpha first in the 8-digit case.
  REQUIRE(only("int c = 0xF80;", With::zero_x()).rgb == 0xFF8800u);
  REQUIRE(only("int c = 0xFF8800;", With::zero_x()).rgb == 0xFF8800u);
  REQUIRE(only("int c = 0x80FF8800;", With::zero_x()).rgb == 0xFF8800u);
  REQUIRE(only("int c = 0XFF8800;", With::zero_x()).rgb == 0xFF8800u);
  // A run that is not one of those lengths is not a colour, and neither is a
  // longer identifier.
  REQUIRE(scan("int c = 0xFF88;", With::zero_x()).empty());
  REQUIRE(scan("int c = 0xGGGGGG;", With::zero_x()).empty());
  REQUIRE(scan("int c = 0xFF88000;", With::zero_x()).empty());
  // Off by default.
  Options plain;
  REQUIRE(scan("int c = 0xFF8800;", plain).empty());
}

TEST_CASE("Terminal codes: xterm index and ANSI escapes", "[jot][colorizer]")
{
  // #xNN is the palette shorthand; index 208 is the orange in the cube.
  REQUIRE(only("#x208", With::xterm()).rgb == 0xFF8700u);
  REQUIRE(only("#x208", With::xterm()).rgb == xterm256_rgb(208));
  REQUIRE(only("#x0", With::xterm()).rgb == 0x000000u);
  REQUIRE(only("#x255", With::xterm()).rgb == 0xEEEEEEu);
  // Out of range, or glued to a longer token, is not a colour.
  REQUIRE(scan("#x256", With::xterm()).empty());
  REQUIRE(scan("#x2088", With::xterm()).empty());

  // Escape sequences, in each spelling that appears in source.
  const std::string fe = "prompt='\\e[38;5;208m$'";
  REQUIRE(only(fe, With::xterm()).rgb == 0xFF8700u);
  REQUIRE(text_of(fe, only(fe, With::xterm())) == "\\e[38;5;208m");
  REQUIRE(only("s = '\\x1b[48;5;196m';", With::xterm()).rgb == 0xFF0000u);
  REQUIRE(only("s = '\\033[38;5;21m';", With::xterm()).rgb == 0x0000FFu);
  // Truecolour escapes carry the colour verbatim.
  REQUIRE(only("s = '\\e[38;2;255;136;0m';", With::xterm()).rgb == 0xFF8800u);
  REQUIRE(only("s = '\\e[48;2;255;136;0m';", With::xterm()).rgb == 0xFF8800u);
  // A malformed or out-of-range code is rejected rather than half-matched.
  REQUIRE(scan("\\e[38;5;300m", With::xterm()).empty());
  REQUIRE(scan("\\e[38;5;20", With::xterm()).empty()); // missing the final 'm'
  REQUIRE(scan("\\e[31m", With::xterm()).empty());      // 16-colour short form: not ported
}

TEST_CASE("LS_COLORS / SGR snippets", "[jot][colorizer]")
{
  // The forms dircolors files are made of. Foreground wins when both appear.
  REQUIRE(only("di=38;5;196", With::ls_colors()).rgb == xterm256_rgb(196));
  REQUIRE(only("di=48;5;196", With::ls_colors()).rgb == xterm256_rgb(196));
  REQUIRE(only("di=32", With::ls_colors()).rgb == xterm256_rgb(2));
  REQUIRE(only("di=44", With::ls_colors()).rgb == xterm256_rgb(4));
  // Bold promotes one of the eight base colours to its bright variant.
  REQUIRE(only("di=01;34", With::ls_colors()).rgb == xterm256_rgb(12));
  REQUIRE(only("di=1;31", With::ls_colors()).rgb == xterm256_rgb(9));
  // Truecolour snippets carry the colour verbatim.
  REQUIRE(only("x=38;2;0;0;255", With::ls_colors()).rgb == 0x0000FFu);
  REQUIRE(only("x=48;2;255;136;0", With::ls_colors()).rgb == 0xFF8800u);
  // A code with no colour in it is not a match, and the span covers the run.
  const std::string line = "di=01;34:";
  const ColorSpan span = only(line, With::ls_colors());
  REQUIRE(text_of(line, span) == "=01;34");
  REQUIRE(scan("n=99", With::ls_colors()).empty());
  // Off by default: "=34" in arithmetic is not a colour.
  Options plain;
  REQUIRE(scan("x = 34", plain).empty());
}

TEST_CASE("Tailwind class suffixes", "[jot][colorizer]")
{
  // The scanner looks up the whole identifier, so a class name resolves without
  // any prefix stripping.
  REQUIRE(only("class=\"text-orange-500\"", With::tailwind()).rgb == 0xF97316u);
  REQUIRE(only("class=\"bg-slate-50\"", With::tailwind()).rgb == 0xF8FAFCu);
  REQUIRE(only("class=\"border-red-600\"", With::tailwind()).rgb == 0xDC2626u);
  // The standalone colours are classes too.
  REQUIRE(only("class=\"text-white\"", With::tailwind()).rgb == 0xFFFFFFu);
  REQUIRE(only("class=\"bg-black\"", With::tailwind()).rgb == 0x000000u);
  // A class that is not a Tailwind colour is left alone, and a bare palette name
  // (no prefix) is not one either.
  REQUIRE(scan("class=\"flex items-center\"", With::tailwind()).empty());
  REQUIRE(scan("class=\"orange-500\"", With::tailwind()).empty());
  // Off by default, so "text-red-500" stays a plain identifier.
  Options plain;
  REQUIRE(scan("class=\"text-orange-500\"", plain).empty());
}

TEST_CASE("LaTeX xcolor expressions", "[jot][colorizer]")
{
  // NAME!NN mixes the name toward white: red!30 is 30% red, 70% white.
  const ColorSpan thirty = only("\\textcolor{red!30}{x}", With::xcolor());
  REQUIRE(thirty.rgb == 0xFFB3B3u);
  REQUIRE(text_of("\\textcolor{red!30}{x}", thirty) == "red!30");
  // !100 is the colour itself, and !0 is white.
  REQUIRE(only("\\color{blue!100}", With::xcolor()).rgb == 0x0000FFu);
  REQUIRE(only("\\color{blue!0}", With::xcolor()).rgb == 0xFFFFFFu);
  // The CamelCase names work too.
  REQUIRE(only("x{LightBlue!50}", With::xcolor()).rgb == 0xD6ECF3u);
  // A percentage over 100 is not an xcolor expression. The name inside it is
  // still a colour in its own right, so the span is just "red" -- what matters
  // is that the "!200" is not folded into it.
  const std::string over = "x{red!200}";
  const auto over_spans = scan(over, With::xcolor());
  REQUIRE(over_spans.size() == 1);
  REQUIRE(text_of(over, over_spans[0]) == "red");
  // A "!" with no digits is not an expression -- the name is still a colour on
  // its own, so the span stops at the name.
  const std::string bang = "x{red!}";
  const auto bang_spans = scan(bang, With::xcolor());
  REQUIRE(bang_spans.size() == 1);
  REQUIRE(text_of(bang, bang_spans[0]) == "red");
  // A name that is not a colour yields nothing at all.
  REQUIRE(scan("x{notacolour!30}", With::xcolor()).empty());
  // Off by default: the expression is not recognised. The name inside it is
  // still a colour in its own right (the names parser is on by default), so the
  // span stops at the name rather than covering the expression.
  Options plain;
  const std::string tex = "\\textcolor{red!30}{x}";
  const auto plain_spans = scan(tex, plain);
  REQUIRE(plain_spans.size() == 1);
  REQUIRE(text_of(tex, plain_spans[0]) == "red");
}

TEST_CASE("The CSS function family beyond rgb and hsl", "[jot][colorizer]")
{
  // hwb, lab, lch, oklch, hsluv and color() all go through the same dispatcher,
  // which validates each grammar and reports the whole call.
  struct Case
  {
    const char *text;
    std::uint32_t rgb;
  };
  const Case kCases[] = {
      {"hwb(0 0% 0%)", 0xFF0000u},
      {"hwb(120 0% 0%)", 0x00FF00u},
      {"hwb(0 100% 0%)", 0xFFFFFFu},
      {"lab(54.29 80.81 69.89)", 0xFF0000u},
      {"lch(54.29 106.84 40.86)", 0xFF0000u},
      {"oklch(0.6279 0.2577 29.23)", 0xFF0000u},
      {"color(srgb 1 0 0)", 0xFF0000u},
      {"color(srgb-linear 1 1 1)", 0xFFFFFFu},
      {"hsluv(12.177 100 53.237)", 0xFF0000u},
  };
  for (const Case &c : kCases)
  {
    const std::string line = std::string("c: ") + c.text + ";";
    Options opts;
    const auto spans = scan(line, opts);
    REQUIRE(spans.size() == 1);
    // Tolerance of 3: the anchors are rounded published values.
    const int dr = std::abs((int)((spans[0].rgb >> 16) & 0xFF) - (int)((c.rgb >> 16) & 0xFF));
    const int dg = std::abs((int)((spans[0].rgb >> 8) & 0xFF) - (int)((c.rgb >> 8) & 0xFF));
    const int db = std::abs((int)(spans[0].rgb & 0xFF) - (int)(c.rgb & 0xFF));
    REQUIRE(dr <= 3);
    REQUIRE(dg <= 3);
    REQUIRE(db <= 3);
    REQUIRE(text_of(line, spans[0]) == c.text);
  }

  // An unknown colour space is rejected rather than guessed at.
  REQUIRE(scan("c: color(bogus 1 0 0);", Options{}).empty());
  // hsluvu() takes the same arguments as hsluv(), with an alpha that is ignored.
  REQUIRE(scan("c: hsluvu(12.177 100 53.237 0.5);", Options{}).size() == 1);
}

TEST_CASE("A function call must be well-formed", "[jot][colorizer]")
{
  // The argument grammar is validated: a call missing an argument, mixing comma
  // and space separators, or leaving out a required percentage is not a colour.
  REQUIRE(scan("c: rgb(1,2);", Options{}).empty());
  REQUIRE(scan("c: rgb(255, 0 0);", Options{}).empty());
  REQUIRE(scan("c: rgb(1,2,3", Options{}).empty());
  REQUIRE(scan("c: hsl(0, 100, 50);", Options{}).empty());
  REQUIRE(scan("c: hwb(0, 0%, 0%);", Options{}).empty()); // hwb is space-separated
  REQUIRE(scan("c: myrgb(1,2,3);", Options{}).empty());
  // Both separator styles are accepted, in their own spelling.
  REQUIRE(scan("c: rgb(255, 0, 0);", Options{}).size() == 1);
  REQUIRE(scan("c: rgb(255 0 0);", Options{}).size() == 1);
  REQUIRE(scan("c: rgba(255, 0, 0, 0.5);", Options{}).size() == 1);
  REQUIRE(scan("c: rgb(255 0 0 / 50%);", Options{}).size() == 1);
}
