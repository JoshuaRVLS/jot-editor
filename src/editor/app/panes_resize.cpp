// Pane and sidebar resize interactions: hit tests, drag tracking, and split-ratio adjustment.
#include "editor.h"
#include "editor/app/panes_internal.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>

using namespace pane_internal;
bool Editor::collapsed_sidebar_handle_hit_test(int x, int y) const
{
  if (zen_mode || show_sidebar || show_home_menu || !ui || panes.empty())
  {
    return false;
  }
  if (x != 0)
  {
    return false;
  }
  const ContentColumn col = side_column();
  return y >= col.top && y < col.bottom;
}

bool Editor::sidebar_resize_hit_test(int x, int y) const
{
  if (collapsed_sidebar_handle_hit_test(x, y))
  {
    return true;
  }
  if (!show_sidebar || !ui)
  {
    return false;
  }
  int w = effective_sidebar_width();
  if (w < 2 || x != w - 1)
  {
    return false;
  }
  const ContentColumn col = side_column();
  return y >= col.top && y < col.bottom;
}

bool Editor::resize_current_pane(int delta)
{
  if (panes.size() < 2 || current_pane < 0 || current_pane >= (int)panes.size())
  {
    return false;
  }

  if (pane_zoom_active)
  {
    pane_zoom_active = false;
    pane_zoom_pane = -1;
    update_pane_layout();
  }

  std::function<int(int, int)> find_leaf = [&](int node_index, int pane_index) -> int
  {
    if (node_index < 0 || node_index >= (int)pane_tree.size())
    {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf)
    {
      return node.pane_index == pane_index ? node_index : -1;
    }
    int first = find_leaf(node.first, pane_index);
    return first >= 0 ? first : find_leaf(node.second, pane_index);
  };

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0)
  {
    return false;
  }
  int parent = pane_tree[leaf].parent;
  if (parent < 0 || parent >= (int)pane_tree.size())
  {
    return false;
  }
  return adjust_pane_split_ratio(parent, delta);
}

bool Editor::resize_current_pane_direction(char dir, int delta)
{
  if (panes.size() < 2 || current_pane < 0 || current_pane >= (int)panes.size())
  {
    return false;
  }

  char d = (char)std::tolower((unsigned char)dir);
  if (d != 'h' && d != 'j' && d != 'k' && d != 'l')
  {
    return false;
  }

  if (pane_zoom_active)
  {
    pane_zoom_active = false;
    pane_zoom_pane = -1;
    update_pane_layout();
  }

  // Directional resize is defined on the split tree, not on pixel probes:
  // the divider that borders the current pane in the pressed direction is
  // pushed one step that way (growing the current pane). If the pane already
  // touches the outer edge in that direction, the divider on its opposite
  // side is pushed the same way instead, which shrinks it -- so every press
  // predictably changes the active pane's size.
  std::function<int(int, int)> find_leaf = [&](int node_index, int pane_index) -> int
  {
    if (node_index < 0 || node_index >= (int)pane_tree.size())
    {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf)
    {
      return node.pane_index == pane_index ? node_index : -1;
    }
    int first = find_leaf(node.first, pane_index);
    return first >= 0 ? first : find_leaf(node.second, pane_index);
  };

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0)
  {
    return false;
  }

  std::function<bool(int, int)> subtree_holds = [&](int child, int target_leaf) -> bool
  {
    if (child < 0 || child >= (int)pane_tree.size())
    {
      return false;
    }
    const PaneTreeNode &node = pane_tree[child];
    if (node.leaf)
    {
      return child == target_leaf;
    }
    return subtree_holds(node.first, target_leaf) || subtree_holds(node.second, target_leaf);
  };

  // Horizontal keys move a vertical divider (node.vertical), vertical keys a
  // horizontal one. 'l'/'j' push the divider away from the first child,
  // 'h'/'k' toward it.
  const bool vertical_axis = (d == 'h' || d == 'l');
  const int sign = (d == 'l' || d == 'j') ? 1 : -1;
  const int step = std::max(1, std::abs(delta));

  // Climb from the current leaf: the lowest ancestor split along this axis
  // where the pane sits on the pushed-away side owns the divider we want;
  // the lowest one where it sits on the other side is the fallback when the
  // pane reaches the outer edge.
  int preferred = -1;
  int fallback = -1;
  int node = pane_tree[leaf].parent;
  while (node >= 0 && node < (int)pane_tree.size())
  {
    const PaneTreeNode &n = pane_tree[node];
    if (!n.leaf && n.vertical == vertical_axis)
    {
      const bool current_in_first = subtree_holds(n.first, leaf);
      const bool push_side_is_first = (d == 'l' || d == 'j');
      if (current_in_first == push_side_is_first)
      {
        if (preferred < 0)
        {
          preferred = node; // nearest such divider
        }
      }
      else if (fallback < 0)
      {
        fallback = node;
      }
    }
    node = n.parent;
  }

  int target = preferred >= 0 ? preferred : fallback;
  if (target < 0)
  {
    return false; // no split along this axis anywhere near the pane
  }
  // clamp_only keeps boundary presses (already at min/max) quiet no-ops
  // instead of reporting an error, like the mouse drag path.
  return adjust_pane_split_ratio(target, sign * step, true);
}

