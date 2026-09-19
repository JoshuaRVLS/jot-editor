// ---------------------------------------------------------------------------
// Explorer and sidebar
// ---------------------------------------------------------------------------
//
// The file tree and its watcher, the sidebar render caches, the sidebar
// geometry and its resize drag.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
private:
  void handle_sidebar_input(int ch);
  void handle_sidebar_mouse(int x, int y, bool is_click, bool is_double_click = false);
  void render_sidebar();
  void render_collapsed_sidebar_handle();
  // The cell grid the editor is drawing into. Always use these for layout and
  // hit-testing, never the Terminal's size: the terminal object only exists in
  // terminal mode and keeps its 80x24 constructor default under --gui, so
  // reading it there silently clamps geometry (and clicks) to 24 rows.
  int grid_height() const
  {
    return ui ? ui->get_height() : 0;
  }
  int grid_width() const
  {
    return ui ? ui->get_render_width() : 0;
  }
  // The vertical span the chrome columns (sidebar, right dock) occupy: the rows
  // between the top bar and the status line, less whatever the bottom panel
  // reserves. Derived once so the dock edges, the renderers and the mouse
  // hit-tests cannot disagree about where a column starts and ends.
  struct ContentColumn
  {
    int top = 0;
    int bottom = 0;
    int h = 0;
  };
  ContentColumn content_column() const;
  // Rows the sidebar's list can show: the content column minus its own header,
  // footer and bottom-border rows.
  int sidebar_list_rows() const;
  int sidebar_activity_rail_width() const
  {
    return 5;
  }
  int min_sidebar_width() const
  {
    return sidebar_activity_rail_width() + 18;
  }
  int sidebar_close_threshold() const
  {
    return 12;
  }
  int max_sidebar_width() const;
  int effective_sidebar_width() const;
  bool collapsed_sidebar_handle_hit_test(int x, int y) const;
  bool sidebar_resize_hit_test(int x, int y) const;
  bool begin_sidebar_resize_drag(int x, int y);
  bool update_sidebar_resize_drag(int x);
  void end_sidebar_resize_drag();
  int min_right_panel_width() const
  {
    return 28;
  }
  int max_right_panel_width() const;
  bool right_panel_resize_hit_test(int x, int y) const;
  bool begin_right_panel_resize_drag(int x, int y);
  bool update_right_panel_resize_drag(int x);
  void end_right_panel_resize_drag();
  void build_tree(const std::string &path, std::vector<FileNode> &nodes, int depth);
  void refresh_tree_children(FileNode &node);
  std::string build_file_tree_signature() const;
  void refresh_file_tree_watch_baseline();
  void poll_file_tree_changes();
  void invalidate_sidebar_tree_cache();
  void invalidate_sidebar_diagnostics_cache();
  void invalidate_sidebar_git_cache();
  void ensure_sidebar_render_cache();
  void rebuild_sidebar_tree_cache();
  void rebuild_sidebar_diagnostics_cache();
  void rebuild_sidebar_git_cache();
  std::vector<GitSidebarRow> build_git_sidebar_rows() const;

