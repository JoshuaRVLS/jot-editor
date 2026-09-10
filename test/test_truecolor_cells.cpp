// Truecolour plumbing for the cell model.
//
// jot's cells were xterm-256 palette indices only, so a colour preview could
// never show the colour a literal actually named. UICell now carries an
// optional 24-bit value that wins over the index; these cases pin both ends of
// that: the GUI resolves it exactly, and the terminal emits 38;2/48;2 when it
// is supported and quantises to the nearest palette entry when it is not.
#include "ui/terminal.h"
#include "ui/ui.h"
#include "ui/xterm_palette.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace jot_ui;

TEST_CASE("Nearest palette index round-trips the palette", "[jot][colorizer]")
{
  // Every palette entry must map to a colour that is *identical* to it, or
  // quantising would shift a colour even when the palette already has it. The
  // index itself is not asserted because the palette repeats some colours
  // (index 0 and 16 are both black, 15 and 231 both white), so either is a
  // correct answer for those.
  for (int i = 0; i < 256; i++)
  {
    unsigned char r = 0, g = 0, b = 0;
    palette_rgb(i, r, g, b);
    const int hit = palette_nearest_index(r, g, b);
    unsigned char hr = 0, hg = 0, hb = 0;
    palette_rgb(hit, hr, hg, hb);
    REQUIRE(hr == r);
    REQUIRE(hg == g);
    REQUIRE(hb == b);
  }

  // Pure primaries land on the palette's own primaries.
  REQUIRE(palette_nearest_index(0, 0, 0) == 0);
  REQUIRE(palette_nearest_index(255, 255, 255) == 15);
  REQUIRE(palette_nearest_index(255, 0, 0) == 9);
  // A colour between two palette entries picks the closer one.
  const int dark = palette_nearest_index(8, 8, 8);
  unsigned char dr = 0, dg = 0, db = 0;
  palette_rgb(dark, dr, dg, db);
  REQUIRE(dr <= 10);
}

TEST_CASE("The terminal emits truecolour only when it is supported", "[jot][colorizer]")
{
  Terminal term;
  term.clear_pending_output_for_test();

  // Palette-only call: unchanged behaviour, the index goes out as 38;5/48;5.
  term.set_color(1, 2);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;5;1m\x1b[48;5;2m");

  // Without truecolor support a 24-bit value is quantised at emit time, so the
  // same cell still shows the closest available colour.
  term.clear_pending_output_for_test();
  REQUIRE_FALSE(term.supports_truecolor());
  term.set_color(1, 2, 0xFF8800u, 0x001122u);
  const std::string quantised = term.pending_output_for_test();
  REQUIRE(quantised.find("38;5;") != std::string::npos);
  REQUIRE(quantised.find("48;5;") != std::string::npos);
  REQUIRE(quantised.find("38;2;") == std::string::npos);

  // With support the exact value goes out as 38;2/48;2.
  term.clear_pending_output_for_test();
  term.set_truecolor_supported(true);
  REQUIRE(term.supports_truecolor());
  term.set_color(1, 2, 0xFF8800u, 0x001122u);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;2;255;136;0m\x1b[48;2;0;17;34m");

  // A cell with only one of the two sides in 24-bit colour mixes the forms.
  term.clear_pending_output_for_test();
  term.set_color(4, 5, kNoRgb, 0x0A0B0Cu);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;5;4m\x1b[48;2;10;11;12m");
}

TEST_CASE("The environment decides the auto truecolor answer", "[jot][colorizer]")
{
  // terminal_env_supports_truecolor() reads the real environment; set and
  // restore it so the assertion is deterministic either way.
  const char *saved_colorterm = std::getenv("COLORTERM");
  const char *saved_term = std::getenv("TERM");
  const std::string colorterm = saved_colorterm ? saved_colorterm : "";
  const std::string term_value = saved_term ? saved_term : "";

  setenv("COLORTERM", "truecolor", 1);
  setenv("TERM", "xterm-256color", 1);
  REQUIRE(terminal_env_supports_truecolor());

  setenv("COLORTERM", "24bit", 1);
  REQUIRE(terminal_env_supports_truecolor());

  setenv("COLORTERM", "", 1);
  setenv("TERM", "xterm-direct", 1);
  REQUIRE(terminal_env_supports_truecolor());

  // An ordinary 256-colour terminal must not be claimed as truecolor: guessing
  // wrong would paint every preview cell wrong.
  setenv("COLORTERM", "", 1);
  setenv("TERM", "xterm-256color", 1);
  REQUIRE_FALSE(terminal_env_supports_truecolor());

  if (saved_colorterm)
  {
    setenv("COLORTERM", colorterm.c_str(), 1);
  }
  else
  {
    unsetenv("COLORTERM");
  }
  if (saved_term)
  {
    setenv("TERM", term_value.c_str(), 1);
  }
  else
  {
    unsetenv("TERM");
  }
}

TEST_CASE("A cell's 24-bit colour survives the diff renderer", "[jot][colorizer]")
{
  // The row-diff renderer compares cells to decide what to re-emit. Two cells
  // that differ only in their 24-bit colour must compare unequal, or the
  // second colour would never be painted.
  UICell a;
  a.ch = "x";
  a.fg = 1;
  a.bg = 2;
  UICell b = a;
  REQUIRE(a == b);

  b.bg_rgb = 0x123456u;
  REQUIRE(a != b);
  b.bg_rgb = kNoRgb;
  b.fg_rgb = 0x654321u;
  REQUIRE(a != b);
  REQUIRE(a == UICell(a));
}