bool Editor::begin_sidebar_resize_drag(int x, int y)
{
  if (!sidebar_resize_hit_test(x, y))
  {
    return false;
  }
  bool opening_from_collapsed = collapsed_sidebar_handle_hit_test(x, y);
  sidebar_resize_dragging = true;
  sidebar_resize_opening = opening_from_collapsed;
  sidebar_resize_start_x = x;
  int open_width = effective_sidebar_width();
  sidebar_resize_start_width = opening_from_collapsed ? open_width : effective_sidebar_width();
  if (opening_from_collapsed)
  {
    if (file_tree.empty())
    {
      load_file_tree(root_dir);
    }
    show_sidebar = true;
    sidebar_width = open_width;
    update_pane_layout();
  }
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_SIDEBAR;
  set_message("Resizing file tree");
  needs_redraw = true;
  return true;
}

bool Editor::update_sidebar_resize_drag(int x)
{
  if (!sidebar_resize_dragging)
  {
    return false;
  }
  int requested_width = sidebar_resize_start_width + (x - sidebar_resize_start_x);
  if (!sidebar_resize_opening && requested_width <= sidebar_close_threshold())
  {
    sidebar_resize_dragging = false;
    sidebar_resize_opening = false;
    sidebar_resize_start_x = 0;
    sidebar_resize_start_width = min_sidebar_width();
    sidebar_width = min_sidebar_width();
    show_sidebar = false;
    if (focus_state == FOCUS_SIDEBAR)
    {
      focus_state = FOCUS_EDITOR;
    }
    update_pane_layout();
    set_message("File tree closed");
    needs_redraw = true;
    return true;
  }

  int next_width = std::clamp(requested_width, min_sidebar_width(), max_sidebar_width());
  if (sidebar_width == next_width)
  {
    return false;
  }
  sidebar_resize_opening = false;
  sidebar_width = next_width;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::end_sidebar_resize_drag()
{
  if (!sidebar_resize_dragging)
  {
    return;
  }
  sidebar_resize_dragging = false;
  sidebar_resize_opening = false;
  sidebar_resize_start_x = 0;
  sidebar_resize_start_width = effective_sidebar_width();
  sidebar_width = effective_sidebar_width();
  set_message("File tree resized");
  needs_redraw = true;
}

// --- Sidebar section boundaries -------------------------------------------
// The column stacks EXPLORER / OUTLINE / TIMELINE under a header row each, so
// the OUTLINE and TIMELINE headers sit on the two boundaries between them.
// Each of those rows is both the toggle and the splitter: a press arms the
// gesture (the toggle moves to the release), and the first vertical row of
// movement makes it a resize -- the boundary follows the pointer, the section
// above and the section below trade rows, and the heights are remembered in
// sidebar_outline_height / sidebar_timeline_height (0 = automatic share).
void Editor::sidebar_section_resize_press(int y)
{
  sidebar_section_resize_pending = false;
  const SidebarHit hit = sidebar_hit_test(y);
  if (!hit.header || hit.section == jot_sidebar::SECTION_EXPLORER)
  {
    return;
  }
  // Both sections the boundary separates have to be open: a closed one owns
  // no rows to trade, and its header stays a plain toggle.
  if (hit.section == jot_sidebar::SECTION_OUTLINE)
  {
    if (!sidebar_explorer_open || !sidebar_outline_open)
    {
      return;
    }
  }
  else if (!sidebar_outline_open || !sidebar_timeline_open)
  {
    return;
  }
  sidebar_section_resize_pending = true;
  sidebar_section_resize_boundary = (jot_sidebar::Section)hit.section;
  sidebar_section_resize_start_y = y;
}

bool Editor::begin_sidebar_section_resize_drag(int y)
{
  if (!sidebar_section_resize_pending)
  {
    return false;
  }
  const jot_sidebar::Layout sections = sidebar_sections_layout();
  const bool boundary_one = sidebar_section_resize_boundary == jot_sidebar::SECTION_OUTLINE;
  // Rebase on the header's current row: rows above may have shifted between
  // the press and this first movement (an async tree or symbol rebuild), and
  // starting from a stale row would jump the boundary.
  const int header_y = boundary_one ? sections.outline.header_y : sections.timeline.header_y;
  if (header_y < 0)
  {
    // Not laid out (column too short for three sections): keep the press
    // pending so the release is still the toggle, and try again on a later
    // movement.
    return false;
  }
  const int upper = boundary_one ? sections.explorer.h : sections.outline.h;
  const int lower = boundary_one ? sections.outline.h : sections.timeline.h;
  if (boundary_one)
  {
    // Boundary one moves the outline against the explorer; the timeline keeps
    // its rows, so the outline may grow until the explorer would hit its floor.
    const int content = side_column().h - 1 - jot_sidebar::kHeaderRows;
    const int hi = content - sections.timeline.h - jot_sidebar::kTreeMinRows;
    if (hi < 1)
    {
      return false;
    }
  }
  else if (upper + lower < 2)
  {
    // Boundary two trades rows between outline and timeline only: with fewer
    // than two rows between them there is nothing to move.
    return false;
  }
  sidebar_section_resize_dragging = true;
  sidebar_section_resize_pending = false;
  sidebar_section_resize_start_y = header_y;
  sidebar_section_resize_start_upper = upper;
  sidebar_section_resize_start_lower = lower;
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_SIDEBAR;
  needs_redraw = true;
  return true;
}

bool Editor::update_sidebar_section_resize_drag(int y)
{
  if (!sidebar_section_resize_dragging)
  {
    return false;
  }
  const int d = y - sidebar_section_resize_start_y;
  if (d == 0)
  {
    return false;
  }
  if (sidebar_section_resize_boundary == jot_sidebar::SECTION_OUTLINE)
  {
    // Boundary one: only the outline's height is stored -- the explorer is
    // the column's flex and takes the difference itself. The timeline is read
    // off the live layout so a section that changed while the drag was on
    // still keeps its rows.
    const jot_sidebar::Layout sections = sidebar_sections_layout();
    const int content = side_column().h - 1 - jot_sidebar::kHeaderRows;
    const int hi = content - sections.timeline.h - jot_sidebar::kTreeMinRows;
    const int next =
        std::clamp(sidebar_section_resize_start_lower - d, 1, std::max(1, hi));
    if (next == sidebar_outline_height)
    {
      return false;
    }
    sidebar_outline_height = next;
  }
  else
  {
    // Boundary two: outline and timeline trade rows between themselves while
    // the explorer above stays put, so their sum is the constant this drag
    // started with -- storing both heights is what keeps the upper boundary
    // from creeping along with the lower one.
    const int sum = sidebar_section_resize_start_upper + sidebar_section_resize_start_lower;
    const int timeline = std::clamp(sidebar_section_resize_start_lower - d, 1, sum - 1);
    if (timeline == sidebar_timeline_height && (sum - timeline) == sidebar_outline_height)
    {
      return false;
    }
    sidebar_timeline_height = timeline;
    sidebar_outline_height = sum - timeline;
  }
  needs_redraw = true;
  return true;
}

void Editor::end_sidebar_section_resize_drag()
{
  if (!sidebar_section_resize_dragging)
  {
    return;
  }
  sidebar_section_resize_dragging = false;
  set_message("Sidebar sections resized");
  needs_redraw = true;
}

bool Editor::begin_terminal_resize_drag(int x, int y)
{
  if (terminal_zoom_active || !show_integrated_terminal || !ui)
  {
    return false;
  }
  // The handle is the rule above the panel. That row is the panel's own first
  // row -- its reservation includes it -- so the pane area's last row is a code
  // row instead of the separator. Dragging the rule up grows the terminal,
  // dragging it down shrinks it.
  if (y != integrated_terminal_panel_y() - 1 || x < 0 || x >= integrated_terminal_panel_w())
  {
    return false;
  }
  terminal_resize_dragging = true;
  terminal_resize_start_y = y;
  terminal_resize_start_height = integrated_terminal_height;
  needs_redraw = true;
  return true;
}

bool Editor::update_terminal_resize_drag(int y)
{
  if (!terminal_resize_dragging || !ui)
  {
    return false;
  }
  // Same bounds as integrated_terminal_panel_h: keep at least 5 rows of
  // panes and never grow past the status line.
  int max_h = std::max(5, ui->get_height() - status_height - pane_area_top() - 5);
  int requested = terminal_resize_start_height + (terminal_resize_start_y - y);
  int next = std::clamp(requested, 5, max_h);
  if (integrated_terminal_height == next)
  {
    return false;
  }
  integrated_terminal_height = next;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::end_terminal_resize_drag()
{
  if (!terminal_resize_dragging)
  {
    return;
  }
  terminal_resize_dragging = false;
  terminal_resize_start_y = 0;
  terminal_resize_start_height = 0;
  set_message("Terminal resized");
  needs_redraw = true;
}

bool Editor::right_panel_resize_hit_test(int x, int y) const
{
  if (!show_right_panel || !ui)
  {
    return false;
  }
  int w = effective_right_panel_width();
  if (w < min_right_panel_width())
  {
    return false;
  }
  int handle_x = ui->get_render_width() - w;
  if (x != handle_x)
  {
    return false;
  }
  int top = side_column_top();
  int bottom = ui->get_height() - status_height;
  return y >= top && y < bottom;
}

bool Editor::begin_right_panel_resize_drag(int x, int y)
{
  if (!right_panel_resize_hit_test(x, y))
  {
    return false;
  }
  right_panel_resize_dragging = true;
  right_panel_resize_start_x = x;
  right_panel_resize_start_width = effective_right_panel_width();
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_EDITOR;
  set_message("Resizing right panel");
  needs_redraw = true;
  return true;
}

bool Editor::update_right_panel_resize_drag(int x)
{
  if (!right_panel_resize_dragging)
  {
    return false;
  }
  int requested_width = right_panel_resize_start_width + (right_panel_resize_start_x - x);
  int max_w = max_right_panel_width();
  int min_w = std::min(min_right_panel_width(), max_w);
  int next_width = std::clamp(requested_width, min_w, max_w);
  if (right_panel_width == next_width)
  {
    return false;
  }
  right_panel_width = next_width;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::end_right_panel_resize_drag()
{
  if (!right_panel_resize_dragging)
  {
    return;
  }
  right_panel_resize_dragging = false;
  right_panel_resize_start_x = 0;
  right_panel_resize_start_width = effective_right_panel_width();
  right_panel_width = effective_right_panel_width();
  set_message("Right panel resized");
  needs_redraw = true;
}

bool Editor::adjust_pane_split_ratio(int node_index, int delta, bool clamp_only)
{
  if (node_index < 0 || node_index >= (int)pane_tree.size() || pane_tree[node_index].leaf)
  {
    return false;
  }

  PaneTreeNode &node = pane_tree[node_index];
  int first_pane = -1;
  int second_pane = -1;
  std::function<int(int)> first_pane_in_node = [&](int child) -> int
  {
    if (child < 0 || child >= (int)pane_tree.size())
    {
      return -1;
    }
    const PaneTreeNode &n = pane_tree[child];
    if (n.leaf)
    {
      return (n.pane_index >= 0 && n.pane_index < (int)panes.size()) ? n.pane_index : -1;
    }
    int first = first_pane_in_node(n.first);
    return first >= 0 ? first : first_pane_in_node(n.second);
  };

  first_pane = first_pane_in_node(node.first);
  second_pane = first_pane_in_node(node.second);
  if (first_pane < 0 || second_pane < 0)
  {
    return false;
  }

  int total_dim = node.vertical ? (panes[first_pane].w + panes[second_pane].w)
                                : (panes[first_pane].h + panes[second_pane].h);
  const int min_dim = node.vertical ? kMinPaneWidth : kMinPaneHeight;
  if (total_dim < min_dim * 2)
  {
    return false;
  }

  int first_dim = std::clamp((int)(node.ratio * total_dim), min_dim, total_dim - min_dim);
  int next_first = std::clamp(first_dim + delta, min_dim, total_dim - min_dim);
  if (!clamp_only && next_first == first_dim)
  {
    return false;
  }

  node.ratio = (float)next_first / (float)std::max(1, total_dim);
  pane_layout_mode = node.vertical ? PANE_LAYOUT_VERTICAL : PANE_LAYOUT_HORIZONTAL;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

bool Editor::begin_pane_resize_drag(int x, int y)
{
  int node = pane_split_at_position(x, y);
  if (node < 0 || node >= (int)pane_tree.size())
  {
    return false;
  }
  pane_resize_dragging = true;
  pane_resize_node = node;
  pane_resize_vertical = pane_tree[node].vertical;
  pane_resize_start_pos = pane_resize_vertical ? x : y;
  pane_resize_start_ratio = pane_tree[node].ratio;
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_EDITOR;
  set_message("Resizing pane");
  needs_redraw = true;
  return true;
}

bool Editor::update_pane_resize_drag(int x, int y)
{
  if (!pane_resize_dragging || pane_resize_node < 0 || pane_resize_node >= (int)pane_tree.size())
  {
    return false;
  }
  int pos = pane_resize_vertical ? x : y;
  int delta = pos - pane_resize_start_pos;
  pane_tree[pane_resize_node].ratio = pane_resize_start_ratio;
  return adjust_pane_split_ratio(pane_resize_node, delta, true);
}

void Editor::end_pane_resize_drag()
{
  if (!pane_resize_dragging)
  {
    return;
  }
  pane_resize_dragging = false;
  pane_resize_node = -1;
  pane_resize_vertical = false;
  pane_resize_start_pos = 0;
  pane_resize_start_ratio = 0.5f;
  set_message("Pane resized");
  needs_redraw = true;
}
