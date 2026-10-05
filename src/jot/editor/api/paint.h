// ---------------------------------------------------------------------------
// Painting the frame
// ---------------------------------------------------------------------------
//
// The render entry points and the per-pane scratch the GUI caret placement
// reads back from a frame.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  void render();
  // Whether the frame just painted has to be painted again on the next tick.
  // UI's retained baseline records cells as painted when they are queued, so a
  // frame the terminal could not take completely (see UI::render) left them
  // half-drawn and the cell diff would skip them from now on: keep the redraw
  // request alive instead of going idle on a screen known to be wrong.
  bool frame_needs_repaint() const
  {
    return ui && ui->full_repaint_pending();
  }
  // The workspace tab strip (one row of the pane area): the order
  // reconciliation, the layout the painter and the hit-tests share, and the
  // operations on it (src/render/tabs.cpp).
  void sync_tab_order();
  FileTabLayout build_tabline_layout();
  // The columns the strip spans on its row: between the explorer and the right
  // dock when they are up (full width when neither is; a zoomed terminal covers
  // them), so the side columns keep their own top row.
  void tabline_span(int &x, int &w) const;
  bool tabline_contains_x(int x) const;
  void render_tabline();
  bool handle_tabline_mouse(int x,
                            int y,
                            bool is_click,
                            bool is_click_release,
                            bool is_middle_click,
                            bool is_motion);
  bool tabline_activate(int position);
  bool tabline_scroll(int delta);
  // The strip's keys, shared by both input modes: Alt+1..9 and Alt+0 pick a
  // position, Alt+, / Alt+. step through the order, Alt+R arms jump-to-buffer
  // mode (a letter then picks the tab it is painted on). Returns false when the
  // key is not the strip's, so the caller keeps handling it.
  bool handle_tabline_key(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  // Steps to the previous (-1) or next (+1) position in the strip order, wrapping
  // at the ends (the strip is a ring, like barbar's :BufferNext).
  bool cycle_tabline_tab(int delta);
  void reveal_tabline_position(int position);
  // Closes every tab but `keep_buffer`, leaving that one focused.
  void close_other_tabs(int keep_buffer);

  // The breadcrumb winbar: the row a code pane spends above its text, and the
  // drop-down a crumb opens (src/render/winbar.cpp, features/winbar.h). A crumb
  // is a click away from its siblings -- the file's folder, the folder's
  // children, the symbols sharing a scope -- and a folder row opens its own
  // listing *beside* the level that offered it, a cascade that keeps every
  // level it stepped through on screen (dropbar's model).
  const std::vector<SymbolMatch> &winbar_symbols(int buffer_id);
  Winbar::WinbarLayout build_winbar_layout(const SplitPane &pane, int pane_index);
  // Emits every pane's breadcrumb row as one surface, before the panes paint.
  bool emit_winbar_rows();
  void render_winbar(const SplitPane &pane, int pane_index);
  void render_winbar_menu();
  bool handle_winbar_mouse(int x, int y, bool is_click, bool is_click_release, bool is_motion);
  // The cascade's own input, taken before any editing key: Esc puts the whole
  // cascade away, h / Left steps back one level, j/k (Down/Up) move the deepest
  // level's selection, Enter / Right / space opens the selected folder one level
  // deeper (or the selected file), and every other key is swallowed so it
  // cannot edit the buffer behind the menus.
  bool handle_winbar_menu_input(int ch);
  void open_winbar_menu(int crumb_index, int pane_index);
  void close_winbar_menu();
  void winbar_menu_move(int delta);
  void winbar_menu_scroll(int level, int delta);
  void winbar_menu_back();
  void winbar_menu_activate(int level, int index);
  void winbar_menu_hover(int level, int index);
  void winbar_menu_clamp(int level);
  // The cascade's own geometry: which pane's row a point is on (and which crumb
  // of it), which level of the cascade a point lands in (-1 for none), and the
  // level's order in the cascade.
  bool winbar_row_crumb_at(int x, int y, int &pane_index, int &crumb_index);
  int winbar_menu_level_at(int x, int y) const;
  void winbar_menu_follow(int level);
  void winbar_menu_open_submenu(int level, int entry_index);
  bool buffer_visible_in_other_pane(int buffer_id) const;
  int tabline_position_at(const FileTabLayout &layout, int x) const;
  int tabline_scroll_chip_at(const FileTabLayout &layout, int x) const;
  int tabline_drop_position(const FileTabLayout &layout) const;
  int tabline_drop_position_x(const FileTabLayout &layout) const;
  // The pane's own tab strip (a VSCode editor group, src/render/pane_tabs.cpp).
  // build_pane_tab_layout measures which tabs fit and where; the painter, the
  // mouse hit-tests and the drop markers all read that one layout. The strip
  // only exists while pane_shows_tab_strip is true (pane_tabs=auto by default:
  // a lone pane leaves the row to the workspace strip).
  PaneTabLayout build_pane_tab_layout(int pane_index) const;
  void render_pane_tab_strip(int pane_index);
  // The drop target the live drag would land in, painted after every pane so it
  // reads over the text: a slot marker rides in the target's strip (drawn by
  // render_pane_tab_strip), a body insert outlines the pane, and an edge target
  // inks the edge the split would take.
  void render_pane_tab_drag_hint();
  int pane_tab_position_at(const PaneTabLayout &layout, int x) const;
  int pane_tab_scroll_chip_at(const PaneTabLayout &layout, int x) const;
  int pane_tab_drop_x(const PaneTabLayout &layout) const;
  // The strip's mouse input (src/input/mouse/pane_tabs.cpp), the tabline's
  // shape per pane: which pane's strip row a point is on, the press that both
  // switches a tab and arms a drag, the motions that recompute the drop target,
  // and the release that performs it. A live drag owns every event until it
  // ends, because the drop can land in another pane entirely.
  bool pane_tab_strip_row_at(int x, int y, int &pane_index) const;
  bool handle_pane_tab_mouse(
      int x, int y, bool is_click, bool is_click_release, bool is_middle_click, bool is_motion);
  // The wheel over a strip: scrolls its tab window and claims the notch either
  // way (a strip with nothing hidden still eats it rather than scrolling the
  // buffer behind the row).
  bool handle_pane_tab_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
  PaneTabDrop pane_tab_drop_target_at(int x, int y) const;
  void update_pane_tab_drag(int x, int y);
  void finish_pane_tab_drag();
  void cancel_pane_tab_drag();
  void render_panes();
  void render_pane_resize_guides();
  void render_easter_egg();
  void render_pane(const SplitPane &pane, int pane_index);
  int find_local_tab_index(const SplitPane &pane, int buffer_id) const;
  // Shows a buffer in the focused pane, adding it to that pane's history.
  bool show_buffer_in_current_pane(int buffer_id);
  // Pane-local tab history: Ctrl+Tab / Ctrl+Shift+Tab cycle it, Alt+W closes.
  bool switch_to_local_tab(int target_index);
  bool cycle_local_tab(int delta);
  void render_telescope();
  void render_minimap(int x, int y, int w, int h, int buffer_id);
  // The picture lives in the pane that holds the image tab, the way any other
  // file's contents do -- a window-wide panel read as an overlay.
  void render_image_viewer(const SplitPane &pane);
  // The bottom panel: one dock, two views (the shell and the diagnostics
  // list). The tab strip and the frame are shared; the body switches.
  void render_integrated_terminal();
  // The floating terminal's box: centered over the active pane's text rows,
  // sized by the terminal_float_* settings (a percentage of that area) and
  // clamped to it, so it never shares a row with the pane chrome or covers the
  // docks and the status line.
  TerminalBox floating_terminal_rect() const;
  void render_floating_terminal();
  void render_problems_view(int x, int w);
  // Width the panel's view tabs occupy, so the renderer and the click
  // hit-test walk the same two label offsets.
  int bottom_panel_view_tabs_width() const;
  static const char *bottom_panel_view_label(int view);
  // A view label's first cell: the labels run down the left end of the tab bar
  // with a fixed gap between them, measured by both the renderer and the click
  // hit-test.
  int bottom_panel_view_tab_x(int view) const;
  // The panel's rows: the tab bar (view labels on the left, the action cluster
  // on the right), then content. Both views share the same two rows now: the
  // shell's sessions moved off the panel into the vertical list down its right
  // side (see bottom_panel_shell_list_w), so neither view spends a row on tabs.
  // Shared here so the renderer, the mouse hit-tests and the terminal's
  // selection math cannot drift apart.
  int bottom_panel_view_tab_y() const;
  int bottom_panel_content_y() const;
  int bottom_panel_content_h() const;
  // The session list's column: one row per shell session down the right side of
  // the terminal view, the way the editor's docks carry their own tab strips.
  // The width follows the names (within bounds) and the shell keeps a minimum
  // span beside it; 0 means the panel is too narrow and the shell takes the row
  // instead. bottom_panel_shell_list_at maps a cell to a terminal index, or -1
  // when the cell is outside the list.
  int bottom_panel_shell_list_w() const;
  int bottom_panel_shell_list_x() const;
  int bottom_panel_shell_list_at(int x, int y) const;
  void render_bottom_panel_session_list();
  // The name a session list row draws (the generated "term N" or the custom
  // label), as opposed to get_label(), which is what the task runner and the
  // Lua API match on.
  std::string integrated_terminal_display_name(int index) const;
  // One session's list label (the shell glyph, then the name), built here so
  // the list's renderer and every hit-test measure the same string. A long
  // custom name is elided to the column's share with an ellipsis.
  std::string integrated_terminal_session_label(int index) const;
  void render_debugger_panel();
  void render_git_diff_panel();
  void render_git_panel();
  void render_outline_panel();
  bool outline_active() const
  {
    return show_right_panel && active_right_panel_tab == RIGHT_PANEL_SYMBOLS;
  }
  void toggle_outline_panel();
  void close_outline_panel();
  void note_outline_edit();
  void ensure_outline_fresh(bool force = false);
  void outline_move_selection(int delta);
  void outline_jump_selected();
  void render_plugin_panel();
  // The plugin panel's row area: the dock minus its border, tab strip and
  // title row. Shared by the renderer and the mouse hit-test so a row's pixels
  // and its clickable extent cannot drift apart.
  TerminalBox plugin_panel_body() const;
  int effective_right_panel_width() const;
  void render_menu_bar();
  void render_menu_dropdown();
  void render_status_line();
  // Whether the statusline's time labels (features/status_clock.h) have moved
  // on since the last frame asked: the clock's minute, or the session
  // duration's next unit. The frame loop asks before the paint, so a label that
  // did move goes out on the frame that noticed it -- which is what keeps the
  // bar honest without a repaint timer or a repaint while the text is unchanged.
  bool status_time_due_soon();
  void render_command_palette();
  // The palette's painted geometry: the explanation box, the results grid and
  // the prompt row. One builder for the renderer, the mouse hit-test and the
  // Lua view, so a click lands on the cell that was painted.
  CommandPaletteLayout build_command_palette_layout();
  void render_quick_pick();
  // Multi-chord plugin keymaps ("Ctrl+T N"): a prefix chord starts a pending
  // sequence and handle_which_key_input advances it, runs the final chord, or
  // closes it. Nothing is drawn for it -- the options used to be listed in a
  // panel above the status line, which was more distraction than help.
  void open_which_key(const std::string &chord);
  void close_which_key();
  bool handle_which_key_input(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch);
  void sync_lua_ui_surfaces();
  void place_command_palette_cursor();
  void render_context_menu();
  void render_tree_sitter_status_modal();
  void render_save_prompt();
  // Interactive LSP rename: opened by :lsprename with no argument or Ctrl+Shift+R.
  void open_rename_prompt();
  void handle_rename_prompt(int ch);
  // The identifier under the cursor, used to seed the prompt.
  std::string identifier_under_cursor();
  void render_rename_prompt();
  void place_rename_prompt_cursor();
  void place_save_prompt_cursor();
  void render_quit_prompt();
  // The save / rename / quit prompt as a modal: the screen is dimmed and the
  // panel paints over it. One entry point so the frame's shared tail and its
  // home-screen early return (which raises the same panel from the home menu)
  // treat the three prompts identically.
  void render_prompt_modal();
  void render_popup();
  // The home screen's rows for the current query, plus the rects the mouse
  // hit-tests. render_home_menu paints what this returns; the input handler
  // rebuilds it when a key arrives before the frame that would have.
  HomeView build_home_menu_model();
  void render_home_menu();
  // Cell-based settings menu (:settings / Ctrl+, in GUI mode): a quick-
  // pick style panel listing every config key with its value, narrowed by
  // the search bar above the list. Bools toggle on Enter, ints step and
  // edit, enums cycle and open their choices, strings edit inline.
  // Lua-registered config keys appear automatically (the menu enumerates
  // config.keys()).
  void render_settings_menu();
  void place_settings_cursor();
  void toggle_settings_menu();
  void close_settings_menu();
  void rebuild_settings_entries();
  // Re-derives the visible entries from the search bar's query and clamps the
  // selection to the new list. Also refreshes each entry's filtered position.
  void refresh_settings_filter();
  // The entry the selection is on, or null when the query matches nothing.
  SettingsEntry *settings_selected_entry();
  // Moves the selected row's value one notch: a bool toggles, an int steps by
  // its own amount (clamped to its range), an enum walks its choices.
  bool step_settings_value(bool increase);
  // The choices drop-down: opened by Enter on an Enum row, it owns the
  // keyboard until a choice is taken or it is dismissed.
  void open_settings_dropdown();
  bool handle_settings_dropdown_input(int ch);
  bool handle_settings_input(int ch);
  // A terminal paste, for the two fields above: the row being edited keeps it,
  // the search bar takes it when no row is open, and the buffer behind the
  // panel never sees it. False when the panel is not up, so the paste falls
  // through to the editor's own paste_text.
  bool handle_settings_paste(const std::string &text);
  bool handle_settings_mouse(int x, int y, bool is_click);
  void render_buffer_content(const SplitPane &pane, int pane_index, int buffer_id);
  // The regions that share a separator with this pane: the other visible panes
  // (skipping the ones zoom hides), the sidebar when it is up, the right dock,
  // and whatever occupies the rows below the pane area. Used to decide which
  // sides of the pane's box get inked (see render/pane_edges.h).
  std::vector<UIRect> pane_neighbours(const SplitPane &pane, int draw_w) const;
  // The right dock's box sides. Nothing lies to its right and the panes own the
  // separator on its left, so only the status-line edge below it gets ink.
  // GUI smooth-scroll tracking: last reported first-visible line per pane,
  // so the fold-aware delta for the scroll animation is computed once per
  // pane per frame (editor side, where the fold ranges live). gui_pane_scroll_xs_
  // is the same per pane for the horizontal window: it has no slide animation,
  // so the GUI uses the change to place the caret instead of easing it.
  // gui_pane_buffer_ids_ remembers which buffer each pane last reported for:
  // a pane whose buffer changed must report no delta (the retained viewports
  // belong to the previous file, so the new one snaps into place).
  std::vector<int> gui_pane_top_lines_;
  std::vector<int> gui_pane_scroll_xs_;
  std::vector<int> gui_pane_buffer_ids_;
