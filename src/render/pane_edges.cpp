#include "render/pane_edges.h"

#include <algorithm>

namespace pane_layout
{
  bool spans_vertically(const UIRect &a, const UIRect &b)
  {
    const int top = std::max(a.y, b.y);
    const int bottom = std::min(a.y + a.h, b.y + b.h);
    return bottom > top;
  }

  bool spans_horizontally(const UIRect &a, const UIRect &b)
  {
    const int left = std::max(a.x, b.x);
    const int right = std::min(a.x + a.w, b.x + b.w);
    return right > left;
  }

  UIBorderEdges border_edges(const UIRect &rect, const std::vector<UIRect> &neighbours)
  {
    UIBorderEdges edges;
    edges.top = false;
    edges.left = false;
    edges.right = false;
    edges.bottom = false;

    const int right_col = rect.x + rect.w;
    const int bottom_row = rect.y + rect.h;

    for (const UIRect &other : neighbours)
    {
      if (other.w <= 0 || other.h <= 0)
      {
        continue;
      }
      // Separator to the right: the neighbour starts exactly where this region
      // ends, and the two share rows. When the neighbour also ends on our bottom
      // row it inks its own bottom edge there, so our corner is a T-junction and
      // the two bottom edges read as one continuous line.
      if (!edges.right && other.x == right_col && spans_vertically(rect, other))
      {
        edges.right = true;
        edges.join_right = (other.y + other.h) == bottom_row;
      }
      // The mirror case: a region to our left ending on our bottom row continues
      // its own bottom edge into ours.
      if (!edges.join_left && other.x + other.w == rect.x && spans_vertically(rect, other)
          && (other.y + other.h) == bottom_row)
      {
        edges.join_left = true;
      }
      // ...and below, sharing columns.
      if (!edges.bottom && other.y == bottom_row && spans_horizontally(rect, other))
      {
        edges.bottom = true;
      }
      if (edges.right && edges.bottom && edges.join_left)
      {
        break;
      }
    }
    return edges;
  }
} // namespace pane_layout
