#ifndef UI_XTERM_PALETTE_H
#define UI_XTERM_PALETTE_H

// The colour-value domain, shared by both frontends and kept free of SDL/GL/
// Terminal so it is unit testable on its own.
//
// A colour travels through the UI as one int -- UICell::fg/bg, Theme slots, the
// Lua UI kit, the integrated terminal's default colours. Values 0-255 are xterm
// palette indices: the terminal backend writes them as SGR 38;5 / 48;5 and the
// emulator resolves them, while the GUI resolves them itself. From
// kExactColorBase up they are ids into a process-wide table of exact 24-bit
// colours, which is how a theme file can name "#ffb86b" without being folded
// onto the 256-entry grid.
//
// Both sides also need to reason about luminance: the caret is drawn in whichever
// of its two theme colours stands out against the cell underneath it, so a caret
// parked on a comment, a selection or a dimmed cell stays legible instead of
// blending into it.

#include <cstddef>
#include <string>

namespace jot_ui
{
  // First id an exact colour can have; below it a value is a palette index.
  constexpr int kExactColorBase = 256;

  // Interns an exact colour and returns its id (>= kExactColorBase). The same
  // rgb always interns to the same id, so two themes naming one colour share it.
  int exact_color_id(unsigned char r, unsigned char g, unsigned char b);

  // The exact colour behind a value: false for a palette index, for -1, and for
  // an id that was never handed out.
  bool exact_color_rgb(int value, unsigned char &r, unsigned char &g, unsigned char &b);

  // True for an interned exact colour id (>= kExactColorBase).
  bool is_exact_color(int value);

  // "#rgb", "#rrggbb" or "#rrggbbaa" (alpha ignored), case-insensitive and
  // surrounding whitespace tolerated; false for anything else.
  bool parse_hex_color(const std::string &text, unsigned char &r, unsigned char &g,
                       unsigned char &b);

  // parse_hex_color + exact_color_id: the colour value for a hex string in a
  // theme slot, or -1 when the text is not a colour (so the slot is left alone).
  int exact_color_from_hex(const std::string &text);

  // Test hook: how many exact colours are interned. The table is process-wide and
  // deliberately never cleared -- an id can still sit in the cell grid or the
  // previous frame's baseline, so dropping it would repaint that cell black. A
  // few hundred entries is a few hundred bytes.
  std::size_t exact_color_count();

  // Colour value -> 8 bit rgb. Palette indices: 0-15 ANSI (the table the GUI
  // paints with), 16-231 the 6x6x6 cube, 232-255 the grayscale ramp; an interned
  // exact colour resolves to itself; anything else clamps to 0 (black).
  void palette_rgb(int index, unsigned char &r, unsigned char &g, unsigned char &b);

  // WCAG relative luminance of a palette entry (0 = black, 1 = white).
  float palette_luminance(int index);

  // WCAG contrast ratio between two palette entries: 1 for identical colours, up to
  // 21 for black against white. Used to decide which colour is readable on which.
  float contrast_ratio(int a, int b);

  // Closest palette entry to a 24-bit colour, by squared RGB distance. Used when a
  // truecolor value has to be shown on a terminal (or in a context) that only
  // understands palette indices.
  int palette_nearest_index(unsigned char r, unsigned char g, unsigned char b);

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
