#ifndef JOT_STATE_PANEL_STATE_H
#define JOT_STATE_PANEL_STATE_H

#include "editor/model/panels.h" // BottomPanelView, RightPanelTab, GitDiffPanel, OutlinePanelState
#include "lsp/client.h"          // LSPCodeAction
#include <string>
#include <vector>

// The docks around the pane area: the bottom panel (integrated terminal and
// Problems in one tabbed dock), the right dock (debugger, git panel, outline,
// plugin panels), the minimap, and the live drags that resize them.
//
// Split out of editor_state.h (which is now the umbrella over src/editor/state/);
// the members and their comments moved verbatim.
struct PanelState
{
  bool show_minimap = false;
  int minimap_width = 0;

  bool show_integrated_terminal = false;
  int integrated_terminal_height = 0;
  // Which view the bottom panel shows, and the Problems view's list cursor.
  // The panel is one dock: both views share its height and its tab strip.
  BottomPanelView bottom_panel_view = BOTTOM_PANEL_TERMINAL;
  int problems_selected = 0;
  int problems_scroll = 0;
  // True while the terminal fills the whole pane area (like pane zoom);
  // panes stay laid out underneath but are covered and their clicks route
  // to the terminal until the zoom is toggled off.
  bool terminal_zoom_active = false;
  // Mouse selection in the integrated terminal: anchors live in full-space
  // row/col coordinates (see IntegratedTerminal::get_total_rows) so they
  // survive scrolls and redraws. active = a selection exists (rendered),
  // dragging = the mouse button is held and motions extend it.
  bool terminal_sel_active = false;
  bool terminal_sel_dragging = false;
  int terminal_sel_anchor_row = 0;
  int terminal_sel_anchor_col = 0;
  int terminal_sel_cur_row = 0;
  int terminal_sel_cur_col = 0;
  // Ctrl+hover over a link in a terminal: the underline that advertises what
  // the click would open, as a full-space row and half-open cell columns (the
  // same coordinates the selection anchors use). in_float says which view owns
  // it; only that view paints it.
  bool terminal_link_hover_active = false;
  bool terminal_link_hover_in_float = false;
  int terminal_link_hover_row = -1;
  int terminal_link_hover_start = 0;
  int terminal_link_hover_end = 0;
  // Dragging the terminal panel's top border resizes its height live.
  bool terminal_resize_dragging = false;
  int terminal_resize_start_y = 0;
  int terminal_resize_start_height = 0;

  // The floating terminal: a box centered over the active pane's text rows
  // hosting its own shell (integrations/terminal_float.cpp). Visible means it
  // owns the keys; hiding it leaves the process and its scrollback running.
  // The two sizes are percentages of that area, 100 being all of it.
  bool show_floating_terminal = false;
  int floating_terminal_width = 85;
  int floating_terminal_height = 75;
  // Which view began the live selection: the anchors below are shared, and only
  // the view that started the drag may paint the band.
  bool terminal_sel_in_float = false;

  bool show_debugger_panel = false;
  int debugger_panel_height = 0;

  bool show_right_panel = false;
  int right_panel_width = 0;
  RightPanelTab active_right_panel_tab = RIGHT_PANEL_DEBUG;
  // Ordered list of panels opened in the right dock (VSCode-style tabs).
  // Commands like :gitpanel add their tab here; the tab strip at the top of
  // the panel switches between them and each tab can be closed individually.
  std::vector<RightPanelTab> right_panel_tabs;
  GitDiffPanel git_diff_panel;
  OutlinePanelState outline_panel;
  std::string active_plugin_panel;
  // Row under the pointer in the plugin panel (-1 = none): the hover
  // highlight, and the row a click activates.
  int plugin_panel_hover_row = -1;
  // Scroll of the plugin panel's row list, kept here so a panel longer than
  // the dock (the LeetCode console) can be wheeled through.
  int plugin_panel_scroll = 0;
  std::string plugin_quick_pick_select_callback;
  // Actions offered by the last code-action response, indexed by the quick
  // pick selection; cleared once the selection is applied.
  std::vector<LSPCodeAction> lsp_code_actions_pending;
  bool right_panel_resize_dragging = false;
  int right_panel_resize_start_x = 0;
  int right_panel_resize_start_width = 0;

  // Dragging the sidebar's right border (the dock is part of the workspace,
  // the drag belongs to the panel machinery that owns all border drags).
  bool sidebar_resize_dragging = false;
  bool sidebar_resize_opening = false;
  int sidebar_resize_start_x = 0;
  int sidebar_resize_start_width = 0;

};

#endif
