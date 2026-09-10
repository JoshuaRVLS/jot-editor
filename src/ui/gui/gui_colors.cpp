// Cell colors: the same xterm 256-color palette the terminal backend writes
// as SGR 38;5 / 48;5 indices. 0-15 ANSI, 16-231 6x6x6 cube, 232-255
// grayscale ramp. cell_colors resolves a cell's effective fg/bg (honoring
// reverse video and dim) into linear-ish rgb floats for the shader.
#include "gui/gui.h"

namespace
{
const unsigned char kBasicColors[16][3] = {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};
const unsigned char kCubeLevels[6] = {0, 95, 135, 175, 215, 255};
} // namespace

void UIGui::xterm_rgb(int index, float &r, float &g, float &b)
{
  if (index < 0)
  {
    index = 0;
  }
  if (index < 16)
  {
    r = kBasicColors[index][0] / 255.0f;
    g = kBasicColors[index][1] / 255.0f;
    b = kBasicColors[index][2] / 255.0f;
  }
  else if (index < 232)
  {
    int v = index - 16;
    r = kCubeLevels[v / 36] / 255.0f;
    g = kCubeLevels[(v / 6) % 6] / 255.0f;
    b = kCubeLevels[v % 6] / 255.0f;
  }
  else
  {
    float v = (8 + (index - 232) * 10) / 255.0f;
    r = g = b = v;
  }
}

void UIGui::cell_colors(const UICell &cell, float &fr, float &fg_, float &fb, float &br,
                        float &bg_, float &bb) const
{
  int fgi = cell.reverse ? cell.bg : cell.fg;
  int bgi = cell.reverse ? cell.fg : cell.bg;
  xterm_rgb(fgi, fr, fg_, fb);
  xterm_rgb(bgi, br, bg_, bb);
  if (cell.dim)
  {
    fr *= 0.55f;
    fg_ *= 0.55f;
    fb *= 0.55f;
  }
}