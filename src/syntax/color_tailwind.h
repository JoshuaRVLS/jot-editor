#ifndef FEATURES_COLOR_TAILWIND_H
#define FEATURES_COLOR_TAILWIND_H

// Tailwind CSS colour class suffixes, used by the colour preview. The table is
// generated (tools/gen_tailwind_colors.py); this header only declares the lookup
// so the generated file stays free of hand-written API.
//
// Upstream's parser builds one entry per (utility prefix x colour), so
// "text-orange-500" and "bg-slate-50" are each a table entry rather than a
// prefix strip followed by a lookup.

#include <cstdint>
#include <string>

namespace jot_color
{
  // Looks up a whole identifier as a Tailwind colour class. Returns false when
  // it is not one.
  bool lookup_tailwind(const std::string &word, std::uint32_t &rgb);
} // namespace jot_color

#endif // FEATURES_COLOR_TAILWIND_H
