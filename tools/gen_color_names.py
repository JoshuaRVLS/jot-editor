#!/usr/bin/env python3
"""Generate src/features/color_names.cpp -- the CSS/X11 named-colour table.

The colour preview feature (src/features/color_codes.cpp) looks up words like
"red" or "LightBlue"; this script owns the name -> RGB data so it stays
reviewable and regenerable instead of being hand-typed into the source.

The CamelCase spelling is the source of truth (it is how the X11/CSS names are
defined); the lowercase variant is derived by folding case, and the UPPERCASE
variant is generated at runtime only when that option is on.

Usage:
    python3 tools/gen_color_names.py            # rewrites the generated file
    python3 tools/gen_color_names.py --check    # exits non-zero if out of date

The file is committed; re-run this after editing the table below.
"""
from __future__ import annotations

import sys
from pathlib import Path

# CSS Color Module Level 4 named colours (plus the two transparent-free X11
# spellings of gray/grey that CSS also defines). CamelCase spelling -> 0xRRGGBB.
NAMED_COLORS: dict[str, int] = {
    "AliceBlue": 0xF0F8FF,
    "AntiqueWhite": 0xFAEBD7,
    "Aqua": 0x00FFFF,
    "Aquamarine": 0x7FFFD4,
    "Azure": 0xF0FFFF,
    "Beige": 0xF5F5DC,
    "Bisque": 0xFFE4C4,
    "Black": 0x000000,
    "BlanchedAlmond": 0xFFEBCD,
    "Blue": 0x0000FF,
    "BlueViolet": 0x8A2BE2,
    "Brown": 0xA52A2A,
    "BurlyWood": 0xDEB887,
    "CadetBlue": 0x5F9EA0,
    "Chartreuse": 0x7FFF00,
    "Chocolate": 0xD2691E,
    "Coral": 0xFF7F50,
    "CornflowerBlue": 0x6495ED,
    "Cornsilk": 0xFFF8DC,
    "Crimson": 0xDC143C,
    "Cyan": 0x00FFFF,
    "DarkBlue": 0x00008B,
    "DarkCyan": 0x008B8B,
    "DarkGoldenRod": 0xB8860B,
    "DarkGray": 0xA9A9A9,
    "DarkGreen": 0x006400,
    "DarkGrey": 0xA9A9A9,
    "DarkKhaki": 0xBDB76B,
    "DarkMagenta": 0x8B008B,
    "DarkOliveGreen": 0x556B2F,
    "DarkOrange": 0xFF8C00,
    "DarkOrchid": 0x9932CC,
    "DarkRed": 0x8B0000,
    "DarkSalmon": 0xE9967A,
    "DarkSeaGreen": 0x8FBC8F,
    "DarkSlateBlue": 0x483D8B,
    "DarkSlateGray": 0x2F4F4F,
    "DarkSlateGrey": 0x2F4F4F,
    "DarkTurquoise": 0x00CED1,
    "DarkViolet": 0x9400D3,
    "DeepPink": 0xFF1493,
    "DeepSkyBlue": 0x00BFFF,
    "DimGray": 0x696969,
    "DimGrey": 0x696969,
    "DodgerBlue": 0x1E90FF,
    "FireBrick": 0xB22222,
    "FloralWhite": 0xFFFAF0,
    "ForestGreen": 0x228B22,
    "Fuchsia": 0xFF00FF,
    "Gainsboro": 0xDCDCDC,
    "GhostWhite": 0xF8F8FF,
    "Gold": 0xFFD700,
    "GoldenRod": 0xDAA520,
    "Gray": 0x808080,
    "Green": 0x008000,
    "GreenYellow": 0xADFF2F,
    "Grey": 0x808080,
    "HoneyDew": 0xF0FFF0,
    "HotPink": 0xFF69B4,
    "IndianRed": 0xCD5C5C,
    "Indigo": 0x4B0082,
    "Ivory": 0xFFFFF0,
    "Khaki": 0xF0E68C,
    "Lavender": 0xE6E6FA,
    "LavenderBlush": 0xFFF0F5,
    "LawnGreen": 0x7CFC00,
    "LemonChiffon": 0xFFFACD,
    "LightBlue": 0xADD8E6,
    "LightCoral": 0xF08080,
    "LightCyan": 0xE0FFFF,
    "LightGoldenRodYellow": 0xFAFAD2,
    "LightGray": 0xD3D3D3,
    "LightGreen": 0x90EE90,
    "LightGrey": 0xD3D3D3,
    "LightPink": 0xFFB6C1,
    "LightSalmon": 0xFFA07A,
    "LightSeaGreen": 0x20B2AA,
    "LightSkyBlue": 0x87CEFA,
    "LightSlateGray": 0x778899,
    "LightSlateGrey": 0x778899,
    "LightSteelBlue": 0xB0C4DE,
    "LightYellow": 0xFFFFE0,
    "Lime": 0x00FF00,
    "LimeGreen": 0x32CD32,
    "Linen": 0xFAF0E6,
    "Magenta": 0xFF00FF,
    "Maroon": 0x800000,
    "MediumAquaMarine": 0x66CDAA,
    "MediumBlue": 0x0000CD,
    "MediumOrchid": 0xBA55D3,
    "MediumPurple": 0x9370DB,
    "MediumSeaGreen": 0x3CB371,
    "MediumSlateBlue": 0x7B68EE,
    "MediumSpringGreen": 0x00FA9A,
    "MediumTurquoise": 0x48D1CC,
    "MediumVioletRed": 0xC71585,
    "MidnightBlue": 0x191970,
    "MintCream": 0xF5FFFA,
    "MistyRose": 0xFFE4E1,
    "Moccasin": 0xFFE4B5,
    "NavajoWhite": 0xFFDEAD,
    "Navy": 0x000080,
    "OldLace": 0xFDF5E6,
    "Olive": 0x808000,
    "OliveDrab": 0x6B8E23,
    "Orange": 0xFFA500,
    "OrangeRed": 0xFF4500,
    "Orchid": 0xDA70D6,
    "PaleGoldenRod": 0xEEE8AA,
    "PaleGreen": 0x98FB98,
    "PaleTurquoise": 0xAFEEEE,
    "PaleVioletRed": 0xDB7093,
    "PapayaWhip": 0xFFEFD5,
    "PeachPuff": 0xFFDAB9,
    "Peru": 0xCD853F,
    "Pink": 0xFFC0CB,
    "Plum": 0xDDA0DD,
    "PowderBlue": 0xB0E0E6,
    "Purple": 0x800080,
    "RebeccaPurple": 0x663399,
    "Red": 0xFF0000,
    "RosyBrown": 0xBC8F8F,
    "RoyalBlue": 0x4169E1,
    "SaddleBrown": 0x8B4513,
    "Salmon": 0xFA8072,
    "SandyBrown": 0xF4A460,
    "SeaGreen": 0x2E8B57,
    "SeaShell": 0xFFF5EE,
    "Sienna": 0xA0522D,
    "Silver": 0xC0C0C0,
    "SkyBlue": 0x87CEEB,
    "SlateBlue": 0x6A5ACD,
    "SlateGray": 0x708090,
    "SlateGrey": 0x708090,
    "Snow": 0xFFFAFA,
    "SpringGreen": 0x00FF7F,
    "SteelBlue": 0x4682B4,
    "Tan": 0xD2B48C,
    "Teal": 0x008080,
    "Thistle": 0xD8BFD8,
    "Tomato": 0xFF6347,
    "Turquoise": 0x40E0D0,
    "Violet": 0xEE82EE,
    "Wheat": 0xF5DEB3,
    "White": 0xFFFFFF,
    "WhiteSmoke": 0xF5F5F5,
    "Yellow": 0xFFFF00,
    "YellowGreen": 0x9ACD32,
}

