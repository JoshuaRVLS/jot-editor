#ifndef JOT_STATE_TABLINE_STATE_H
#define JOT_STATE_TABLINE_STATE_H

#include "ui/tab_order.h" // TabOrder, TabInsert

// The workspace tab strip: row 0, above the panes.
//
// The strip shows every buffer once, in the workspace's own order. A lone pane
// draws no strip of its own, so the workspace strip is the tab UI there; when
// the pane area is split the groups draw their own lists on that row and the
// workspace strip steps aside (see Editor::pane_shows_tab_strip), while the
// order -- and the keys that walk it -- stay live. See ui/tab_order.h for the
// order itself; this is the presentation state around it: the config knobs,
// where the strip is scrolled to, and what the pointer is doing to it.
//
// Split out of editor_state.h (the umbrella over src/editor/state/), like the
// other domain groups.
struct TablineState
{
  // The workspace's buffer order, its pins and its jump letters
  // (ui/tab_order.h).
  TabOrder tab_order;
  // `tabline` / `tabline_auto_hide`: the strip hides itself when there are this
  // many tabs or fewer (0 = never hide).
  bool tabline_visible = true;
  int tabline_auto_hide = 0;
  // `tabline_insert`: where a buffer the strip has not seen before lands.
  TabInsert tabline_insert = TabInsert::TAB_INSERT_AFTER_CURRENT;
  // The first tab shown when the strip overflows (the ‹N / ›+N chips report the
  // rest), and the tab the pointer is over (a hover highlight, never a switch).
  int tabline_scroll_index = 0;
  int tabline_hover_index = -1;
  // Jump-to-buffer mode (barbar's magic picking): each tab shows the letter it
  // answers to and typing one switches to it.
  bool tabline_jump_mode = false;
  // Dragging a tab along the strip: its position, the pointer's column, and
  // whether the pointer moved far enough to count as a drag rather than a click.
  int tabline_drag_index = -1;
  int tabline_drag_x = -1;
  bool tabline_drag_moved = false;
};

#endif
