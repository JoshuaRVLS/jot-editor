#include "ui/xterm_palette.h"

#include <algorithm>
#include <cmath>

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
} // namespace

namespace jot_ui
{
void palette_rgb(int index, unsigned char &r, unsigned char &g, unsigned char &b)
{
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
