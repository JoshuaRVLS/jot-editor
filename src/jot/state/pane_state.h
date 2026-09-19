#ifndef JOT_STATE_PANE_STATE_H
#define JOT_STATE_PANE_STATE_H

#include "jot/model/buffer.h" // FileBuffer
#include "jot/model/panels.h" // EditorFocus
#include "jot/model/panes.h"  // SplitPane, PaneTreeNode, PaneLayoutMode
#include <vector>

// The pane area: the open buffers, the split tree that lays them out, which
// pane and buffer are current, the tab strip above them, and the live drags
// that resize or zoom the layout.
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
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
  int tab_size = 0;
  int tab_scroll_index = 0;
  int preview_buffer_index = 0;

  // Dragging a split boundary (pane_resize_*) and the zoom that expands one
  // pane over the others (VSCode's "maximize editor group").
  bool pane_resize_dragging = false;
  int pane_resize_node = 0;
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
