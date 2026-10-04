#ifndef JOT_MODEL_PANES_H
#define JOT_MODEL_PANES_H

#include "jot/model/buffer.h"
#include <algorithm>
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
  // The chrome rows this pane spends above its text: the local tab strip and
  // the winbar (see Editor::update_pane_layout, the one place that sets it).
  // Every "where does the text start / how many rows does it have" question
  // goes through pane_content_top/pane_viewport_h below so a pane that pays a
  // winbar row does not have to be threaded through each call site.
  int header_height = 1;
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

// The first row of a pane's text and the rows it has to paint text in. Take a
// pane, not a bare height: the header is the pane's own (a buffer showing a
// breadcrumb pays one row more than one that does not).
inline int pane_content_top(const SplitPane &pane)
{
  return pane.y + pane.header_height;
}

inline int pane_viewport_h(const SplitPane &pane)
{
  return std::max(0, pane.h - pane.header_height);
}

enum PaneLayoutMode
{
  PANE_LAYOUT_SINGLE,
  PANE_LAYOUT_VERTICAL,
  PANE_LAYOUT_HORIZONTAL
};

// When a pane draws its own tab strip (a VSCode editor group). `auto` keeps
// the strip off until a pane holds a second buffer, so a single-buffer pane
// keeps the layout it had before the strip existed; `always` shows it for
// every pane that holds a buffer; `off` disables it everywhere.
enum PaneTabStripMode
{
  PANE_TABS_AUTO,
  PANE_TABS_ALWAYS,
  PANE_TABS_OFF
};

// One tab on a pane's own strip: the buffer it shows, where it was measured,
// and the state the painter draws (active, modified, preview, hovered). This is
// the FileTabSegment shape per pane; the workspace strip (row 0) stays the
// window-wide list it has been since e1c23d3.
struct PaneTabSegment
{
  int buffer_id = -1;
  int tab_index = -1;
  int x = 0;
  int end_x = 0;
  int label_x = 0;
  int close_x = 0;
  // The × only earns a cell while it lands on the label's padding, the rule
  // the workspace strip's tabs follow.
  bool hidden_close = false;
  bool active = false;
  bool modified = false;
  bool preview = false;
  bool hovered = false;
  std::string label;
  std::string icon;
  int icon_fg = -1;
};

// A pane's strip as the last frame laid it out. The painter and the mouse
// hit-tests read the same struct, so a tab's pixels and its clickable extent
// cannot drift.
struct PaneTabLayout
{
  int pane = -1;
  int x = 0;
  int y = 0;
  int w = 0;
  std::vector<PaneTabSegment> segments;
  int first_index = 0;   // pane.tab_scroll_index this layout was built from
  int hidden_before = 0; // tabs scrolled off the left edge
  int hidden_after = 0;  // tabs that do not fit on the right
  std::string scroll_left_label;
  int scroll_left_x = -1;
  int scroll_left_end_x = -1;
  std::string overflow_label;
  int overflow_x = -1;
};

// Where a dragged pane tab would land. `insert` moves the tab into the target
// pane's list at `slot` (its strip row, or anywhere in its body); `edge` splits
// the pane and plants the tab in the new group (edge: 1 left, 2 right, 3 up,
// 4 down). `on_strip` says the pointer is on the target's strip row, where the
// painter draws a slot marker instead of the pane-wide body hint.
enum PaneTabDropKind
{
  PANE_TAB_DROP_NONE,
  PANE_TAB_DROP_INSERT,
  PANE_TAB_DROP_EDGE
};

struct PaneTabDrop
{
  PaneTabDropKind kind = PANE_TAB_DROP_NONE;
  int pane = -1;
  int slot = 0;
  int edge = 0;
  bool on_strip = false;
};

// One tab on the workspace strip (row 0). The strip is the only consumer now
// that panes have no strip of their own, so these fields answer what the strip
// draws and what the mouse hit-tests against.
struct FileTabSegment
{
  int buffer_id = -1;
  int tab_index = -1;
  int x = 0;
  int label_x = 0;
  int close_x = 0;
  int end_x = 0;
  // The × only earns a cell while it lands on the label's padding: a tab
  // squeezed until its name fills the box draws nothing there and refuses the
  // close click. The painter and the hit-test read the same flag.
  bool hidden_close = false;
  std::string label;
  // Per-language file glyph painted ahead of the label in its brand color
  // (shared with the status line / explorer); empty for directories and
  // unnamed buffers, -1 color = use the tab foreground.
  std::string icon;
  int icon_fg = -1;
  bool active = false;
  bool modified = false;
  bool preview = false;
  // Pinned tabs lead the strip (the pin glyph sits before the close control);
  // `visible_elsewhere` marks a buffer another pane is showing, and `hovered`
  // is the band under the pointer (`tabline_hover_index`).
  bool pinned = false;
  bool visible_elsewhere = false;
  bool hovered = false;
  // The letter this tab answers to in jump-to-buffer mode, 0 when the mode is
  // off (features/tab_order.h assigns it and keeps it stable).
  char jump_letter = 0;
  // Where the state glyphs are drawn; -1 when the tab is too narrow for them.
  int marker_x = -1;
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
