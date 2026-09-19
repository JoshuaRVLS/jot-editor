// ---------------------------------------------------------------------------
// The public command surface
// ---------------------------------------------------------------------------
//
// Members other layers may call directly: inlay-hint geometry queries used by
// the renderer and mouse mapping, the default Tab actions, sidebar / right
// panel / zen / workspace-session commands, and the layout metrics Lua reads.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
public:
public:
  // Hint cells inserted before `byte_col` on `line` (text shift amount), for
  // caret placement, popup anchoring, and mouse mapping. Pure query helpers
  // shared with the renderer and input layers (and the test suite).
  int lsp_inlay_hint_cells_before(const std::string &filepath,
                                  int line,
                                  int byte_col,
                                  const std::string &line_text);
  // (shifted screen column, cell width) pairs for every hint on `line`,
  // mirroring the renderer's layout (used to un-shift mouse clicks).
  std::vector<std::pair<int, int>> lsp_inlay_hints_visual(const std::string &filepath,
                                                          int line,
                                                          const std::string &line_text,
                                                          int tab_size);
  // The modeless Tab / Shift+Tab actions. Exposed so keymaps that shadow
  // those chords (the bundled snippet engine) can fall back to exactly the
  // editor's own behavior instead of re-deriving it in Lua.
  void apply_default_tab();
  void apply_default_shift_tab();
  void toggle_sidebar();
  // Drops the integrated-terminal mouse selection (panel close, terminal
  // close, and the test suite).
  void clear_terminal_selection();
  bool zen_active() const
  {
    return zen_mode;
  }
  bool right_panel_visible() const
  {
    return show_right_panel;
  }
  // Right dock (VSCode-style tabbed sidebar): panels open as tabs
  // (git, git diff, symbols, debug, plugin). Ctrl+Shift+B toggles the dock;
  // the tab strip at the top switches panels and closes individual tabs.
  void toggle_right_panel();
  void open_right_panel_tab(RightPanelTab tab);
  void close_right_panel_tab(RightPanelTab tab);
  bool right_panel_tab_open(RightPanelTab tab) const;
  // Right-dock state for headless tests (private EditorState; tests read
  // the tab list and active tab through these).
  const std::vector<RightPanelTab> &right_panel_tabs_for_test() const
  {
    return right_panel_tabs;
  }
  RightPanelTab active_right_panel_tab_for_test() const
  {
    return active_right_panel_tab;
  }
  // Headless tests: enables the workspace session subsystem for a fake root
  // so save_workspace_session / restore_workspace_session can round-trip
  // through a JOT_CONFIG_HOME-backed session file.
  void enable_workspace_session_for_test(const std::string &root)
  {
    workspace_session_enabled = true;
    workspace_session_root = root;
  }
  void save_workspace_session_for_test()
  {
    save_workspace_session();
  }
  bool restore_workspace_session_for_test()
  {
    return restore_workspace_session();
  }
  int status_line_height() const
  {
    return status_height;
  }
  // Zen focus mode: hides the sidebar and right panel, suppresses the status
  // line and centers the pane area at zen_content_width. Returns the new
  // state (true = zen on). Layout-affecting; safe with no panes / no ui.
  bool toggle_zen_mode();
  // Left/right margin that centers the pane area at zen_content_width while
  // zen mode is active (0 otherwise or when the area is narrower).
  int zen_content_margin(int available_w) const;
  void load_file_tree(const std::string &path);
  void open_workspace(const std::string &path, bool restore_session = true);
  // Test seam: the computed explorer/git sidebar rows (rebuilding the cache
  // on demand so callers never see a stale tree).
  const SidebarRenderCache &sidebar_render_cache()
  {
    ensure_sidebar_render_cache();
    return sidebar_render_cache_;
  }
  bool resume_last_workspace_session();
  void set_home_menu_visible(bool visible);
