#ifndef JOT_MODEL_DECORATIONS_H
#define JOT_MODEL_DECORATIONS_H

#include <cstdint>
#include <string>
// Anchored decoration: a highlight span and/or end-of-line virtual text that
// stays glued to its buffer position across edits (the same anchoring idea as
// extmarks). Positions are byte offsets within a line. Every content mutation
// goes through decoration_rebase_begin / ensure_decorations_anchored, which
// diff the pre-edit text snapshot against the current text and shift each
// decoration through the single edit window, honoring right/left gravity.
struct Decoration
{
  std::uint64_t id = 0;
  int row = 0;
  int col = 0; // byte offset within the line
  bool right_gravity = true; // true: insert at (row,col) keeps the mark after
  int fg = -1; // span foreground (-1 = unset; an xterm index or an exact 24-bit colour)
  int bg = -1; // span background (-1 = unset)
  std::string hl; // theme group name, resolved at render time (e.g. "DiagnosticError")
  int width = 0; // span length in bytes (0 = point mark, no span)
  int priority = 0; // higher draws over lower and over syntax colors
  // Underline over the span: 0 = none, 1 = straight, 2 = wavy. Unlike fg/bg,
  // the underline does NOT recolor the text — the decoration color only
  // reaches the underline itself (VSCode-style squiggle).
  int underline = 0;
  // Raw underline colour (-1 = fall back to hl, then fg): an xterm index or an
  // exact 24-bit colour (see ui/xterm_palette.h), painted as SGR 58.
  int underline_fg = -1;
  std::string underline_hl; // theme group for the underline color
  std::string virt_text; // end-of-line virtual text (empty = none)
  int virt_fg = -1;
  int virt_bg = -1;
  std::string virt_hl; // theme group for the virtual text
  // Faint (SGR 2): keeps the token's own colours and drops its intensity,
  // unlike a colour swap. The terminal equivalent of an opacity fade on
  // inactive preprocessor branches.
  bool dim = false;
};

#endif
