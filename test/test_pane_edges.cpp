// The border-edges rule (src/render/pane_edges.cpp).
//
// jot used to draw a full box per pane, which put two lines wherever two regions
// met: the sidebar's right border sat in the column next to the pane's left
// border (`││`), a split did the same, and a single pane framed all four screen
// edges -- three of which delimit nothing. The rule now is that a region inks an
// edge only where another region lies immediately to its right or below, so each
// separator is drawn exactly once, by the upper/left region.
//
// These pin the rule itself; the probe (test/sidebar_edges_probe.py) pins what
// actually reaches the screen.
#include "render/pane_edges.h"

#include <catch2/catch_test_macros.hpp>

using namespace pane_layout;

namespace
{
  UIRect rect(int x, int y, int w, int h)
  {
    return UIRect{x, y, w, h};
  }

  int count_on(const UIBorderEdges &e)
  {
    return (e.top ? 1 : 0) + (e.right ? 1 : 0) + (e.bottom ? 1 : 0) + (e.left ? 1 : 0);
  }
} // namespace

TEST_CASE("A lone region draws no border at all", "[jot][borders]")
{
  // Nothing around it: every edge faces the screen boundary, which delimits
  // nothing. This is the single-pane, no-sidebar case.
  const UIBorderEdges e = border_edges(rect(0, 1, 80, 20), {});
  REQUIRE(count_on(e) == 0);
}

TEST_CASE("Only the right and bottom edges can ever be inked", "[jot][borders]")
{
  // A region on the left owns the separator between us, so its presence must
  // not make us draw our left edge -- that is what produced the doubled `││`.
  const UIBorderEdges left = border_edges(rect(20, 1, 60, 20), {rect(0, 1, 20, 20)});
  REQUIRE_FALSE(left.left);
  REQUIRE(count_on(left) == 0);

  // Same for a region above us: it owns the shared line.
  const UIBorderEdges above = border_edges(rect(0, 11, 80, 10), {rect(0, 1, 80, 10)});
  REQUIRE_FALSE(above.top);
  REQUIRE(count_on(above) == 0);
}

TEST_CASE("A region to the right inks the right edge only", "[jot][borders]")
{
  // Side-by-side split: the left pane draws the separator, the right pane
  // draws nothing.
  const UIBorderEdges left_pane = border_edges(rect(0, 1, 40, 20), {rect(40, 1, 40, 20)});
  REQUIRE(left_pane.right);
  REQUIRE_FALSE(left_pane.left);
  REQUIRE_FALSE(left_pane.top);
  REQUIRE_FALSE(left_pane.bottom);

  const UIBorderEdges right_pane = border_edges(rect(40, 1, 40, 20), {rect(0, 1, 40, 20)});
  REQUIRE(count_on(right_pane) == 0);
}

TEST_CASE("A region below inks the bottom edge only", "[jot][borders]")
{
  // Stacked split.
  const UIBorderEdges top_pane = border_edges(rect(0, 1, 80, 10), {rect(0, 11, 80, 10)});
  REQUIRE(top_pane.bottom);
  REQUIRE(count_on(top_pane) == 1);

  const UIBorderEdges bottom_pane = border_edges(rect(0, 11, 80, 10), {rect(0, 1, 80, 10)});
  REQUIRE(count_on(bottom_pane) == 0);
}

TEST_CASE("Adjacency requires an exact meeting, not an overlap", "[jot][borders]")
{
  // Side by side, sharing rows: adjacent.
  REQUIRE(border_edges(rect(0, 5, 10, 10), {rect(10, 5, 10, 10)}).right);

  // A gap of one column between them: not adjacent, so no separator.
  REQUIRE(count_on(border_edges(rect(0, 5, 10, 10), {rect(11, 5, 10, 10)})) == 0);

  // Rows do not overlap (one is above the other, not beside it), so a rect whose
  // x happens to line up must not count as a right neighbour.
  REQUIRE(count_on(border_edges(rect(0, 5, 10, 10), {rect(10, 40, 10, 10)})) == 0);

  // Overlapping rows but the columns merely overlap too (no shared boundary).
  REQUIRE(count_on(border_edges(rect(0, 5, 10, 10), {rect(5, 5, 10, 10)})) == 0);
}

TEST_CASE("Several neighbours combine and degenerate rects are ignored", "[jot][borders]")
{
  // Sidebar on the left, a pane to the right, a panel below: the right and
  // bottom edges each get inked once, the left does not (the sidebar owns it).
  const UIBorderEdges e = border_edges(
      rect(20, 1, 40, 10), {rect(0, 1, 20, 10), rect(60, 1, 20, 10), rect(0, 11, 80, 3)});
  REQUIRE(e.right);
  REQUIRE(e.bottom);
  REQUIRE_FALSE(e.left);
  REQUIRE_FALSE(e.top);
  REQUIRE(count_on(e) == 2);

  // A zero-sized neighbour (a hidden pane, a collapsed panel) is not a region.
  REQUIRE(count_on(border_edges(rect(0, 0, 10, 10), {rect(10, 0, 0, 10), rect(0, 10, 10, 0)}))
          == 0);
}

