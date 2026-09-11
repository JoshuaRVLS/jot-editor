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
// its right or immediately below. Each separator is therefore drawn once, by the
// upper/left region, and edges against the screen boundary get no ink at all.
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
} // namespace pane_layout

#endif // RENDER_PANE_EDGES_H
