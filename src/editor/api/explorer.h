// ---------------------------------------------------------------------------
// Explorer and sidebar
// ---------------------------------------------------------------------------
//
// The file tree and its watcher, the sidebar render caches, the sidebar
// geometry and its resize drag.
//
// A fragment of the Editor class body, included by src/editor/editor.h. It is
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
// The same span for the full-height side columns: they start on the row the
// workspace strip leaves them (side_column_top) instead of below it, so the
// explorer and the dock own their top row and the strip only spans the pane
// area between them.
ContentColumn side_column() const;
// Rows the sidebar's list can show: the explorer section's content rows,
// less the pinned root row.
int sidebar_list_rows() const;
// The left column's stacked sections (explorer, outline, timeline): the one
// geometry model the painter, the hit-test and the cursor walk all read.
jot_sidebar::Layout sidebar_sections_layout() const;
// The section a sidebar row belongs to and the data-row index it carries:
// -1 is the section's header, -2 the footer or the empty space under a
// section.
struct SidebarHit
{
  int section = -1;
  int index = -2;
  bool header = false;
};
SidebarHit sidebar_hit_test(int y) const;
// The data-row index a hit names: the hit-test maps a screen row to its place
// in the section's window, so the section's scroll offset has to be added back
// (and the explorer's window skips its pinned root row).
int sidebar_data_index(const SidebarHit &hit) const;
// Where the sidebar's content starts: past the left border and the activity
// rail when one is shown. The renderer and the row hit-tests (the outline's
// chevron) read the same column.
int sidebar_content_x() const;
// Pointer motion in the sidebar: moves the hover band to the row under the
// pointer, or drops it when the pointer is anywhere else.
void sidebar_update_hover(int x, int y);
// The per-section cursors and row counts, shared by the keyboard walk, the
// page/home/end keys and the render's clamps.
int sidebar_section_row_count(jot_sidebar::Section section) const;
int sidebar_section_cursor(jot_sidebar::Section section) const;
int sidebar_section_view_rows(jot_sidebar::Section section) const;
void sidebar_set_cursor(jot_sidebar::Section section, int index);
// Keyboard cursor: moves inside the current section and across the section
// boundaries; an empty section is skipped over.
void sidebar_move_cursor(int delta);
void sidebar_activate_cursor();
void sidebar_toggle_section(int section);
// Jumps the buffer to the symbol the sidebar's outline row shows, and opens
// the commit's file diff from a timeline row.
void sidebar_jump_outline(int index);
void sidebar_open_timeline_commit(int index);
// Reloads the file timeline (async, cached per file). The sidebar render
// calls it so the section always describes the buffer on screen.
void refresh_timeline(bool force = false);
std::string timeline_current_path() const;
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
// Vertical resize of the stacked sections. A press on a boundary header
// (OUTLINE's or TIMELINE's -- the rows between two sections) arms the gesture
// with sidebar_section_resize_press instead of toggling on the spot: the
// first row of movement starts the drag, a release without movement is still
// the toggle click. The heights land in sidebar_outline_height /
// sidebar_timeline_height (0 = the automatic share).
void sidebar_section_resize_press(int y);
bool begin_sidebar_section_resize_drag(int y);
bool update_sidebar_section_resize_drag(int y);
void end_sidebar_section_resize_drag();
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
