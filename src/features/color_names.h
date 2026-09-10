#ifndef FEATURES_COLOR_NAMES_H
#define FEATURES_COLOR_NAMES_H

// CSS/X11 named colours, used by the colour preview (src/features/color_codes.cpp).
// The table itself is generated (tools/gen_color_names.py); this header only
// declares the lookup so the generated file stays free of hand-written API.

#include <cstddef>
#include <cstdint>

namespace jot_color
{
  // Looks up a whole word (no surrounding word characters) case-sensitively
  // against the table. `word` need not be NUL-terminated; `len` bounds it.
  // Returns false when there is no such colour, leaving `rgb` untouched.
  bool lookup_named_color(
      const char *word, size_t len, bool allow_camelcase, bool allow_uppercase, std::uint32_t &rgb);
} // namespace jot_color

#endif // FEATURES_COLOR_NAMES_H
