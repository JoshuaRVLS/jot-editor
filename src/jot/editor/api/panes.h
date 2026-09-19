// ---------------------------------------------------------------------------
// Folds and panes
// ---------------------------------------------------------------------------
//
// The fold commands, the pane commands (split, focus, move, resize, zoom) and
// the pane-area geometry helpers.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  std::string telescope_launch_root() const;
  void refresh_folds(FileBuffer &buf);
  bool toggle_fold_at_line(FileBuffer &buf, int line);
  bool fold_at_cursor();
  bool unfold_at_cursor();
  bool toggle_fold_at_cursor();
  void fold_all();
  void unfold_all();
  bool is_line_hidden_by_fold(const FileBuffer &buf, int line) const;
  int buffer_line_for_visible_row(const FileBuffer &buf, int first_line, int row) const;

  void split_pane_horizontal();
  void split_pane_vertical();
  void split_pane_left();
  void split_pane_right();
  void split_pane_up();
  void split_pane_down();
  void close_pane();
  void next_pane();
  void prev_pane();
  bool focus_pane_direction(char dir);
  void equalize_panes();
  void toggle_pane_zoom();
  void swap_panes();
  bool pane_zoomed() const
  {
    return pane_zoom_active;
  }
  // Per-pane views: each pane keeps its own cursor/scroll/selection for the
  // buffer it shows, so two panes can display one buffer independently.
  void capture_pane_view(int pane_index);
  void restore_pane_view(int pane_index);
  void activate_pane(int pane_index);
  void pane_show_buffer(int buffer_index);
  bool resize_current_pane(int delta);
  bool resize_current_pane_direction(char dir, int delta);
  // The region the pane tree fills, after the chrome (top bar, status line,
  // bottom panel), the docks (sidebar, right dock) and zen centering have taken
  // their share. Derived in exactly one place so the renderer, the splitter
  // hit-tests and the resize guides cannot disagree about where a pane is.
  struct PaneArea
  {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
  };
  PaneArea compute_pane_area() const;
  int pane_split_at_position(int x, int y) const;
  bool begin_pane_resize_drag(int x, int y);
  bool update_pane_resize_drag(int x, int y);
  void end_pane_resize_drag();
  bool is_pane_resize_dragging() const
  {
    return pane_resize_dragging;
  }
  bool pane_split_is_resizing(int node_index) const
  {
    return pane_resize_dragging && pane_resize_node == node_index;
  }
  bool adjust_pane_split_ratio(int node_index, int delta, bool clamp_only = false);

