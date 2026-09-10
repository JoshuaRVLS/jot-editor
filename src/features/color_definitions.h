#ifndef FEATURES_COLOR_DEFINITIONS_H
#define FEATURES_COLOR_DEFINITIONS_H

// Variable definitions for the colour preview: CSS custom properties
// (--brand: #ff8800, or --brand: 240,198,198) and Sass variables
// ($brand: #ff8800), plus the references that resolve to them
// (var(--brand), $brand).
//
// Ported from nvim-colorizer.lua's css_var.lua, css_var_rgb.lua and sass.lua.
// Those three keep per-buffer state and rescan on every text change; here the
// index is a value the caller owns and rebuilds when the buffer is marked
// edited, which is the same idiom the syntax and bracket-depth caches use.
//
// Not ported: following @import to other files (and Sass's file watchers). The
// definitions are resolved within the buffer being rendered, so a variable
// defined in a file that is not open resolves to nothing rather than to a
// stale or missing colour.

#include "features/color_codes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jot_color
{
  class Definitions
  {
  public:
    // Rescans every definition line, using `options` to decide what counts as a
    // colour value (so disabling a parser also disables variables holding it).
    void rebuild(const std::vector<std::string> &lines, const Options &options);

    // Colour for a CSS custom property name (without the leading "--").
    bool lookup_css(const std::string &name, std::uint32_t &rgb) const;
    // Colour for a Sass variable name (without the leading "$").
    bool lookup_sass(const std::string &name, std::uint32_t &rgb) const;

    bool empty() const
    {
      return css_.empty() && sass_.empty();
    }

  private:
    std::unordered_map<std::string, std::uint32_t> css_;
    std::unordered_map<std::string, std::uint32_t> sass_;
  };
} // namespace jot_color

#endif // FEATURES_COLOR_DEFINITIONS_H
