// GENERATED FILE -- do not edit by hand.
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
      {"AliceBlue", 0xF0F8FFu},
      {"AntiqueWhite", 0xFAEBD7u},
      {"Aqua", 0x00FFFFu},
      {"Aquamarine", 0x7FFFD4u},
      {"Azure", 0xF0FFFFu},
      {"Beige", 0xF5F5DCu},
      {"Bisque", 0xFFE4C4u},
      {"Black", 0x000000u},
      {"BlanchedAlmond", 0xFFEBCDu},
      {"Blue", 0x0000FFu},
      {"BlueViolet", 0x8A2BE2u},
      {"Brown", 0xA52A2Au},
      {"BurlyWood", 0xDEB887u},
      {"CadetBlue", 0x5F9EA0u},
      {"Chartreuse", 0x7FFF00u},
      {"Chocolate", 0xD2691Eu},
      {"Coral", 0xFF7F50u},
      {"CornflowerBlue", 0x6495EDu},
      {"Cornsilk", 0xFFF8DCu},
      {"Crimson", 0xDC143Cu},
      {"Cyan", 0x00FFFFu},
      {"DarkBlue", 0x00008Bu},
      {"DarkCyan", 0x008B8Bu},
      {"DarkGoldenRod", 0xB8860Bu},
      {"DarkGray", 0xA9A9A9u},
      {"DarkGreen", 0x006400u},
      {"DarkGrey", 0xA9A9A9u},
      {"DarkKhaki", 0xBDB76Bu},
      {"DarkMagenta", 0x8B008Bu},
      {"DarkOliveGreen", 0x556B2Fu},
      {"DarkOrange", 0xFF8C00u},
      {"DarkOrchid", 0x9932CCu},
      {"DarkRed", 0x8B0000u},
      {"DarkSalmon", 0xE9967Au},
      {"DarkSeaGreen", 0x8FBC8Fu},
      {"DarkSlateBlue", 0x483D8Bu},
      {"DarkSlateGray", 0x2F4F4Fu},
      {"DarkSlateGrey", 0x2F4F4Fu},
      {"DarkTurquoise", 0x00CED1u},
      {"DarkViolet", 0x9400D3u},
      {"DeepPink", 0xFF1493u},
      {"DeepSkyBlue", 0x00BFFFu},
      {"DimGray", 0x696969u},
      {"DimGrey", 0x696969u},
      {"DodgerBlue", 0x1E90FFu},
      {"FireBrick", 0xB22222u},
      {"FloralWhite", 0xFFFAF0u},
      {"ForestGreen", 0x228B22u},
      {"Fuchsia", 0xFF00FFu},
      {"Gainsboro", 0xDCDCDCu},
      {"GhostWhite", 0xF8F8FFu},
      {"Gold", 0xFFD700u},
      {"GoldenRod", 0xDAA520u},
      {"Gray", 0x808080u},
      {"Green", 0x008000u},
      {"GreenYellow", 0xADFF2Fu},
      {"Grey", 0x808080u},
      {"HoneyDew", 0xF0FFF0u},
      {"HotPink", 0xFF69B4u},
      {"IndianRed", 0xCD5C5Cu},
      {"Indigo", 0x4B0082u},
      {"Ivory", 0xFFFFF0u},
      {"Khaki", 0xF0E68Cu},
      {"Lavender", 0xE6E6FAu},
      {"LavenderBlush", 0xFFF0F5u},
      {"LawnGreen", 0x7CFC00u},
      {"LemonChiffon", 0xFFFACDu},
      {"LightBlue", 0xADD8E6u},
      {"LightCoral", 0xF08080u},
      {"LightCyan", 0xE0FFFFu},
      {"LightGoldenRodYellow", 0xFAFAD2u},
      {"LightGray", 0xD3D3D3u},
      {"LightGreen", 0x90EE90u},
      {"LightGrey", 0xD3D3D3u},
      {"LightPink", 0xFFB6C1u},
      {"LightSalmon", 0xFFA07Au},
      {"LightSeaGreen", 0x20B2AAu},
      {"LightSkyBlue", 0x87CEFAu},
      {"LightSlateGray", 0x778899u},
      {"LightSlateGrey", 0x778899u},
      {"LightSteelBlue", 0xB0C4DEu},
      {"LightYellow", 0xFFFFE0u},
      {"Lime", 0x00FF00u},
      {"LimeGreen", 0x32CD32u},
      {"Linen", 0xFAF0E6u},
      {"Magenta", 0xFF00FFu},
      {"Maroon", 0x800000u},
      {"MediumAquaMarine", 0x66CDAAu},
      {"MediumBlue", 0x0000CDu},
      {"MediumOrchid", 0xBA55D3u},
      {"MediumPurple", 0x9370DBu},
      {"MediumSeaGreen", 0x3CB371u},
      {"MediumSlateBlue", 0x7B68EEu},
      {"MediumSpringGreen", 0x00FA9Au},
      {"MediumTurquoise", 0x48D1CCu},
      {"MediumVioletRed", 0xC71585u},
      {"MidnightBlue", 0x191970u},
      {"MintCream", 0xF5FFFAu},
      {"MistyRose", 0xFFE4E1u},
      {"Moccasin", 0xFFE4B5u},
      {"NavajoWhite", 0xFFDEADu},
      {"Navy", 0x000080u},
      {"OldLace", 0xFDF5E6u},
      {"Olive", 0x808000u},
      {"OliveDrab", 0x6B8E23u},
      {"Orange", 0xFFA500u},
      {"OrangeRed", 0xFF4500u},
      {"Orchid", 0xDA70D6u},
      {"PaleGoldenRod", 0xEEE8AAu},
      {"PaleGreen", 0x98FB98u},
      {"PaleTurquoise", 0xAFEEEEu},
      {"PaleVioletRed", 0xDB7093u},
      {"PapayaWhip", 0xFFEFD5u},
      {"PeachPuff", 0xFFDAB9u},
      {"Peru", 0xCD853Fu},
      {"Pink", 0xFFC0CBu},
      {"Plum", 0xDDA0DDu},
      {"PowderBlue", 0xB0E0E6u},
      {"Purple", 0x800080u},
      {"RebeccaPurple", 0x663399u},
      {"Red", 0xFF0000u},
      {"RosyBrown", 0xBC8F8Fu},
      {"RoyalBlue", 0x4169E1u},
      {"SaddleBrown", 0x8B4513u},
      {"Salmon", 0xFA8072u},
      {"SandyBrown", 0xF4A460u},
      {"SeaGreen", 0x2E8B57u},
      {"SeaShell", 0xFFF5EEu},
      {"Sienna", 0xA0522Du},
      {"Silver", 0xC0C0C0u},
      {"SkyBlue", 0x87CEEBu},
      {"SlateBlue", 0x6A5ACDu},
      {"SlateGray", 0x708090u},
      {"SlateGrey", 0x708090u},
      {"Snow", 0xFFFAFAu},
      {"SpringGreen", 0x00FF7Fu},
      {"SteelBlue", 0x4682B4u},
      {"Tan", 0xD2B48Cu},
      {"Teal", 0x008080u},
      {"Thistle", 0xD8BFD8u},
      {"Tomato", 0xFF6347u},
      {"Turquoise", 0x40E0D0u},
      {"Violet", 0xEE82EEu},
      {"Wheat", 0xF5DEB3u},
      {"White", 0xFFFFFFu},
      {"WhiteSmoke", 0xF5F5F5u},
      {"Yellow", 0xFFFF00u},
      {"YellowGreen", 0x9ACD32u},
      {"aliceblue", 0xF0F8FFu},
      {"antiquewhite", 0xFAEBD7u},
      {"aqua", 0x00FFFFu},
      {"aquamarine", 0x7FFFD4u},
      {"azure", 0xF0FFFFu},
      {"beige", 0xF5F5DCu},
      {"bisque", 0xFFE4C4u},
      {"black", 0x000000u},
      {"blanchedalmond", 0xFFEBCDu},
      {"blue", 0x0000FFu},
      {"blueviolet", 0x8A2BE2u},
      {"brown", 0xA52A2Au},
      {"burlywood", 0xDEB887u},
      {"cadetblue", 0x5F9EA0u},
      {"chartreuse", 0x7FFF00u},
      {"chocolate", 0xD2691Eu},
      {"coral", 0xFF7F50u},
      {"cornflowerblue", 0x6495EDu},
      {"cornsilk", 0xFFF8DCu},
      {"crimson", 0xDC143Cu},
      {"cyan", 0x00FFFFu},
      {"darkblue", 0x00008Bu},
      {"darkcyan", 0x008B8Bu},
      {"darkgoldenrod", 0xB8860Bu},
      {"darkgray", 0xA9A9A9u},
      {"darkgreen", 0x006400u},
      {"darkgrey", 0xA9A9A9u},
      {"darkkhaki", 0xBDB76Bu},
      {"darkmagenta", 0x8B008Bu},
      {"darkolivegreen", 0x556B2Fu},
      {"darkorange", 0xFF8C00u},
      {"darkorchid", 0x9932CCu},
      {"darkred", 0x8B0000u},
      {"darksalmon", 0xE9967Au},
      {"darkseagreen", 0x8FBC8Fu},
      {"darkslateblue", 0x483D8Bu},
      {"darkslategray", 0x2F4F4Fu},
      {"darkslategrey", 0x2F4F4Fu},
      {"darkturquoise", 0x00CED1u},
      {"darkviolet", 0x9400D3u},
      {"deeppink", 0xFF1493u},
      {"deepskyblue", 0x00BFFFu},
      {"dimgray", 0x696969u},
      {"dimgrey", 0x696969u},
      {"dodgerblue", 0x1E90FFu},
      {"firebrick", 0xB22222u},
      {"floralwhite", 0xFFFAF0u},
      {"forestgreen", 0x228B22u},
      {"fuchsia", 0xFF00FFu},
      {"gainsboro", 0xDCDCDCu},
      {"ghostwhite", 0xF8F8FFu},
      {"gold", 0xFFD700u},
      {"goldenrod", 0xDAA520u},
      {"gray", 0x808080u},
      {"green", 0x008000u},
      {"greenyellow", 0xADFF2Fu},
      {"grey", 0x808080u},
      {"honeydew", 0xF0FFF0u},
      {"hotpink", 0xFF69B4u},
      {"indianred", 0xCD5C5Cu},
      {"indigo", 0x4B0082u},
      {"ivory", 0xFFFFF0u},
      {"khaki", 0xF0E68Cu},
      {"lavender", 0xE6E6FAu},
      {"lavenderblush", 0xFFF0F5u},
      {"lawngreen", 0x7CFC00u},
      {"lemonchiffon", 0xFFFACDu},
      {"lightblue", 0xADD8E6u},
      {"lightcoral", 0xF08080u},
      {"lightcyan", 0xE0FFFFu},
      {"lightgoldenrodyellow", 0xFAFAD2u},
      {"lightgray", 0xD3D3D3u},
      {"lightgreen", 0x90EE90u},
      {"lightgrey", 0xD3D3D3u},
      {"lightpink", 0xFFB6C1u},
      {"lightsalmon", 0xFFA07Au},
      {"lightseagreen", 0x20B2AAu},
      {"lightskyblue", 0x87CEFAu},
      {"lightslategray", 0x778899u},
      {"lightslategrey", 0x778899u},
      {"lightsteelblue", 0xB0C4DEu},
      {"lightyellow", 0xFFFFE0u},
      {"lime", 0x00FF00u},
      {"limegreen", 0x32CD32u},
      {"linen", 0xFAF0E6u},
      {"magenta", 0xFF00FFu},
      {"maroon", 0x800000u},
      {"mediumaquamarine", 0x66CDAAu},
      {"mediumblue", 0x0000CDu},
      {"mediumorchid", 0xBA55D3u},
      {"mediumpurple", 0x9370DBu},
      {"mediumseagreen", 0x3CB371u},
      {"mediumslateblue", 0x7B68EEu},
      {"mediumspringgreen", 0x00FA9Au},
      {"mediumturquoise", 0x48D1CCu},
      {"mediumvioletred", 0xC71585u},
      {"midnightblue", 0x191970u},
      {"mintcream", 0xF5FFFAu},
      {"mistyrose", 0xFFE4E1u},
      {"moccasin", 0xFFE4B5u},
      {"navajowhite", 0xFFDEADu},
      {"navy", 0x000080u},
      {"oldlace", 0xFDF5E6u},
      {"olive", 0x808000u},
      {"olivedrab", 0x6B8E23u},
      {"orange", 0xFFA500u},
      {"orangered", 0xFF4500u},
      {"orchid", 0xDA70D6u},
      {"palegoldenrod", 0xEEE8AAu},
      {"palegreen", 0x98FB98u},
      {"paleturquoise", 0xAFEEEEu},
      {"palevioletred", 0xDB7093u},
      {"papayawhip", 0xFFEFD5u},
      {"peachpuff", 0xFFDAB9u},
      {"peru", 0xCD853Fu},
      {"pink", 0xFFC0CBu},
      {"plum", 0xDDA0DDu},
      {"powderblue", 0xB0E0E6u},
      {"purple", 0x800080u},
      {"rebeccapurple", 0x663399u},
      {"red", 0xFF0000u},
      {"rosybrown", 0xBC8F8Fu},
      {"royalblue", 0x4169E1u},
      {"saddlebrown", 0x8B4513u},
      {"salmon", 0xFA8072u},
      {"sandybrown", 0xF4A460u},
      {"seagreen", 0x2E8B57u},
      {"seashell", 0xFFF5EEu},
      {"sienna", 0xA0522Du},
      {"silver", 0xC0C0C0u},
      {"skyblue", 0x87CEEBu},
      {"slateblue", 0x6A5ACDu},
      {"slategray", 0x708090u},
      {"slategrey", 0x708090u},
      {"snow", 0xFFFAFAu},
      {"springgreen", 0x00FF7Fu},
      {"steelblue", 0x4682B4u},
      {"tan", 0xD2B48Cu},
      {"teal", 0x008080u},
      {"thistle", 0xD8BFD8u},
      {"tomato", 0xFF6347u},
      {"turquoise", 0x40E0D0u},
      {"violet", 0xEE82EEu},
      {"wheat", 0xF5DEB3u},
      {"white", 0xFFFFFFu},
      {"whitesmoke", 0xF5F5F5u},
      {"yellow", 0xFFFF00u},
      {"yellowgreen", 0x9ACD32u},
  };
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
    buf[len] = '\0';

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
