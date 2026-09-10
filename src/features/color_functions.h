#ifndef FEATURES_COLOR_FUNCTIONS_H
#define FEATURES_COLOR_FUNCTIONS_H

// The CSS colour-function family for the colour preview: rgb(), hsl(), hwb(),
// lab(), lch(), oklch(), hsluv() and color(). Split out of color_codes.cpp so
// the scanner stays a scanner and the (substantial) per-function argument
// grammar lives in one place.
//
// Argument handling follows nvim-colorizer.lua, which in turn follows the CSS
// Color Level 4 grammar: percentages where the spec allows them, angle units on
// every hue, the modern space-separated form, and a "/" before alpha.
//
// Alpha channels are parsed and then ignored: the preview shows the opaque
// colour. (The spec's other option is compositing against an unknown backdrop,
// which is not something a preview can answer.)

#include <cstdint>
#include <string>

namespace jot_color
{
  // Parses one colour function whose name starts at `name_start` in `line`,
  // scanning no further than `limit`. On success `rgb` holds 0xRRGGBB and `end`
  // is the index just past the call's closing paren.
  //
  // Rejects malformed calls (missing arguments, unbalanced parens, mixed comma
  // and space separators, unknown colour spaces), so a rejected candidate can
  // fall through to the next parser.
  bool parse_css_function(const std::string &line,
                          size_t name_start,
                          size_t limit,
                          std::uint32_t &rgb,
                          size_t &end);
} // namespace jot_color

#endif // FEATURES_COLOR_FUNCTIONS_H
