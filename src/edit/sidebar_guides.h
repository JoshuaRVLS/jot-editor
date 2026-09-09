// Sidebar tree indent guides (neo-tree style): every level owns a two-cell
// marker column. Directories show their expander chevron there; files show
// a bare vertical bar "│ " (non-last) or the foot "└ " (last child). Rows
// below the top level reserve the level-0 column with two spaces, then one
// two-cell slot per ancestor: "│ " while that ancestor's sibling run
// continues, "  " after its last sibling. Because the connector under an
// expanded folder is formed by the children's own markers, it stays visible
// even when the folder is the last child of its parent.
#pragma once

#include <string>

namespace sidebar_guides
{
  // Guide string for one row. `ancestor_guide` is the concatenation of the
  // ancestor slots (one 2-cell entry per ancestor level above this node,
  // starting at the parent's level); pass "" for top-level rows.
  inline std::string row_guide(bool is_dir,
                               bool expanded,
                               bool is_last,
                               int depth,
                               const std::string &ancestor_guide)
  {
    const std::string own = is_dir ? (expanded ? " " : " ")
                                  : (is_last ? "└ " : "│ ");
    if (depth == 0)
    {
      return own;
    }
    // The level-0 column is reserved for top-level markers; every deeper row
    // leaves it blank.
    return "  " + ancestor_guide + own;
  }

  // Guide slots to pass down to a node's children: the parent's own column
  // continues with "│ " while the parent has following siblings.
  inline std::string children_guide(bool is_last, const std::string &ancestor_guide)
  {
    return ancestor_guide + (is_last ? "  " : "│ ");
  }
} // namespace sidebar_guides