#ifndef UI_XTERM_PALETTE_H
#define UI_XTERM_PALETTE_H

// xterm-256 palette maths, shared by both frontends and kept free of SDL/GL/Terminal
// so it is unit testable on its own.
//
// The terminal backend writes palette *indices* (SGR 38;5 / 48;5) and the emulator
// resolves them, while the GUI has to resolve them itself. Both sides also need to
// reason about luminance: the caret is drawn in whichever of its two theme colours
// stands out against the cell underneath it, so a caret parked on a comment, a
// selection or a dimmed cell stays legible instead of blending into it.

namespace jot_ui
{
  // xterm-256 -> 8 bit rgb. 0-15 ANSI (the table the GUI paints with), 16-231 the
  // 6x6x6 cube, 232-255 the grayscale ramp. Out-of-range indices clamp to 0.
  void palette_rgb(int index, unsigned char &r, unsigned char &g, unsigned char &b);

  // WCAG relative luminance of a palette entry (0 = black, 1 = white).
  float palette_luminance(int index);

  // WCAG contrast ratio between two palette entries: 1 for identical colours, up to
  // 21 for black against white. Used to decide which colour is readable on which.
  float contrast_ratio(int a, int b);

  struct CursorColors
  {
    int fill = 7; // the caret body
    int ink = 0;  // the glyph redrawn inside a block caret
  };

  // Picks the caret's colours for a cell whose background is `cell_bg`: whichever
  // member of the theme's cursor pair (fg_cursor, bg_cursor) contrasts most with
  // that background becomes the fill, the other the ink. A tie -- including a pair
  // whose two colours are identical -- prefers bg_cursor, the colour themes use for
  // the caret body.
  CursorColors cursor_colors(int cursor_fg, int cursor_bg, int cell_bg);
} // namespace jot_ui

#endif // UI_XTERM_PALETTE_H
