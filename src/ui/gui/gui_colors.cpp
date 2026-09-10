// Cell colors: the xterm 256-color palette the terminal backend writes as
// SGR 38;5 / 48;5 indices, resolved to rgb for the shader. The palette itself
// lives in ui/xterm_palette (shared with the caret's contrast decision and
// unit tested there); cell_colors resolves a cell's effective fg/bg on top of
// it, honoring reverse video and dim.
#include "gui/gui.h"
#include "ui/xterm_palette.h"

void UIGui::xterm_rgb(int index, float &r, float &g, float &b)
{
  unsigned char cr = 0;
  unsigned char cg = 0;
  unsigned char cb = 0;
  jot_ui::palette_rgb(index, cr, cg, cb);
  r = cr / 255.0f;
  g = cg / 255.0f;
  b = cb / 255.0f;
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