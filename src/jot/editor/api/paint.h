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
  void render_tabs();
  void render_panes();
  void render_pane_resize_guides();
  void render_easter_egg();
  void render_pane(const SplitPane &pane, int pane_index);
  FileTabLayout build_file_tab_layout(const SplitPane &pane, int draw_w);
  int find_local_tab_index(const SplitPane &pane, int buffer_id) const;
  void clamp_tab_scroll(SplitPane &pane);
  void reveal_local_tab(SplitPane &pane, int target_index, int draw_w);
  bool scroll_local_tabs(SplitPane &pane, int delta);
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
  void render_problems_view(int x, int w);
  // Width the panel's view tabs occupy, so the renderer and the click
  // hit-test walk the same two label offsets.
  int bottom_panel_view_tabs_width() const;
  static const char *bottom_panel_view_label(int view);
  // The panel's rows: the view tabs, a second strip for the shell's own tabs
  // (terminal view only), then content. Shared here so the renderer, the mouse
  // hit-tests and the terminal's selection math cannot drift apart.
  int bottom_panel_view_tab_y() const;
  int bottom_panel_terminal_tab_y() const;
  // One shell tab's label (leading pad, shell glyph, name, trailing pad), built
  // here so the strip's renderer and every hit-test measure the same string. A
  // long custom name is elided to a share of the strip so it cannot push the
  // tabs after it (and the "+") off the row.
  std::string integrated_terminal_tab_label(int index) const;
  int bottom_panel_content_y() const;
  int bottom_panel_content_h() const;
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
  int effective_right_panel_width() const;
  void render_menu_bar();
  void render_menu_dropdown();
  void render_status_line();
  void render_command_palette();
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
  void render_popup();
  void render_home_menu();
  // Cell-based settings menu (:settings / Ctrl+, in GUI mode): a quick-
  // pick style panel listing every config key with its value. Bools toggle
  // on Enter; ints/strings edit inline. Lua-registered config keys appear
  // automatically (the menu enumerates config.keys()).
  void render_settings_menu();
  void place_settings_cursor();
  void toggle_settings_menu();
  void close_settings_menu();
  void rebuild_settings_entries();
  bool handle_settings_input(int ch);
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
  std::vector<int> gui_pane_top_lines_;
  std::vector<int> gui_pane_scroll_xs_;