HEADER = '''// GENERATED FILE -- do not edit by hand.
//
// The CSS/X11 named-colour table behind the colour preview
// (src/features/color_codes.cpp). Regenerate with:
//
//     python3 tools/gen_color_names.py
//
// The CamelCase spelling is the canonical one; the lowercase variant is a
// second table entry so lookups are a plain binary search on the word as it
// appears in the buffer. UPPERCASE is folded at lookup time instead of being
// stored, since it is off by default and would double the table again.

#include "features/color_names.h"

#include <algorithm>
#include <cstring>

namespace
{
  struct NamedColor
  {
    const char *name;
    std::uint32_t rgb;
  };

  // Sorted by name (strcmp order) for binary search.
  const NamedColor kNames[] = {
'''

FOOTER = '''  };
  constexpr size_t kNameCount = sizeof(kNames) / sizeof(kNames[0]);

  int compare_name(const void *key, const void *elem)
  {
    return std::strcmp((const char *)key, ((const NamedColor *)elem)->name);
  }
} // namespace

namespace jot_color
{
  bool lookup_named_color(
      const char *word, size_t len, bool allow_camelcase, bool allow_uppercase, std::uint32_t &rgb)
  {
    if (len == 0 || len > 32)
    {
      return false;
    }
    char buf[33];
    std::memcpy(buf, word, len);
    buf[len] = '\\0';

    const NamedColor *hit =
        (const NamedColor *)std::bsearch(buf, kNames, kNameCount, sizeof(NamedColor), compare_name);
    if (hit)
    {
      rgb = hit->rgb;
      return true;
    }

    // Uppercase is opt-in and folded here so the table only carries the two
    // spellings that are on by default.
    if (allow_uppercase)
    {
      for (size_t i = 0; i < len; i++)
      {
        if (buf[i] >= 'A' && buf[i] <= 'Z')
        {
          buf[i] = (char)(buf[i] - 'A' + 'a');
        }
      }
      if (std::strcmp(buf, word) != 0 || allow_camelcase)
      {
        hit = (const NamedColor *)std::bsearch(
            buf, kNames, kNameCount, sizeof(NamedColor), compare_name);
        if (hit)
        {
          rgb = hit->rgb;
          return true;
        }
      }
    }
    return false;
  }
} // namespace jot_color
'''


def entries() -> list[tuple[str, int]]:
    out: dict[str, int] = {}
    for camel, rgb in NAMED_COLORS.items():
        out[camel] = rgb
        out[camel.lower()] = rgb
    return sorted(out.items())


def render() -> str:
    rows = [HEADER]
    for name, rgb in entries():
        rows.append(f'      {{"{name}", 0x{rgb:06X}u}},\n')
    rows.append(FOOTER)
    return "".join(rows)


def main() -> int:
    target = Path(__file__).resolve().parent.parent / "src" / "features" / "color_names.cpp"
    content = render()
    if "--check" in sys.argv:
        current = target.read_text() if target.exists() else ""
        if current != content:
            print(f"{target} is out of date; run tools/gen_color_names.py")
            return 1
        print(f"{target} is up to date ({len(entries())} names)")
        return 0
    target.write_text(content)
    print(f"wrote {target} ({len(entries())} names)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
