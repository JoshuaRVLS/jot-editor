// Per-pane tab strips: the click that switches a pane's tab, the drag that
// carries it to another pane, and the drop target the hint is painted from.
//
// The shape is the workspace strip's (src/render/tabs.cpp) applied to one
// pane's own list: a press both switches the tab and arms a drag, motions
// recompute where the tab would land, and the release performs it. Unlike the
// workspace strip the drag can leave the strip entirely -- that is the point:
// the drop may land in another pane's strip, in its body, or against one of its
// edges -- so a live drag owns every motion and release, wherever they happen.
#include "core/editor.h"
#include <algorithm>

bool Editor::pane_tab_strip_row_at(int x, int y, int &pane_index) const
{
  pane_index = -1;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    // A zoomed pane is the only one on screen; the others still have geometry
    // but nothing the pointer can see.
    if (pane_zoom_active && i != current_pane)
    {
      continue;
    }
    const SplitPane &pane = panes[(size_t)i];
    if (!pane_shows_tab_strip(pane))
    {
      continue;
    }
    if (y == pane.y && x >= pane.x && x < pane.x + std::max(1, pane.w))
    {
      pane_index = i;
      return true;
    }
  }
  return false;
}

bool Editor::handle_pane_tab_mouse(
    int x, int y, bool is_click, bool is_click_release, bool is_middle_click, bool is_motion)
{
  if (panes.empty() || !ui)
  {
    return false;
  }

  // A live drag owns the pointer until the button is released (the dispatcher's
  // right-click path cancels it before this runs).
  if (pane_tab_drag_active)
  {
    if (is_motion || is_click)
    {
      update_pane_tab_drag(x, y);
      return true;
    }
    if (is_click_release)
    {
      finish_pane_tab_drag();
      return true;
    }
    if (is_middle_click)
    {
      cancel_pane_tab_drag();
      return true;
    }
    return false;
  }

  int strip_pane = -1;
  if (!pane_tab_strip_row_at(x, y, strip_pane))
  {
    // The pointer left a strip: drop the hover band.
    if (is_motion && pane_tab_hover_pane != -1)
    {
      pane_tab_hover_pane = -1;
      pane_tab_hover_index = -1;
      needs_redraw = true;
    }
    return false;
  }

  const PaneTabLayout layout = build_pane_tab_layout(strip_pane);
  const int position = pane_tab_position_at(layout, x);

  if (is_motion)
  {
    const int hover = position >= 0 ? layout.segments[(size_t)position].tab_index : -1;
    if (pane_tab_hover_pane != strip_pane || pane_tab_hover_index != hover)
    {
      pane_tab_hover_pane = hover >= 0 ? strip_pane : -1;
      pane_tab_hover_index = hover;
      needs_redraw = true;
    }
    return true;
  }

  if (is_click_release)
  {
    // The release only closes a drag (handled above); on its own it is inert,
    // but it still belongs to the strip row.
    return true;
  }

  if (is_click)
  {
    const int chip = pane_tab_scroll_chip_at(layout, x);
    if (chip != 0)
    {
      if (scroll_pane_tabs(strip_pane, chip * std::max(1, (int)layout.segments.size() - 1)))
      {
        needs_redraw = true;
      }
      return true;
    }
  }

  if (position < 0)
  {
    // The strip's empty tail: consume, so a press there can never place a caret
    // on the row the strip owns.
    return true;
  }

  const PaneTabSegment &tab = layout.segments[(size_t)position];
  if ((is_click && !tab.hidden_close && x == layout.x + tab.close_x) || is_middle_click)
  {
    close_buffer_at(tab.buffer_id);
    pane_tab_hover_pane = -1;
    pane_tab_hover_index = -1;
    focus_state = FOCUS_EDITOR;
    restart_blink_pointer();
    needs_redraw = true;
    return true;
  }

  if (is_click)
  {
    // The press switches the tab (and focuses the pane) and arms a drag:
    // moving the pointer before releasing moves the tab, releasing without
    // moving leaves the switch as it was.
    switch_pane_to_local_tab(strip_pane, tab.tab_index);
    pane_tab_drag_active = true;
    pane_tab_drag_pane = strip_pane;
    pane_tab_drag_buffer = tab.buffer_id;
    pane_tab_drag_x = x;
    pane_tab_drag_y = y;
    pane_tab_drag_moved = false;
    pane_tab_drop = PaneTabDrop{};
    focus_state = FOCUS_EDITOR;
    restart_blink_pointer();
    needs_redraw = true;
    return true;
  }
  return true;
}

