// Colour-space conversions (src/features/color_space.cpp).
//
// These are the conversions behind hwb(), lab(), lch(), oklch(), hsluv() and
// color(). A wrong matrix or a transposed constant produces colours that look
// plausible but are wrong, so each case is anchored to a value that is
// documented outside this codebase: sRGB red/white/black have published
// coordinates in every one of these spaces (CSS Color 4, Björn Ottosson's OKLab
// article, the HSLuv reference), and an achromatic input must stay achromatic in
// any space that shares the sRGB white point.
#include "features/color_space.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace jot_color;

namespace
{
  // Channel-wise comparison with a tolerance: the published anchors are rounded
  // to a few decimals, and the matrices carry rounding of their own.
  bool near_rgb(std::uint32_t got, std::uint32_t want, int tolerance = 2)
  {
    for (int shift : {16, 8, 0})
    {
      const int a = (int)((got >> shift) & 0xFF);
      const int b = (int)((want >> shift) & 0xFF);
      if (std::abs(a - b) > tolerance)
      {
        return false;
      }
    }
    return true;
  }

  // True when all three channels are within `tolerance` of each other.
  bool is_neutral(std::uint32_t rgb, int tolerance = 1)
  {
    const int r = (int)((rgb >> 16) & 0xFF);
    const int g = (int)((rgb >> 8) & 0xFF);
    const int b = (int)(rgb & 0xFF);
    return std::abs(r - g) <= tolerance && std::abs(g - b) <= tolerance;
  }
} // namespace

TEST_CASE("OKLCH anchors: the documented coordinates of the sRGB primaries", "[jot][colorizer]")
{
  // oklch(0.6279 0.2577 29.23) is sRGB red, per the OKLab reference values.
  REQUIRE(near_rgb(oklch_to_rgb(0.6279, 0.2577, 29.23), 0xFF0000u));
  // The achromatic corners are exact in any sane implementation.
  REQUIRE(oklch_to_rgb(1.0, 0.0, 0.0) == 0xFFFFFFu);
  REQUIRE(oklch_to_rgb(0.0, 0.0, 0.0) == 0x000000u);
  // Zero chroma is a grey at that lightness, and lightness is monotonic.
  REQUIRE(is_neutral(oklch_to_rgb(0.5, 0.0, 0.0)));
  const std::uint32_t dark = oklch_to_rgb(0.3, 0.0, 0.0);
  const std::uint32_t light = oklch_to_rgb(0.8, 0.0, 0.0);
  REQUIRE((dark & 0xFF) < (light & 0xFF));
}

TEST_CASE("Lab and LCH anchors", "[jot][colorizer]")
{
  // lab(54.29 80.81 69.89) and lch(54.29 106.84 40.86) are both sRGB red: the
  // polar form is the cartesian form of the same colour.
  REQUIRE(near_rgb(lab_to_rgb(54.29, 80.81, 69.89), 0xFF0000u));
  REQUIRE(near_rgb(lch_to_rgb(54.29, 106.84, 40.86), 0xFF0000u));
  REQUIRE(lab_to_rgb(100.0, 0.0, 0.0) == 0xFFFFFFu);
  REQUIRE(lab_to_rgb(0.0, 0.0, 0.0) == 0x000000u);
  REQUIRE(is_neutral(lab_to_rgb(50.0, 0.0, 0.0)));
  // Rotating a chroma vector by 180 degrees moves a and b to their negatives,
  // which must land on the opponent hue.
  const std::uint32_t a_pos = lab_to_rgb(50.0, 40.0, 0.0);
  const std::uint32_t a_neg = lab_to_rgb(50.0, -40.0, 0.0);
  REQUIRE((a_pos & 0xFF0000) > (a_neg & 0xFF0000)); // first is redder
}

TEST_CASE("HWB anchors", "[jot][colorizer]")
{
  // Hue is HSL's, so 0 is red and 120 is green.
  REQUIRE(hwb_to_rgb(0.0, 0.0, 0.0) == 0xFF0000u);
  REQUIRE(hwb_to_rgb(120.0, 0.0, 0.0) == 0x00FF00u);
  // Full whiteness is white, full blackness black.
  REQUIRE(hwb_to_rgb(0.0, 1.0, 0.0) == 0xFFFFFFu);
  REQUIRE(hwb_to_rgb(0.0, 0.0, 1.0) == 0x000000u);
  // Mixed whiteness and blackness beyond 1 collapse to the grey they describe.
  const std::uint32_t grey = hwb_to_rgb(210.0, 0.5, 0.5);
  REQUIRE(is_neutral(grey, 0));
  REQUIRE(grey == 0x7F7F7Fu);
  // Partway is a tint of the hue, and hue wraps.
  REQUIRE(near_rgb(hwb_to_rgb(360.0, 0.0, 0.0), hwb_to_rgb(0.0, 0.0, 0.0)));
  const std::uint32_t tint = hwb_to_rgb(0.0, 0.5, 0.0);
  REQUIRE((tint & 0xFF0000) == 0xFF0000);
  REQUIRE((int)((tint >> 8) & 0xFF) >= 0x80);
}

