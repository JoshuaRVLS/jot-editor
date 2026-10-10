#ifndef FEATURES_COLOR_SPACE_H
#define FEATURES_COLOR_SPACE_H

// Colour-space conversions for the colour preview: CSS Color Level 4 spaces and
// HSLuv. Pure maths, no UI or editor types, so every conversion is unit testable
// against published anchor values (sRGB red/white/black have documented
// coordinates in each space).
//
// Ported from nvim-colorizer.lua's color.lua and vendored hsluv.lua, which in
// turn follow the CSS Color 4 specification and Björn Ottosson's OKLab article.
// Where a space needs a chromatic adaptation (Lab/LCH are D50, sRGB is D65) the
// Bradford matrix from the spec is used, so results match what a browser shows.

#include <cstdint>

namespace jot_color
{
  // 0xRRGGBB from already-clamped channels.
  std::uint32_t pack_rgb(int r, int g, int b);
  // 0xRRGGBB from floating channels in 0..255, rounded and clamped.
  std::uint32_t pack_rgb_f(double r, double g, double b);

  // sRGB transfer function, both directions (channels in 0..1).
  double srgb_to_linear(double c);
  double linear_to_srgb(double c);

  // hwb(): hue in degrees, whiteness/blackness in 0..1. When w + b >= 1 the
  // result is the grey they describe, per the spec.
  std::uint32_t hwb_to_rgb(double h_deg, double w, double b);

  // hsl(): hue in degrees, saturation and lightness in 0..1.
  std::uint32_t hsl_to_rgb(double h_deg, double s, double l);

  // CIE Lab with the spec's D50 white point; L in 0..100, a/b unbounded.
  std::uint32_t lab_to_rgb(double l, double a, double b);

  // CIE LCH: the cylindrical form of Lab. Hue in degrees.
  std::uint32_t lch_to_rgb(double l, double c, double h_deg);

  // OKLCH: the cylindrical form of OKLab. L in 0..1, chroma unbounded, hue in
  // degrees.
  std::uint32_t oklch_to_rgb(double l, double c, double h_deg);

  // The colour spaces color() accepts. Anything else is rejected by the caller.
  enum class ColorFnSpace
  {
    Srgb,
    SrgbLinear,
    DisplayP3,
    A98Rgb,
    ProPhotoRgb,
    Rec2020,
  };

  // color(space r g b): channels in 0..1 (already percent-scaled). Converts to
  // sRGB with the spec's matrices and clamps to the sRGB gamut.
  std::uint32_t css_color_to_rgb(ColorFnSpace space, double r, double g, double b);

  // hsluv(): H in 0..360, S and L in 0..100.
  std::uint32_t hsluv_to_rgb(double h, double s, double l);
} // namespace jot_color

#endif // FEATURES_COLOR_SPACE_H