bool Editor::handle_pane_tab_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down)
{
  if (!is_scroll_up && !is_scroll_down)
  {
    return false;
  }
  int strip_pane = -1;
  if (!pane_tab_strip_row_at(x, y, strip_pane))
  {
    return false;
  }
  // The row is the strip's either way: a notch with nothing hidden is still
  // consumed, so it never scrolls the buffer behind the strip.
  if (scroll_pane_tabs(strip_pane, is_scroll_up ? -1 : 1))
  {
    needs_redraw = true;
  }
  return true;
}

PaneTabDrop Editor::pane_tab_drop_target_at(int x, int y) const
{
  PaneTabDrop drop;

  // A strip row: the tab lands where the slot marker would sit, in the target's
  // tab-list coordinates.
  int strip_pane = -1;
  if (pane_tab_strip_row_at(x, y, strip_pane))
  {
    const PaneTabLayout layout = build_pane_tab_layout(strip_pane);
    drop.kind = PANE_TAB_DROP_INSERT;
    drop.pane = strip_pane;
    drop.on_strip = true;
    drop.slot = (int)panes[(size_t)strip_pane].tab_buffer_ids.size();
    for (const PaneTabSegment &tab : layout.segments)
    {
      if (x - layout.x < tab.x + (tab.end_x - tab.x) / 2)
      {
        drop.slot = tab.tab_index;
        return drop;
      }
    }
    if (!layout.segments.empty())
    {
      drop.slot = layout.segments.back().tab_index + 1;
    }
    return drop;
  }

  // A pane's body: the outer band splits that side, anywhere else joins the
  // pane's tabs at the end (VSCode's group drop zones).
  for (int i = 0; i < (int)panes.size(); i++)
  {
    if (pane_zoom_active && i != current_pane)
    {
      continue;
    }
    const SplitPane &pane = panes[(size_t)i];
    if (x < pane.x || x >= pane.x + pane.w || y < pane.y || y >= pane.y + pane.h)
    {
      continue;
    }
    // Too small to split: every cell would be an edge, so the whole pane takes
    // the tab instead.
    const bool room_to_split = pane.w > 4 && pane.h > 4;
    if (room_to_split)
    {
      drop.pane = i;
      drop.kind = PANE_TAB_DROP_EDGE;
      if (x <= pane.x + 1)
      {
        drop.edge = 1;
        return drop;
      }
      if (x >= pane.x + pane.w - 2)
      {
        drop.edge = 2;
        return drop;
      }
      if (y <= pane.y + 2)
      {
        drop.edge = 3;
        return drop;
      }
      if (y >= pane.y + pane.h - 2)
      {
        drop.edge = 4;
        return drop;
      }
      drop.kind = PANE_TAB_DROP_NONE;
      drop.pane = -1;
    }
    drop.kind = PANE_TAB_DROP_INSERT;
    drop.pane = i;
    drop.on_strip = false;
    drop.slot = (int)pane.tab_buffer_ids.size();
    return drop;
  }
  return drop;
}

void Editor::update_pane_tab_drag(int x, int y)
{
  pane_tab_drag_x = x;
  pane_tab_drag_y = y;
  pane_tab_drag_moved = true;
  pane_tab_drop = pane_tab_drop_target_at(x, y);
  needs_redraw = true;
}

void Editor::finish_pane_tab_drag()
{
  if (!pane_tab_drag_active)
  {
    return;
  }
  const int buffer_id = pane_tab_drag_buffer;
  const int source_pane = pane_tab_drag_pane;
  const bool moved = pane_tab_drag_moved;
  const PaneTabDrop drop = pane_tab_drop;
  // The hint and the drag state go before the move: the move can close and
  // renumber panes, and a stale target would paint over the new layout.
  cancel_pane_tab_drag();
  if (moved && drop.kind != PANE_TAB_DROP_NONE)
  {
    move_pane_tab(buffer_id, source_pane, drop);
  }
}

void Editor::cancel_pane_tab_drag()
{
  if (!pane_tab_drag_active && pane_tab_drop.kind == PANE_TAB_DROP_NONE)
  {
    return;
  }
  pane_tab_drag_active = false;
  pane_tab_drag_pane = -1;
  pane_tab_drag_buffer = -1;
  pane_tab_drag_x = -1;
  pane_tab_drag_y = -1;
  pane_tab_drag_moved = false;
  pane_tab_drop = PaneTabDrop{};
  needs_redraw = true;
}