TEST_CASE("hsl() shares the same hue geometry", "[jot][colorizer]")
{
  REQUIRE(hsl_to_rgb(0.0, 1.0, 0.5) == 0xFF0000u);
  REQUIRE(hsl_to_rgb(120.0, 1.0, 0.5) == 0x00FF00u);
  REQUIRE(hsl_to_rgb(240.0, 1.0, 0.5) == 0x0000FFu);
  // Zero saturation is a grey at that lightness.
  REQUIRE(hsl_to_rgb(0.0, 0.0, 0.5) == 0x808080u);
  REQUIRE(hsl_to_rgb(0.0, 0.0, 0.0) == 0x000000u);
  REQUIRE(hsl_to_rgb(0.0, 0.0, 1.0) == 0xFFFFFFu);
  // HWB with no whiteness or blackness is the fully saturated hue, which is
  // exactly HSL(h, 100%, 50%) -- the two must agree.
  REQUIRE(hwb_to_rgb(200.0, 0.0, 0.0) == hsl_to_rgb(200.0, 1.0, 0.5));
}

TEST_CASE("color() across the supported spaces", "[jot][colorizer]")
{
  // srgb is an identity transform apart from the 0-255 scale.
  REQUIRE(css_color_to_rgb(ColorFnSpace::Srgb, 1.0, 0.0, 0.0) == 0xFF0000u);
  REQUIRE(css_color_to_rgb(ColorFnSpace::Srgb, 0.0, 0.5, 1.0) == 0x0080FFu);
  // Linear sRGB puts its transfer function back.
  REQUIRE(css_color_to_rgb(ColorFnSpace::SrgbLinear, 1.0, 1.0, 1.0) == 0xFFFFFFu);
  REQUIRE(css_color_to_rgb(ColorFnSpace::SrgbLinear, 0.0, 0.0, 0.0) == 0x000000u);
  // 0.5 linear is ~0.735 after the sRGB curve.
  REQUIRE(near_rgb(css_color_to_rgb(ColorFnSpace::SrgbLinear, 0.5, 0.5, 0.5), 0xBCBCBCu, 1));
  // Every wide-gamut space maps white to white, which is what makes the
  // chromatic-adaptation path trustworthy.
  for (ColorFnSpace space :
       {ColorFnSpace::DisplayP3, ColorFnSpace::A98Rgb, ColorFnSpace::ProPhotoRgb, ColorFnSpace::Rec2020})
  {
    REQUIRE(css_color_to_rgb(space, 1.0, 1.0, 1.0) == 0xFFFFFFu);
    REQUIRE(css_color_to_rgb(space, 0.0, 0.0, 0.0) == 0x000000u);
    // ...and an achromatic input stays achromatic: a good check that the
    // matrices are not transposed or mismatched to the wrong white point.
    REQUIRE(is_neutral(css_color_to_rgb(space, 0.5, 0.5, 0.5), 2));
  }
  // Out-of-range channels clamp rather than wrapping.
  REQUIRE(css_color_to_rgb(ColorFnSpace::Srgb, 2.0, -1.0, 0.0) == 0xFF0000u);
}

TEST_CASE("HSLuv anchors", "[jot][colorizer]")
{
  // HSLuv(12.177, 100, 53.237) is the reference implementation's published
  // coordinate for sRGB red. (Note the hue is 12.177, not 0: HSLuv is CIE LUV
  // based, and pure red does not sit on the +U axis.)
  REQUIRE(near_rgb(hsluv_to_rgb(12.177, 100.0, 53.237), 0xFF0000u, 3));
  REQUIRE(hsluv_to_rgb(0.0, 0.0, 100.0) == 0xFFFFFFu);
  REQUIRE(hsluv_to_rgb(0.0, 0.0, 0.0) == 0x000000u);
  // Zero saturation is a grey whose level follows CIE lightness, and L is
  // monotonic.
  const std::uint32_t mid = hsluv_to_rgb(0.0, 0.0, 50.0);
  REQUIRE(is_neutral(mid, 1));
  REQUIRE((mid & 0xFF) > 0x60);
  REQUIRE((mid & 0xFF) < 0x90);
  REQUIRE((hsluv_to_rgb(0.0, 0.0, 25.0) & 0xFF) < (mid & 0xFF));
  // Saturation is relative to the gamut boundary, so S=100 at a given lightness
  // and hue must produce a colour at least as saturated as a lower S.
  const std::uint32_t full = hsluv_to_rgb(30.0, 100.0, 60.0);
  const std::uint32_t half = hsluv_to_rgb(30.0, 50.0, 60.0);
  const int spread_full = std::abs((int)((full >> 16) & 0xFF) - (int)(full & 0xFF));
  const int spread_half = std::abs((int)((half >> 16) & 0xFF) - (int)(half & 0xFF));
  REQUIRE(spread_full > spread_half);
}
