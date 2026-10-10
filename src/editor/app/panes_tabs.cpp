// Per-pane tab lists (VSCode editor groups): moving a tab between panes, the
// edge split a dropped tab asks for, and the tab-list repair both use.
//
// A pane's strip is its own `tab_buffer_ids` list with `buffer_id` active; the
// workspace strip on row 0 stays the window-wide list it has been since
// e1c23d3. Drags come from src/input/mouse/pane_tabs.cpp, which hands the
// resolved drop here. Every move keeps the one-buffer-per-pane invariant: a
// group whose last tab leaves closes (the VSCode merge), and a pane that loses
// its active tab falls back to a neighbour in its own list.
#include "core/editor.h"
#include <algorithm>
#include <string>

void Editor::apply_pane_tabs_setting()
{
  // `auto` (the strip arrives where the workspace strip cannot speak for the
  // pane: a split, or `tabline = false`) unless `always` / `off` says
  // otherwise; a typo keeps the default rather than turning the row on for
  // every pane.
  const std::string mode = config.get("pane_tabs", "auto");
  if (mode == "off")
  {
    pane_tabs_mode = PANE_TABS_OFF;
  }
  else if (mode == "always")
  {
    pane_tabs_mode = PANE_TABS_ALWAYS;
  }
  else
  {
    pane_tabs_mode = PANE_TABS_AUTO;
  }
  if (!panes.empty())
  {
    refresh_pane_headers();
  }
}

void Editor::refresh_pane_headers()
{
  for (SplitPane &pane : panes)
  {
    pane.header_height = pane_header_height(pane);
  }
}

bool Editor::detach_pane_tab(int pane, int buffer_id)
{
  if (pane < 0 || pane >= (int)panes.size())
  {
    return false;
  }
  SplitPane &p = panes[(size_t)pane];
  const int index = find_local_tab_index(p, buffer_id);
  if (index < 0)
  {
    return p.tab_buffer_ids.empty();
  }
  p.tab_buffer_ids.erase(p.tab_buffer_ids.begin() + index);
  if (p.tab_buffer_ids.empty())
  {
    return true;
  }
  if (p.buffer_id == buffer_id)
  {
    // The tab that slid into its place: the one that was to the right, or the
    // last tab when it was the rightmost.
    const int next = std::clamp(index, 0, (int)p.tab_buffer_ids.size() - 1);
    p.buffer_id = p.tab_buffer_ids[(size_t)next];
    if (pane == current_pane)
    {
      current_buffer = p.buffer_id;
      restore_pane_view(pane);
    }
  }
  return false;
}

bool Editor::move_buffer_to_pane(int buffer_id, int source_pane, int target_pane, int slot)
{
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    return false;
  }
  if (source_pane < 0 || source_pane >= (int)panes.size() || target_pane < 0
      || target_pane >= (int)panes.size())
  {
    return false;
  }

  if (source_pane == target_pane)
  {
    // Same group: the drop reorders the tab, it does not move it anywhere.
    SplitPane &pane = panes[(size_t)source_pane];
    const int from = find_local_tab_index(pane, buffer_id);
    if (from < 0)
    {
      return false;
    }
    int to = std::clamp(slot, 0, (int)pane.tab_buffer_ids.size());
    pane.tab_buffer_ids.erase(pane.tab_buffer_ids.begin() + from);
    if (to > from)
    {
      to--;
    }
    to = std::clamp(to, 0, (int)pane.tab_buffer_ids.size());
    pane.tab_buffer_ids.insert(pane.tab_buffer_ids.begin() + to, buffer_id);
    needs_redraw = true;
    return true;
  }

  const bool source_was_current = source_pane == current_pane;
  const int source_index = source_pane;
  const bool source_empty = detach_pane_tab(source_pane, buffer_id);

  int target = target_pane;
  if (source_empty)
  {
    // The group held only this tab: it closes as the tab leaves. Focus it so
    // close_pane() acts on the right group, then remap the target when it sat
    // to the right of the one that went away.
    activate_pane(source_index);
    const int before = (int)panes.size();
    close_pane();
    if ((int)panes.size() == before)
    {
      return false;
    }
    if (source_index < target)
    {
      target--;
    }
  }
  else if (source_was_current)
  {
    // The live fields follow the pane's new active tab.
    current_buffer = panes[(size_t)source_index].buffer_id;
    restore_pane_view(source_index);
    clamp_cursor(current_buffer);
  }

  SplitPane &dst = panes[(size_t)target];
  const int existing = find_local_tab_index(dst, buffer_id);
  if (existing >= 0)
  {
    // Two panes may show the same buffer; a copy already in the target is the
    // tab being moved, so it leaves its old slot first.
    dst.tab_buffer_ids.erase(dst.tab_buffer_ids.begin() + existing);
    if (slot > existing)
    {
      slot--;
    }
  }
  slot = std::clamp(slot, 0, (int)dst.tab_buffer_ids.size());
  dst.tab_buffer_ids.insert(dst.tab_buffer_ids.begin() + slot, buffer_id);
  dst.buffer_id = buffer_id;

  activate_pane(target);
  current_buffer = panes[(size_t)current_pane].buffer_id;
  clamp_cursor(current_buffer);
  ensure_cursor_visible();
  refresh_pane_headers();
  note_editing_activity(false);
  needs_redraw = true;
  return true;
}

