// The xterm-256 palette and the caret's colour choice (src/ui/xterm_palette.cpp).
//
// The GUI caret used to be painted in the *cell's* foreground colour, so it
// inherited whatever syntax colour sat under it -- grey on a comment, blue on a
// keyword, black over a cyan selection -- and disappeared into the text. It now
// takes the theme's cursor pair and picks whichever end of it contrasts with the
// cell background, which is what these cases pin.
#include "ui/xterm_palette.h"

#include <catch2/catch_test_macros.hpp>

using namespace jot_ui;

namespace
{
  int channel_gap(int a, int b)
  {
    unsigned char ar = 0, ag = 0, ab = 0, br = 0, bg = 0, bb = 0;
    palette_rgb(a, ar, ag, ab);
    palette_rgb(b, br, bg, bb);
    return std::abs((int)ar - (int)br) + std::abs((int)ag - (int)bg) + std::abs((int)ab - (int)bb);
  }
} // namespace

TEST_CASE("Palette resolves the three xterm regions", "[jot]")
{
  unsigned char r = 0, g = 0, b = 0;

  // The 16 ANSI entries keep the exact values the GUI has always painted with.
  palette_rgb(0, r, g, b);
  REQUIRE((r == 0 && g == 0 && b == 0));
  palette_rgb(7, r, g, b);
  REQUIRE((r == 229 && g == 229 && b == 229));
  palette_rgb(15, r, g, b);
  REQUIRE((r == 255 && g == 255 && b == 255));

  // 6x6x6 cube: index 16 is the black corner, 231 the white one.
  palette_rgb(16, r, g, b);
  REQUIRE((r == 0 && g == 0 && b == 0));
  palette_rgb(231, r, g, b);
  REQUIRE((r == 255 && g == 255 && b == 255));
  // 196 is the pure-red cube corner (levels 5,0,0).
  palette_rgb(196, r, g, b);
  REQUIRE((r == 255 && g == 0 && b == 0));

  // Grayscale ramp runs 8,18,...,238 and is neutral.
  palette_rgb(232, r, g, b);
  REQUIRE((r == 8 && g == 8 && b == 8));
  palette_rgb(255, r, g, b);
  REQUIRE((r == 238 && g == 238 && b == 238));

  // Out of range clamps instead of reading off the table.
  palette_rgb(-3, r, g, b);
  REQUIRE((r == 0 && g == 0 && b == 0));
  palette_rgb(999, r, g, b);
  REQUIRE((r == 0 && g == 0 && b == 0));
}

TEST_CASE("Luminance and contrast behave like WCAG", "[jot]")
{
  // Black/white extremes.
  REQUIRE(palette_luminance(0) < 0.001f);
  REQUIRE(palette_luminance(15) > 0.99f);
  // Ordering is monotonic across the grayscale ramp.
  REQUIRE(palette_luminance(232) < palette_luminance(240));
  REQUIRE(palette_luminance(240) < palette_luminance(255));

  // Identical colours have the minimum ratio; black on white the maximum.
  REQUIRE(contrast_ratio(4, 4) == 1.0f);
  REQUIRE(contrast_ratio(0, 15) > 20.0f);
  // Symmetric.
  REQUIRE(contrast_ratio(0, 15) == contrast_ratio(15, 0));
}

TEST_CASE("The caret keeps the colour that contrasts with the cell", "[jot]")
{
  // Default theme pair: fg_cursor 0 (black), bg_cursor 7 (light grey).
  // On a dark cell the light end must be the caret body...
  const CursorColors on_dark = cursor_colors(0, 7, 0);
  REQUIRE(on_dark.fill == 7);
  REQUIRE(on_dark.ink == 0);
  // ...and on a light cell the dark end, so the caret can never vanish into the
  // cell it sits on (this is the case that made it hard to see).
  const CursorColors on_light = cursor_colors(0, 7, 15);
  REQUIRE(on_light.fill == 0);
  REQUIRE(on_light.ink == 7);
}

TEST_CASE("The caret stays visible over selections and comments", "[jot]")
{
  // The default theme's cursor pair, against the two backgrounds that used to
  // swallow the caret.
  // Selection background is a bright cyan (index 6): the black end contrasts.
  const CursorColors on_selection = cursor_colors(0, 7, 6);
  REQUIRE(contrast_ratio(on_selection.fill, 6) > 4.5f);
  REQUIRE(on_selection.fill != 6);
  // A dimmed cell background (gray ramp) still gets a readable caret.
  const CursorColors on_dim = cursor_colors(0, 7, 236);
  REQUIRE(contrast_ratio(on_dim.fill, 236) > 4.5f);
}

TEST_CASE("A cursor pair with no contrast still returns a usable pair", "[jot]")
{
  // Identical colours: nothing to choose between, so the block colour stays the
  // fill and the pair is returned unchanged rather than inverted at random.
  const CursorColors same = cursor_colors(4, 4, 0);
  REQUIRE(same.fill == 4);
  REQUIRE(same.ink == 4);

  // A pair that matches the background exactly still yields the better of the
  // two ends, and the two roles are always distinct when the pair allows it.
  const CursorColors low = cursor_colors(8, 0, 0);
  REQUIRE(low.fill != low.ink);
  REQUIRE(contrast_ratio(low.fill, 0) >= contrast_ratio(low.ink, 0));
}

TEST_CASE("The chosen caret colour is never left invisible", "[jot]")
{
  // Exhaustive over the palette: for the default cursor pair, whatever cell
  // background the caret lands on, the chosen fill must be distinguishable from
  // it -- and must be at least as distinguishable as the other end of the pair.
  for (int bg = 0; bg < 256; bg++)
  {
    const CursorColors cc = cursor_colors(0, 7, bg);
    const float chosen = contrast_ratio(cc.fill, bg);
    const float other = contrast_ratio(cc.ink, bg);
    REQUIRE(chosen >= other);
    // The one exception is a background identical to both ends (impossible for
    // two distinct colours), so a strict inequality only has to hold when the
    // pair actually differs from the background.
    if (bg != cc.fill && bg != cc.ink)
    {
      REQUIRE(chosen > 1.0f);
    }
    REQUIRE(channel_gap(cc.fill, cc.ink) == channel_gap(0, 7));
  }
}
