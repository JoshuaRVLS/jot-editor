#ifndef JOT_MODEL_SIDEBAR_SECTIONS_H
#define JOT_MODEL_SIDEBAR_SECTIONS_H

#include <algorithm>

// The left column stacks three sections -- the explorer tree, the outline and
// the timeline -- under a header row each, with the sidebar's footer kept on
// the column's last row. The geometry is computed here, once, so the painter,
// the mouse hit-test and the keyboard walk cannot disagree about where a
// section sits or how many rows it shows.
namespace jot_sidebar
{
  enum Section
  {
    SECTION_EXPLORER = 0,
    SECTION_OUTLINE = 1,
    SECTION_TIMELINE = 2,
    SECTION_COUNT = 3,
  };

  // One section's share of the column. `y` is -1 when the section has no
  // content rows (collapsed, or the column is too short to lay it out).
  struct SectionLayout
  {
    int header_y = -1;
    int y = -1;
    int h = 0;
  };

  struct Layout
  {
    SectionLayout explorer;
    SectionLayout outline;
    SectionLayout timeline;
    int footer_y = -1;
  };

  // A section's body never takes more than this many rows while the tree has
  // rows of its own to show, so the tree keeps the column's bulk however many
  // symbols or commits a file has. A short tree frees those rows: the sections
  // grow into them (up to what they actually have) rather than leaving the
  // column blank above a list that is hiding entries.
  constexpr int kSectionCap = 8;
  // ...and the tree itself never drops below this many rows while its section
  // is open, so the other two cannot squeeze the explorer out.
  constexpr int kTreeMinRows = 3;
  // The three section headers, plus the footer, are the column's fixed chrome.
  constexpr int kHeaderRows = 3;

  // `top`/`height` is the side column. `tree_count` is the explorer's display
  // rows (the root row plus the tree rows), and `outline_count`/`timeline_count`
  // are the data rows each section wants -- an empty section still wants the
  // one-row note that says so.
  inline Layout compute(int top,
                        int height,
                        int tree_count,
                        int outline_count,
                        int timeline_count,
                        bool explorer_open,
                        bool outline_open,
                        bool timeline_open,
                        bool show_footer)
  {
    Layout layout;
    if (height <= 0)
    {
      return layout;
    }
    const int last = top + height - 1;
    layout.footer_y = show_footer ? last : -1;
    int budget = height - (show_footer ? 1 : 0);
    if (budget <= 0)
    {
      return layout;
    }

    // Too short for three stacked headers plus the tree's floor: the explorer
    // keeps the column and the other two are not laid out at all.
    if (budget <= kHeaderRows + kTreeMinRows)
    {
      layout.explorer.header_y = top;
      layout.explorer.y = top + 1;
      layout.explorer.h = std::max(0, budget - 1);
      return layout;
    }

    const int content = budget - kHeaderRows;
    const int cap = std::clamp(content / 3, 2, kSectionCap);
    int outline_h = outline_open ? std::min(std::max(1, outline_count), cap) : 0;
    int timeline_h = timeline_open ? std::min(std::max(1, timeline_count), cap) : 0;
    int tree_h = content - outline_h - timeline_h;

    // The tree is the column's flex -- it keeps whatever the other two do not
    // want -- but only while it has rows of its own to spend it on. What it
    // cannot use goes back to the sections the cap trimmed, so a file with more
    // symbols than the cap shows them instead of sitting above blank rows, and
    // a folded explorer hands the whole column back. Whatever the other
    // sections do not want either stays as the column's trailing blank.
    const int tree_want = explorer_open ? std::min(tree_h, tree_count) : 0;
    if (tree_h > tree_want)
    {
      int spare = tree_h - tree_want;
      if (outline_open)
      {
        const int take = std::min(spare, std::max(0, outline_count - outline_h));
        outline_h += take;
        spare -= take;
      }
      if (timeline_open)
      {
        const int take = std::min(spare, std::max(0, timeline_count - timeline_h));
        timeline_h += take;
      }
      tree_h = content - outline_h - timeline_h;
    }

    // The tree's floor wins over the other sections: trim from the bottom up.
    if (explorer_open && tree_h < kTreeMinRows)
    {
      int need = kTreeMinRows - tree_h;
      const int cut_timeline = std::min(need, timeline_h);
      timeline_h -= cut_timeline;
      need -= cut_timeline;
      const int cut_outline = std::min(need, outline_h);
      outline_h -= cut_outline;
      tree_h = content - outline_h - timeline_h;
    }
    if (!explorer_open)
    {
      tree_h = 0;
    }

    int row = top;
    layout.explorer.header_y = row++;
    layout.explorer.h = tree_h;
    layout.explorer.y = tree_h > 0 ? row : -1;
    row += tree_h;
    layout.outline.header_y = row++;
    layout.outline.h = outline_h;
    layout.outline.y = outline_h > 0 ? row : -1;
    row += outline_h;
    layout.timeline.header_y = row++;
    layout.timeline.h = timeline_h;
    layout.timeline.y = timeline_h > 0 ? row : -1;
    return layout;
  }

  // The section whose chrome starts on row `y`, or -1. Headers count: a click
  // on a header toggles the section.
  inline int section_at(const Layout &layout, int y)
  {
    if (y == layout.explorer.header_y
        || (layout.explorer.y >= 0 && y >= layout.explorer.y
            && y < layout.explorer.y + layout.explorer.h))
    {
      return SECTION_EXPLORER;
    }
    if (y == layout.outline.header_y
        || (layout.outline.y >= 0 && y >= layout.outline.y
            && y < layout.outline.y + layout.outline.h))
    {
      return SECTION_OUTLINE;
    }
    if (y == layout.timeline.header_y
        || (layout.timeline.y >= 0 && y >= layout.timeline.y
            && y < layout.timeline.y + layout.timeline.h))
    {
      return SECTION_TIMELINE;
    }
    return -1;
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