bool Editor::split_pane_with_buffer(int buffer_id, int source_pane, int target_pane, int edge)
{
  int dx = 0;
  int dy = 0;
  switch (edge)
  {
  case 1:
    dx = -1;
    break;
  case 2:
    dx = 1;
    break;
  case 3:
    dy = -1;
    break;
  case 4:
    dy = 1;
    break;
  default:
    return false;
  }
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    return false;
  }
  if (source_pane < 0 || source_pane >= (int)panes.size() || target_pane < 0
      || target_pane >= (int)panes.size())
  {
    return false;
  }

  const bool held_only_tab = panes[(size_t)source_pane].tab_buffer_ids.size() <= 1
                             && find_local_tab_index(panes[(size_t)source_pane], buffer_id) >= 0;
  if (held_only_tab && source_pane == target_pane)
  {
    // A group's only tab dropped on its own edge: split in place and keep the
    // tab in the half it came from. Moving it would leave an empty group, and
    // there is no other group to merge into.
    if (split_pane_at(target_pane, dx, dy, buffer_id, true, false) < 0)
    {
      return false;
    }
    refresh_pane_headers();
    message = "Moved tab to a new pane";
    needs_redraw = true;
    return true;
  }

  int target = target_pane;
  if (held_only_tab)
  {
    // The source group empties as the tab leaves: close it first, then split
    // the target (whose index shifts when it sat to the right of it).
    activate_pane(source_pane);
    detach_pane_tab(source_pane, buffer_id);
    const int before = (int)panes.size();
    close_pane();
    if ((int)panes.size() == before)
    {
      return false;
    }
    if (source_pane < target)
    {
      target--;
    }
  }
  else
  {
    detach_pane_tab(source_pane, buffer_id);
    if (source_pane == current_pane)
    {
      current_buffer = panes[(size_t)source_pane].buffer_id;
      restore_pane_view(source_pane);
      clamp_cursor(current_buffer);
    }
  }

  if (split_pane_at(target, dx, dy, buffer_id, false, false) < 0)
  {
    return false;
  }
  refresh_pane_headers();
  ensure_cursor_visible();
  note_editing_activity(false);
  message = "Moved tab to a new pane";
  needs_redraw = true;
  return true;
}

void Editor::move_pane_tab(int buffer_id, int source_pane, const PaneTabDrop &drop)
{
  if (drop.kind == PANE_TAB_DROP_INSERT)
  {
    move_buffer_to_pane(buffer_id, source_pane, drop.pane, drop.slot);
  }
  else if (drop.kind == PANE_TAB_DROP_EDGE)
  {
    split_pane_with_buffer(buffer_id, source_pane, drop.pane, drop.edge);
  }
}

bool Editor::switch_pane_to_local_tab(int pane, int target_index)
{
  if (pane < 0 || pane >= (int)panes.size())
  {
    return false;
  }
  if (panes[(size_t)pane].tab_buffer_ids.empty())
  {
    return false;
  }
  if (pane != current_pane)
  {
    activate_pane(pane);
  }
  // switch_to_local_tab reads the focused pane and keeps the workspace strip on
  // the tab that just arrived in front.
  return switch_to_local_tab(target_index);
}

bool Editor::scroll_pane_tabs(int pane, int delta)
{
  if (pane < 0 || pane >= (int)panes.size() || delta == 0)
  {
    return false;
  }
  SplitPane &p = panes[(size_t)pane];
  const int total = (int)p.tab_buffer_ids.size();
  if (total <= 0)
  {
    return false;
  }
  // The layout answers what is hidden: a window that shows every tab has
  // nothing to scroll to (the workspace strip's guard).
  const PaneTabLayout before_layout = build_pane_tab_layout(pane);
  if (before_layout.hidden_before + before_layout.hidden_after == 0)
  {
    return false;
  }
  const int step = delta > 0 ? 1 : -1;
  const int before = p.tab_scroll_index;
  p.tab_scroll_index = std::clamp(p.tab_scroll_index + step, 0, std::max(0, total - 1));
  if (p.tab_scroll_index == before)
  {
    return false;
  }
  // A scroll that shows nothing new is not a scroll: the window must have
  // moved over a different tab.
  const PaneTabLayout after_layout = build_pane_tab_layout(pane);
  if (after_layout.segments.empty() || after_layout.first_index == before_layout.first_index)
  {
    p.tab_scroll_index = before;
    return false;
  }
  needs_redraw = true;
  return true;
}
