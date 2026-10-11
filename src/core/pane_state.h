#ifndef JOT_STATE_PANE_STATE_H
#define JOT_STATE_PANE_STATE_H

#include "core/buffer.h" // FileBuffer
#include "core/panels.h" // EditorFocus
#include "core/panes.h"  // SplitPane, PaneTreeNode, PaneLayoutMode
#include <vector>

// The pane area: the open buffers, the split tree that lays them out, which
// pane and buffer are current, the tab strip above them, and the live drags
// that resize or zoom the layout.
//
// Split out of editor_state.h (which is now the umbrella over src/core/);
// the members and their comments moved verbatim.
struct PaneState
{
  std::vector<FileBuffer> buffers;
  std::vector<SplitPane> panes;
  std::vector<float> pane_weights;
  std::vector<PaneTreeNode> pane_tree;
  int pane_root = 0;
  int current_pane = 0;
  int current_buffer = 0;
  PaneLayoutMode pane_layout_mode = PANE_LAYOUT_SINGLE;

  EditorFocus focus_state = FOCUS_EDITOR;

  // Tab strip: its height in cells, the tab width the renderer measured, the
  // first visible tab when the strip overflows, and the buffer a preview tab
  // (single-click open) is showing.
  int tab_height = 0;
  // The breadcrumb winbar's height in cells (0 when it is off or the buffer on
  // screen has none). A pane's own header is tab_height + this, resolved once
  // per pane in update_pane_layout and read back through pane_content_top /
  // pane_viewport_h.
  int winbar_height = 0;
  int tab_size = 0;
  int tab_scroll_index = 0;
  int preview_buffer_index = 0;

  // Per-pane tab strips (VSCode editor groups): when a pane shows one, which
  // tab the pointer is over, and the live drag of one tab between panes. A drag
  // starts on a strip tab and owns the pointer until the button is released;
  // the drop is recomputed on every motion so the hint can name the pane, slot
  // or edge the tab would land in.
  PaneTabStripMode pane_tabs_mode = PANE_TABS_AUTO;
  int pane_tab_hover_pane = -1;
  int pane_tab_hover_index = -1;
  bool pane_tab_drag_active = false;
  int pane_tab_drag_pane = -1;
  int pane_tab_drag_buffer = -1;
  int pane_tab_drag_x = -1;
  int pane_tab_drag_y = -1;
  bool pane_tab_drag_moved = false;
  PaneTabDrop pane_tab_drop;

  // Dragging a split boundary (pane_resize_*) and the zoom that expands one
  // pane over the others (VSCode's "maximize editor group").
  bool pane_resize_dragging = false;
  int pane_resize_node = 0;
  // The last press on a divider, so a second one inside the double-click
  // window resets that split (the gesture every desktop splitter has) instead
  // of grabbing it again. Node -1 means "no press yet".
  long long pane_resize_last_click_ms = 0;
  int pane_resize_last_click_node = -1;
  bool pane_resize_vertical = false;
  int pane_resize_start_pos = 0;
  float pane_resize_start_ratio = 0;
  bool pane_zoom_active; // true while one pane is expanded over the others
  int pane_zoom_pane;    // pane index expanded while zoomed (-1 when off)

  // Dragging a pane's scrollbar: the track and thumb geometry are captured on
  // press so the drag keeps working while the content scrolls under it.
  bool scrollbar_dragging = false;
  int scrollbar_drag_pane = 0;
  int scrollbar_drag_start_y = 0;
  int scrollbar_drag_start_scroll = 0;
  int scrollbar_drag_track_y = 0;
  int scrollbar_drag_track_h = 0;
  int scrollbar_drag_thumb_h = 0;
  int scrollbar_drag_max_scroll = 0;
};

#endif
