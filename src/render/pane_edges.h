#ifndef RENDER_PANE_EDGES_H
#define RENDER_PANE_EDGES_H

// Which sides of a region's box to ink.
//
// jot draws one box per pane, which put two lines wherever two regions met: the
// sidebar's right border sat in the column next to the pane's left border (`││`),
// and a split drew the same doubled line. A single pane also framed all four
// screen edges, none of which delimit anything -- focus is already shown by the
// cursor and the active-tab marker.
//
// The rule: a region inks an edge only where another region lies immediately to
// its right (a vertical separator is drawn once, by the region on its left), and
// edges against the screen boundary get no ink at all. A region's bottom row is
// content rather than a border: its own background, set against the next
// region's, is the break (kNoEdges below).
//
// Two bottom edges are still inked. Two stacked panes share one background, so a
// line is the only thing that separates them. The bottom dock draws the rule
// along its own top as its first row -- the row is part of the height it reserves
// (integrated_terminal_reserved_h) and doubles as the resize handle -- rather
// than spending the pane area's last row on it, which is why a pane's last row
// can be code.
//
// Kept free of editor types (a UIRect is layout, nothing more) so the rule can be
// unit tested rather than only observed on screen.

#include "ui/ui.h"

#include <vector>

namespace pane_layout
{
  // True when `other` overlaps `rect` along the axis being compared, i.e. the two
  // regions really are side by side (or stacked) rather than merely sharing a
  // coordinate.
  bool spans_vertically(const UIRect &a, const UIRect &b);
  bool spans_horizontally(const UIRect &a, const UIRect &b);

  // The edges of `rect` that face one of `neighbours`. Only the right and bottom
  // sides can ever come back set: the region on the other side of a separator
  // owns its left/top edge, so exactly one of the pair inks it.
  UIBorderEdges border_edges(const UIRect &rect, const std::vector<UIRect> &neighbours);

  // No sides inked. What a region uses when every side of it is either the
  // screen's own edge, a separator its neighbour draws, or the row/column where
  // its background alone marks the break -- the docks and the status line, whose
  // backgrounds differ enough that a rule between them was only borrowed space.
  inline constexpr UIBorderEdges kNoEdges{false, false, false, false};
} // namespace pane_layout

#endif // RENDER_PANE_EDGES_H