// draw_border's edge mask and the bottom-row background override, checked on the
// grid itself. The mask is what stops two adjacent regions drawing two lines;
// the override is what makes a bar sitting on another region carry that region's
// colour instead of looking like a strip of the one above it.
#include "terminal.h"
#include "ui/ui.h"

TEST_CASE("A masked border inks only the sides it is given", "[jot][borders]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(20, 8);

  // The renderer keeps one column of margin on the right, so a full-width box is
  // clamped and its right edge lands at 18, not 19.
  const int right_x = ui.get_render_width() - 1;

  // Bottom only: a rule with no corner glyphs at its ends, so it reads as a
  // separator rather than as the bottom of a box.
  UIBorderEdges bottom_only{false, false, true, false};
  ui.draw_border(UIRect{0, 2, 20, 6}, 8, 0, bottom_only);
  REQUIRE(ui.cell_at(0, 7)->ch == "─");
  REQUIRE(ui.cell_at(right_x, 7)->ch == "─");
  REQUIRE(ui.cell_at(10, 7)->ch == "─");
  // ...and nothing above it.
  REQUIRE(ui.cell_at(0, 2)->ch == " ");
  REQUIRE(ui.cell_at(right_x, 2)->ch == " ");
  REQUIRE(ui.cell_at(0, 5)->ch == " ");

  // Right only: a plain vertical, again with no corners.
  ui.clear();
  UIBorderEdges right_only{false, true, false, false};
  ui.draw_border(UIRect{0, 0, 20, 8}, 8, 0, right_only);
  REQUIRE(ui.cell_at(right_x, 0)->ch == "│");
  REQUIRE(ui.cell_at(right_x, 7)->ch == "│");
  REQUIRE(ui.cell_at(0, 0)->ch == " ");

  // All sides: real corners.
  ui.clear();
  ui.draw_border(UIRect{0, 0, 20, 8}, 8, 0);
  REQUIRE(ui.cell_at(0, 0)->ch == "┌");
  REQUIRE(ui.cell_at(right_x, 0)->ch == "┐");
  REQUIRE(ui.cell_at(0, 7)->ch == "└");
  REQUIRE(ui.cell_at(right_x, 7)->ch == "┘");
}

TEST_CASE("The bottom row can take a different background", "[jot][borders]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(20, 8);

  const int kStatusBg = 236;
  const int right_x = ui.get_render_width() - 1;

  // A right edge and a bottom edge: the vertical keeps the region's background,
  // the bar row takes the neighbour's.
  UIBorderEdges right_and_bottom{false, true, true, false};
  ui.draw_border(UIRect{0, 0, 20, 8}, 8, /*bg=*/234, right_and_bottom, kStatusBg);
  REQUIRE(ui.cell_at(right_x, 6)->bg == 234); // vertical, above the bar
  REQUIRE(ui.cell_at(5, 7)->bg == kStatusBg); // the bar itself
  REQUIRE(ui.cell_at(right_x, 7)->bg == kStatusBg);

  // Without the override the whole box uses the single background.
  ui.clear();
  UIBorderEdges bottom_only{false, false, true, false};
  ui.draw_border(UIRect{0, 0, 20, 8}, 8, /*bg=*/234, bottom_only);
  REQUIRE(ui.cell_at(5, 7)->bg == 234);
}

TEST_CASE("The right-edge render margin is configurable and defaults to full width",
          "[jot][borders]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(20, 8);

  // Default: the whole width is paintable, so a right edge lands on the last
  // column rather than leaving a blank strip down the side of the frame.
  REQUIRE(term.render_margin() == 0);
  REQUIRE(ui.get_render_width() == 20);
  UIBorderEdges right_only{false, true, false, false};
  ui.draw_border(UIRect{0, 0, 20, 8}, 8, 0, right_only);
  REQUIRE(ui.cell_at(19, 3)->ch == "│");

  // The escape hatch for a terminal that corrupts the frame when its last
  // column is written: one column is dropped from the paintable width.
  term.set_render_margin(1);
  REQUIRE(ui.get_render_width() == 19);
  term.set_render_margin(0);
  REQUIRE(ui.get_render_width() == 20);
  term.set_render_margin(-3);
  REQUIRE(ui.get_render_width() == 20);
}
