#ifndef JOT_MODEL_SIDEBAR_SECTIONS_H
#define JOT_MODEL_SIDEBAR_SECTIONS_H

#include <algorithm>

// The left column is one section -- the explorer tree -- under its header row,
// with the sidebar's footer kept on the column's last row. The geometry is
// computed here, once, so the painter, the mouse hit-test and the keyboard walk
// cannot disagree about where the list sits or how many rows it shows.
namespace jot_sidebar
{
  // The explorer's share of the column. `y` is -1 when it has no content rows
  // (collapsed, or the column too short to lay it out).
  struct SectionLayout
  {
    int header_y = -1;
    int y = -1;
    int h = 0;
  };

  struct Layout
  {
    SectionLayout explorer;
    int footer_y = -1;
  };

  // `top`/`height` is the side column, `explorer_open` whether the tree is
  // expanded, and `show_footer` whether the column's last row is the footer.
  // The tree takes everything the header and the footer leave: it is the
  // column's only list, and the painter clamps it to the rows it actually has.
  inline Layout compute(int top, int height, bool explorer_open, bool show_footer)
  {
    Layout layout;
    if (height <= 0)
    {
      return layout;
    }
    const int last = top + height - 1;
    layout.footer_y = show_footer ? last : -1;
    const int budget = height - (show_footer ? 1 : 0);
    if (budget <= 0)
    {
      return layout;
    }

    layout.explorer.header_y = top;
    const int rows = explorer_open ? std::max(0, budget - 1) : 0;
    layout.explorer.h = rows;
    layout.explorer.y = rows > 0 ? top + 1 : -1;
    return layout;
  }

  // The section's header row and the data-row index a click on row `y` lands
  // on: index -1 is the header itself, -2 means "not in this section".
  inline int index_in_section(const SectionLayout &section, int y, bool *is_header)
  {
    if (is_header)
    {
      *is_header = false;
    }
    if (y == section.header_y)
    {
      if (is_header)
      {
        *is_header = true;
      }
      return -1;
    }
    if (section.y < 0 || y < section.y || y >= section.y + section.h)
    {
      return -2;
    }
    return y - section.y;
  }
} // namespace jot_sidebar

#endif // JOT_MODEL_SIDEBAR_SECTIONS_H
