#include "ui/xterm_palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
// The 16 ANSI colours, exactly the values the GUI has always painted with (the
// terminal resolves its own for indices < 16). Kept byte-identical so moving them
// here does not shift a single pixel.
const unsigned char kBasicColors[16][3] = {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};
// The 6 levels the 6x6x6 cube is built from.
const unsigned char kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

// sRGB -> linear light, per the WCAG relative-luminance definition.
float linearize(float channel)
{
  return channel <= 0.03928f ? channel / 12.92f
                             : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

// The exact colours interned so far, indexed by id - kExactColorBase.
std::vector<std::array<unsigned char, 3>> &exact_table()
{
  static std::vector<std::array<unsigned char, 3>> table;
  return table;
}

// -1 when `c` is not a hex digit.
int hex_digit(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace

namespace jot_ui
{
int exact_color_id(unsigned char r, unsigned char g, unsigned char b)
{
  auto &table = exact_table();
  for (std::size_t i = 0; i < table.size(); i++)
  {
    if (table[i][0] == r && table[i][1] == g && table[i][2] == b)
    {
      return kExactColorBase + (int)i;
    }
  }
  table.push_back({r, g, b});
  return kExactColorBase + (int)table.size() - 1;
}

bool exact_color_rgb(int value, unsigned char &r, unsigned char &g, unsigned char &b)
{
  if (value < kExactColorBase)
  {
    return false;
  }
  auto &table = exact_table();
  const std::size_t index = (std::size_t)(value - kExactColorBase);
  if (index >= table.size())
  {
    return false;
  }
  r = table[index][0];
  g = table[index][1];
  b = table[index][2];
  return true;
}

bool is_exact_color(int value)
{
  return value >= kExactColorBase
         && (std::size_t)(value - kExactColorBase) < exact_table().size();
}

bool parse_hex_color(const std::string &text, unsigned char &r, unsigned char &g, unsigned char &b)
{
  const std::size_t begin = text.find_first_not_of(" \t");
  if (begin == std::string::npos || text[begin] != '#')
  {
    return false;
  }
  const std::size_t end = text.find_last_not_of(" \t");
  const std::string digits = text.substr(begin + 1, end - begin);
  if (digits.size() == 3)
  {
    // Short form: each digit is doubled, so #48c is #4488cc.
    const int dr = hex_digit(digits[0]);
    const int dg = hex_digit(digits[1]);
    const int db = hex_digit(digits[2]);
    if (dr < 0 || dg < 0 || db < 0)
    {
      return false;
    }
    r = (unsigned char)(dr * 17);
    g = (unsigned char)(dg * 17);
    b = (unsigned char)(db * 17);
    return true;
  }
  if (digits.size() == 6 || digits.size() == 8)
  {
    // #rrggbb and #rrggbbaa: the alpha channel is dropped, since every surface
    // that paints a theme colour composites over an opaque background anyway.
    int channels[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++)
    {
      const int hi = hex_digit(digits[(std::size_t)i * 2]);
      const int lo = hex_digit(digits[(std::size_t)i * 2 + 1]);
      if (hi < 0 || lo < 0)
      {
        return false;
      }
      channels[i] = hi * 16 + lo;
    }
    r = (unsigned char)channels[0];
    g = (unsigned char)channels[1];
    b = (unsigned char)channels[2];
    return true;
  }
  return false;
}

int exact_color_from_hex(const std::string &text)
{
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  if (!parse_hex_color(text, r, g, b))
  {
    return -1;
  }
  return exact_color_id(r, g, b);
}

std::size_t exact_color_count()
{
  return exact_table().size();
}

void palette_rgb(int index, unsigned char &r, unsigned char &g, unsigned char &b)
{
  // An exact colour resolves to itself. Every consumer reads colours through
  // here -- SGR 38;2 / 48;2, the GUI's shader, the luminance and contrast
  // maths -- so an exact colour needs no special case anywhere else.
  if (exact_color_rgb(index, r, g, b))
  {
    return;
  }
  if (index < 0)
  {
    index = 0;
  }
  if (index < 16)
  {
    r = kBasicColors[index][0];
    g = kBasicColors[index][1];
    b = kBasicColors[index][2];
  }
  else if (index < 232)
  {
    const int v = index - 16;
    r = kCubeLevels[v / 36];
    g = kCubeLevels[(v / 6) % 6];
    b = kCubeLevels[v % 6];
  }
  else if (index < 256)
  {
    const unsigned char v = (unsigned char)(8 + (index - 232) * 10);
    r = g = b = v;
  }
  else
  {
    r = g = b = 0;
  }
}

float palette_luminance(int index)
{
  unsigned char r = 0, g = 0, b = 0;
  palette_rgb(index, r, g, b);
  const float lr = linearize(r / 255.0f);
  const float lg = linearize(g / 255.0f);
  const float lb = linearize(b / 255.0f);
  return 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
}

float contrast_ratio(int a, int b)
{
  const float la = palette_luminance(a);
  const float lb = palette_luminance(b);
  const float hi = std::max(la, lb);
  const float lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

int palette_nearest_index(unsigned char r, unsigned char g, unsigned char b)
{
  int best = 0;
  long long best_distance = -1;
  for (int i = 0; i < 256; i++)
  {
    unsigned char pr = 0, pg = 0, pb = 0;
    palette_rgb(i, pr, pg, pb);
    const long long dr = (long long)r - pr;
    const long long dg = (long long)g - pg;
    const long long db = (long long)b - pb;
    const long long d = dr * dr + dg * dg + db * db;
    if (best_distance < 0 || d < best_distance)
    {
      best_distance = d;
      best = i;
    }
  }
  return best;
}

CursorColors cursor_colors(int cursor_fg, int cursor_bg, int cell_bg)
{
  const float bg_contrast = contrast_ratio(cursor_bg, cell_bg);
  const float fg_contrast = contrast_ratio(cursor_fg, cell_bg);
  // Strictly greater: equal contrast (or an identical pair) keeps the block colour
  // as the fill, so the common case of a well-designed pair is left alone.
  if (fg_contrast > bg_contrast)
  {
    return {cursor_fg, cursor_bg};
  }
  return {cursor_bg, cursor_fg};
}
} // namespace jot_ui
