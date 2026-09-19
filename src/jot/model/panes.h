#ifndef JOT_MODEL_PANES_H
#define JOT_MODEL_PANES_H

#include "jot/model/buffer.h"
#include <vector>
struct PaneTreeNode
{
  bool leaf = true;
  int pane_index = -1;
  int parent = -1;
  int first = -1;
  int second = -1;
  bool vertical = true;
  float ratio = 0.5f;
};

struct SplitPane
{
  int x, y, w, h;
  int buffer_id;
  bool active;
  int tab_scroll_index = 0;
  std::vector<int> tab_buffer_ids;
  // Per-pane view of the buffer it shows. Two panes can display the same
  // buffer with independent cursor/scroll/selection (window-style views); the
  // live FileBuffer fields always mirror the *active* pane's view and these
  // fields hold each inactive pane's own view. view_buffer_id is the buffer
  // the stored view belongs to (-1 when the pane has never captured one).
  int view_buffer_id = -1;
  Cursor view_cursor{0, 0};
  int view_preferred_x = 0;
  Selection view_selection{{0, 0}, {0, 0}, false};
  int view_scroll_offset = 0;
  int view_scroll_x = 0;
};

enum PaneLayoutMode
{
  PANE_LAYOUT_SINGLE,
  PANE_LAYOUT_VERTICAL,
  PANE_LAYOUT_HORIZONTAL
};

struct FileTabSegment
{
  int buffer_id = -1;
  int tab_index = -1;
  int x = 0;
  int label_x = 0;
  int close_x = 0;
  int end_x = 0;
  std::string label;
  // Per-language file glyph painted ahead of the label in its brand color
  // (shared with the status line / explorer); empty for directories and
  // unnamed buffers, -1 color = use the tab foreground.
  std::string icon;
  int icon_fg = -1;
  bool active = false;
  bool modified = false;
  bool preview = false;
  std::string git_status;
};

struct FileTabLayout
{
  int x = 0;
  int y = 0;
  int w = 0;
  std::vector<FileTabSegment> segments;
  std::string scroll_left_label;
  std::string overflow_label;
  int overflow_x = 0;
  int scroll_left_x = -1;
  int scroll_left_end_x = -1;
  int scroll_right_x = -1;
  int scroll_right_end_x = -1;
  int hidden_before = 0;
  int hidden_after = 0;
  int hidden_count = 0;
};

#endif
