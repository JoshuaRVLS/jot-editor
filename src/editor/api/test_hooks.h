// ---------------------------------------------------------------------------
// Test surface: core
// ---------------------------------------------------------------------------
//
// The headless hooks the suite drives the engine with: multicursor helpers,
// smooth scroll, settings, panels and terminal geometry, themes, selection and
// textobjects, the rename prompt and the jumplist.
//
// A fragment of the Editor class body, included by src/editor/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
public:
// gui_mode selects the SDL3/OpenGL frontend (jot --gui) over the terminal
// backend; the editor logic is identical either way.
Editor(bool gui_mode = false);
~Editor();
bool multicursor_active();
void clear_extra_carets();
bool add_caret_at(int line_y, int x);
bool select_next_occurrence();
// Selection manipulation, helix's selection-first family: drop the extra
// carets, rotate which one is primary, copy the primary onto the neighbouring
// line, split a multi-line selection into one cursor per line, and make every
// occurrence of the selection a cursor.
void keep_primary_selection();
bool rotate_primary_selection(int direction);
bool add_caret_on_adjacent_line(int direction);
bool split_selection_on_newlines();
bool select_all_occurrences();
// Tree-sitter textobjects: expand/shrink the selection to a syntax node (helix
// Alt+o / Alt+i), select the inside/around of a function, class or argument,
// and step between functions. All act on the primary selection.
bool expand_selection_to_node();
bool shrink_selection_to_node();
bool select_textobject(const std::string &kind, bool inner);
bool goto_relative_function(int direction);
// The same walk for any textobject kind ("function", "class"), which is what
// the ]<kind> / [<kind> family uses.
bool goto_relative_object(const std::string &kind, int direction);
// Selection helpers the operator menus act on.
bool select_word_at_cursor();
// The in-place edits: the word span the caret is on, and the inside of the
// literal or the bracket pair it is in. Driven here so the span and the
// caret that follows the erase are pinned without a real terminal.
bool delete_word_at_cursor_for_test()
{
  return delete_word_at_cursor();
}
bool change_inside_any_quote_for_test()
{
  return change_inside_any_quote();
}
bool change_inside_bracket_for_test()
{
  return change_inside_bracket();
}
void delete_selection_for_test();
void delete_char_for_test(bool forward);
void insert_string_for_test(const std::string &str);
// The two key paths a typed character and a bracketed paste take, so a case
// can pin where each leaves the viewport without driving a terminal.
bool insert_char_for_test(char c)
{
  return insert_char(c);
}
void paste_text_for_test(const std::string &text)
{
  paste_text(text);
}
// Enter, as the keymaps spell it (see Editor::new_line), so a case can pin
// the line it creates and where the caret lands.
void new_line_for_test()
{
  new_line();
}
// The palette's "Trim Trailing Whitespace" command (see trim_caret_probe.py
// for the key path), so a case can pin the caret and the undo step it leaves
// behind without a terminal.
void trim_trailing_whitespace_for_test()
{
  trim_trailing_whitespace();
}
// "Format Document" from the palette. On a document with no tabs it expands
// nothing, which is the state a case needs to see undo skip (see
// format_undo_probe.py for the key path).
void format_document_for_test()
{
  format_document();
}
// Seeds the per-file inlay-hint cache directly (sorted on ingest like a
// real server answer), so coordinate helpers can be unit-tested headless.
void set_inlay_hints_for_test(const std::string &filepath, std::vector<LSPInlayHint> hints);
// LSP replies arrive through the client's poll loop; these deliver one the
// way that loop does, so the landing policy (which file, which tab, what the
// status says) can be asserted without a running server.
void deliver_lsp_definition_for_test(const LSPDefinitionResult &result)
{
  handle_lsp_definition_result(result);
}
void deliver_lsp_switch_source_header_for_test(const std::string &paired)
{
  handle_lsp_switch_source_header_result(paired);
}
// The dwell-hover arm (see request_lsp_hover_at): whether a rest armed a
// request, and the position and token it recorded. Only a pending arm fires
// in maybe_fire_lsp_mouse_hover, so a case can pin that blank space arms
// nothing while a real token arms the position the request will ask about.
bool lsp_mouse_hover_pending_for_test() const
{
  return lsp_mouse_hover_pending;
}
int lsp_mouse_hover_line_for_test() const
{
  return lsp_mouse_hover_line;
}
int lsp_mouse_hover_col_for_test() const
{
  return lsp_mouse_hover_col;
}
int lsp_mouse_hover_token_start_for_test() const
{
  return lsp_mouse_hover_token_start;
}
int lsp_mouse_hover_token_end_for_test() const
{
  return lsp_mouse_hover_token_end;
}
// The last statusline message, for asserting what an action reported. The
// visible text comes from here too when no Lua status_line handler owns it.
const std::string &message_for_test() const
{
  return last_message;
}
// Headless mouse driver for tests: feeds a synthetic mouse event through
// the real handle_mouse path (pane hit-test, selection, edge-panning).
void mouse_event_for_test(int x, int y, int bstate);
void mouse_event_for_test(int x, int y, int bstate, bool ctrl);
// The wheel's own entry point (handle_mouse_input), which the terminal
// backend calls for a scroll notch -- a separate path from the clicks above,
// so a modal that swallows the pointer has to be pinned on both.
void wheel_event_for_test(int x, int y, bool up, bool down)
{
  handle_mouse_input(x, y, false, up, down);
}
void create_new_buffer_for_test();
void move_to_line_start_for_test();
void render_for_test();
// One full frame-loop step (the per-frame blink/scheduling logic plus a
// render), for tests that need to observe behaviour over time.
void render_frame_for_test();
// The frame loop's ask for the repaint that ends the occurrence highlight's
// typing hold (the method itself is private in api/paint.h): a case drives an
// edit, winds the hold's clock, and reads whether the frame was asked for.
bool word_highlight_due_soon_for_test()
{
  return word_highlight_due_soon();
}
// --- home screen (test) ---
// The rows the model built for the last frame, in layout order: what a digit
// press walks, the key it prints, and the query the screen is filtered by.
std::vector<std::string> home_row_paths_for_test() const;
std::vector<char> home_row_keys_for_test() const;
// One key through the real dispatcher (the home handler runs first), so a
// case drives the filter and the row keys the way a terminal does.
void home_input_for_test(int ch);
bool home_visible_for_test() const
{
  return show_home_menu;
}
int home_selected_for_test() const
{
  return home_menu_selected;
}
const std::string &home_filter_for_test() const
{
  return home_filter;
}
// The project and folder scans are cached for a few seconds (the model is
// rebuilt per frame); a case that reshapes the tree on disk drops them.
void home_scan_cache_clear_for_test();
// --- smooth scrolling (test) ---
// Requests an animation and steps it at explicit timestamps, so the easing
// curve can be asserted on without a clock.
bool scroll_view_smooth_for_test(int lines, int base_ms)
{
  return scroll_view_smooth(lines, base_ms);
}
bool advance_smooth_scroll_for_test(long long now_ms)
{
  return advance_smooth_scroll(now_ms);
}
bool smooth_scroll_active_for_test() const
{
  return smooth_scroll_.active;
}
int smooth_scroll_target_for_test() const
{
  return smooth_scroll_.in_flight.target;
}
long long smooth_scroll_start_ms_for_test() const
{
  return smooth_scroll_.start_ms;
}
long long smooth_scroll_duration_for_test() const
{
  return smooth_scroll_.duration_ms;
}
// Reads the animation's easing, so a settings change can be asserted on.
const char *smooth_scroll_easing_for_test() const
{
  return SmoothScroll::easing_name(smooth_scroll_easing_);
}
bool smooth_scroll_enabled_for_test() const
{
  return smooth_scroll_enabled_;
}
FileBuffer &buffer_for_test(int id = -1);
// How many tabs the strip is holding, so a case can assert that a command
// the chord reached actually closed or opened one (see test_alt_chords.cpp).
int buffer_count_for_test() const
{
  return (int)buffers.size();
}
SplitPane &pane_for_test(int id = -1);
// Every pane as laid out by the last update_pane_layout, so a test can check
// the row budget each one was charged (see test_pane_row_budget.cpp).
const std::vector<SplitPane> &panes_for_test() const
{
  return panes;
}
void split_pane_for_test(bool vertical)
{
  if (vertical)
  {
    split_pane_vertical();
  }
  else
  {
    split_pane_horizontal();
  }
  update_pane_layout();
}
void toggle_pane_zoom_for_test()
{
  toggle_pane_zoom();
  update_pane_layout();
}
void close_current_pane_for_test()
{
  close_pane();
  update_pane_layout();
}
// Settings-menu state accessors for headless tests (the menu surface is
// private EditorState; tests drive it through these + handle_settings_input).
bool settings_menu_open_for_test() const
{
  return show_settings_menu;
}
const std::vector<SettingsEntry> &settings_entries_for_test() const
{
  return settings_entries;
}
void settings_select_for_test(int index)
{
  settings_selected = index;
}
// The search bar's query and what it keeps: the filtered list is what
// navigation and selection walk, so a test reads keys, not config indices.
std::string settings_query_for_test() const
{
  return settings_query;
}
int settings_match_count_for_test() const
{
  return (int)settings_filtered.size();
}
std::string settings_key_at_for_test(int position) const
{
  if (position < 0 || position >= (int)settings_filtered.size())
    return "";
  return settings_entries[(size_t)settings_filtered[(size_t)position]].key;
}
std::string settings_selected_key_for_test() const
{
  int pos = settings_selected;
  if (pos < 0)
    pos = 0;
  if (pos > (int)settings_filtered.size() - 1)
    pos = (int)settings_filtered.size() - 1;
  return settings_key_at_for_test(pos);
}
int settings_selected_pos_for_test() const
{
  return settings_selected;
}
// The painted rows: a section header, or an entry at a position in the
// selectable list. The panel puts a *header* row wherever the group changes,
// so these are what the picture and the scroll window are made of.
int settings_row_count_for_test() const
{
  return (int)settings_rows.size();
}
std::string settings_row_header_for_test(int row) const
{
  if (row < 0 || row >= (int)settings_rows.size())
    return "";
  return settings_rows[(size_t)row].header;
}
std::string settings_row_key_for_test(int row) const
{
  if (row < 0 || row >= (int)settings_rows.size())
    return "";
  const int pos = settings_rows[(size_t)row].entry_pos;
  return pos < 0 ? "" : settings_key_at_for_test(pos);
}
// Which group a key is filed under, and the order the groups are listed in.
std::string settings_section_for_test(const std::string &key) const
{
  for (const SettingsEntry &e : settings_entries)
  {
    if (e.key == key)
      return e.section;
  }
  return "";
}
const SettingsEntry *settings_entry_for_test(const std::string &key) const
{
  for (const SettingsEntry &e : settings_entries)
  {
    if (e.key == key)
      return &e;
  }
  return nullptr;
}
// Re-anchors the blink phase without the input-pause hold restart_blink()
// applies (which would keep the caret solid through the whole measurement).
// A test that watches the caret flip needs a known phase to start from; this
// is the clock restart a pointer move uses.
void reset_blink_phase_for_test()
{
  restart_blink_pointer();
}
// Moves the end of the input pause without waiting it out: a case that pins
// the half after a pause would otherwise sit through the real hold first.
void set_blink_pause_end_for_test(long long until_ms)
{
  blink_suspend_until_ms = until_ms;
  blink_visible = true;
  needs_redraw = true;
}
// The choices drop-down state, and the cells the render pass recorded for a
// visible row (what the mouse hit test reads).
bool settings_dropdown_open_for_test() const
{
  return settings_dropdown_open;
}
int settings_dropdown_index_for_test() const
{
  return settings_dropdown_index;
}
// The number of choice rows the drop-down's painted box holds (0 before a
// frame has placed it).
int settings_dropdown_rows_for_test() const
{
  return settings_dropdown_open ? std::max(0, settings_dropdown_h - 2) : 0;
}
// The panel's and the drop-down's painted boxes, for the placement rules the
// picture has to satisfy (inside the panel, clear of the row that owns it).
void settings_panel_rect_for_test(int &x, int &y, int &w, int &h) const
{
  x = settings_panel_x;
  y = settings_panel_y;
  w = settings_panel_w;
  h = settings_panel_h;
}
bool settings_dropdown_rect_for_test(int &x, int &y, int &w, int &h) const
{
  if (!settings_dropdown_open)
    return false;
  x = settings_dropdown_x;
  y = settings_dropdown_y;
  w = settings_dropdown_w;
  h = settings_dropdown_h;
  return true;
}
bool settings_row_cells_for_test(
    const std::string &key, int &row_y, int &value_x, int &step_down_x, int &step_up_x) const
{
  for (const SettingsEntry &e : settings_entries)
  {
    if (e.key != key)
      continue;
    if (e.row_y < 0)
      return false;
    row_y = e.row_y;
    value_x = e.value_x;
    step_down_x = e.step_down_x;
    step_up_x = e.step_up_x;
    return true;
  }
  return false;
}
std::string config_value_for_test(const std::string &key)
{
  return config.get(key, "");
}
int config_int_for_test(const std::string &key)
{
  return config.get_int(key, 0);
}
// Sets one the way the settings menu or Lua would, so a test can drive a
// behaviour the editor reads out of its config.
void set_config_for_test(const std::string &key, const std::string &value)
{
  config.set(key, value);
}
// The save the editor runs on Ctrl+S, so a test can pin what a save leaves on
// disk without a keystroke or a tab.
bool save_buffer_for_test(int index, bool announce = true)
{
  return save_buffer_at(index, announce);
}
// Feeds one key through the settings menu's real input handler.
bool settings_input_for_test(int ch)
{
  return handle_settings_input(ch);
}
// Feeds a paste through the event dispatch a terminal paste takes, so a test
// pins where a pasted block lands (the settings panel's field or the buffer)
// without a pty.
void paste_event_for_test(const std::string &text)
{
  Event ev{};
  ev.type = EVENT_PASTE;
  ev.paste.text = text.c_str();
  handle_terminal_event(ev);
}
// Terminal panel state for headless tests: the integrated-terminal
// fields are private EditorState, so tests configure them directly to
// exercise the panel geometry / resize-drag math without spawning a
// shell.
void set_terminal_state_for_test(bool show, bool zoom, int height)
{
  show_integrated_terminal = show;
  terminal_zoom_active = zoom;
  integrated_terminal_height = std::max(5, height);
  update_pane_layout();
}
bool terminal_zoom_active_for_test() const
{
  return terminal_zoom_active;
}
bool terminal_visible_for_test() const
{
  return show_integrated_terminal;
}
int terminal_panel_h_for_test() const
{
  return integrated_terminal_panel_h();
}
int terminal_panel_y_for_test() const
{
  return integrated_terminal_panel_y();
}
int terminal_panel_w_for_test() const
{
  return integrated_terminal_panel_w();
}
// The docked shell's own content span, which the session list (and its
// divider gutter) narrows.
int docked_terminal_view_w_for_test()
{
  return docked_terminal_view().w;
}
int docked_terminal_view_h_for_test()
{
  return docked_terminal_view().h;
}
// Floating terminal (Alt+Shift+T, :termfloat). The box is registered
// shell-less (like add_terminal_for_test), so the geometry, the routing and
// the selection math run against a live vterm without a process.
void add_floating_terminal_for_test()
{
  floating_terminal = std::make_unique<IntegratedTerminal>();
  floating_terminal->mark_active_for_test();
  floating_terminal->set_label("Floating terminal");
  floating_terminal->set_focused(true);
  show_floating_terminal = true;
  // The box has to be painted by the next frame (the production open path
  // marks the frame dirty too).
  needs_redraw = true;
}
void toggle_floating_terminal_for_test()
{
  toggle_floating_terminal();
}
void floating_terminal_key_for_test(int ch, bool ctrl = false, bool shift = false, bool alt = false)
{
  handle_floating_terminal_input(ch, ctrl, shift, alt);
}
// `ctrl` is the modifier state the dispatcher passes through: a Ctrl+click
// on a link opens it instead of starting a selection.
bool floating_terminal_mouse_for_test(
    int x, int y, bool click, bool motion, bool release, bool ctrl = false)
{
  return handle_floating_terminal_mouse(x, y, click, motion, release, ctrl);
}
// The box's own shell, for tests that feed it output or read its rows.
IntegratedTerminal *floating_terminal_for_test()
{
  return floating_terminal.get();
}
bool floating_terminal_visible_for_test() const
{
  return show_floating_terminal;
}
// True while the shell object exists: hiding the box must not drop it.
bool floating_terminal_alive_for_test() const
{
  return floating_terminal != nullptr;
}
TerminalBox floating_terminal_rect_for_test() const
{
  return floating_terminal_rect();
}
// Sidebar panel height as render_sidebar() computes it: the side column
// (full height, so the strip does not shorten it) minus the terminal's real
// reserved footprint.
int sidebar_panel_h_for_test() const
{
  return side_column().h;
}
// The left column's stacked sections: which are collapsed, which one holds
// the cursor, and where that cursor and its window sit.
struct SidebarSectionsForTest
{
  bool explorer_open = true;
  bool outline_open = true;
  bool timeline_open = true;
  int section = 0;
  int outline_cursor = 0;
  int outline_scroll = 0;
  int timeline_cursor = 0;
  int timeline_scroll = 0;
};
SidebarSectionsForTest sidebar_sections_for_test() const
{
  SidebarSectionsForTest state;
  state.explorer_open = sidebar_explorer_open;
  state.outline_open = sidebar_outline_open;
  state.timeline_open = sidebar_timeline_open;
  state.section = (int)sidebar_section;
  state.outline_cursor = sidebar_outline_selected;
  state.outline_scroll = sidebar_outline_scroll;
  state.timeline_cursor = sidebar_timeline_selected;
  state.timeline_scroll = sidebar_timeline_scroll;
  return state;
}
void set_sidebar_sections_for_test(const SidebarSectionsForTest &state)
{
  sidebar_explorer_open = state.explorer_open;
  sidebar_outline_open = state.outline_open;
  sidebar_timeline_open = state.timeline_open;
  sidebar_section = (jot_sidebar::Section)state.section;
  sidebar_outline_selected = state.outline_cursor;
  sidebar_outline_scroll = state.outline_scroll;
  sidebar_timeline_selected = state.timeline_cursor;
  sidebar_timeline_scroll = state.timeline_scroll;
}
// The sidebar's stacked-section geometry, the model the painter, the hit-test
// and the keyboard walk all read, so a case can aim a click at a row that is
// really painted there.
jot_sidebar::Layout sidebar_layout_for_test() const
{
  return sidebar_sections_layout();
}
// The data-row index a sidebar hit names (see sidebar_data_index): the case
// checks the offset the sections' scroll windows add back.
int sidebar_data_index_for_test(int section, int index) const
{
  SidebarHit hit;
  hit.section = section;
  hit.index = index;
  return sidebar_data_index(hit);
}
// The row the pointer sits on in the sidebar: `section` is the section enum,
// `index` its display row inside it (-1 = the header, -2 = no row).
struct SidebarHoverForTest
{
  int section = -1;
  int index = -2;
};
SidebarHoverForTest sidebar_hover_for_test() const
{
  SidebarHoverForTest hover;
  hover.section = sidebar_hover_section;
  hover.index = sidebar_hover_index;
  return hover;
}
// The column a sidebar row's icon slot sits in (the outline's chevron).
int sidebar_content_x_for_test() const
{
  return sidebar_content_x();
}
// The dock's outline as the render and the mouse read it: the row the pointer
// highlights (-1 = none), the cursor's visible row, the visible-row order
// (flat symbol indices, depth-first), whether a container is open, and where
// the body's rows sit.
int outline_selected_for_test() const
{
  return outline_panel.selected;
}
int outline_scroll_for_test() const
{
  return outline_panel.scroll;
}
int outline_hover_row_for_test() const
{
  return outline_panel.hover_row;
}
std::vector<int> outline_visible_for_test() const
{
  return outline_panel.visible;
}
// The tree the outline was built from, in flat order.
std::vector<SymbolMatch> outline_symbols_for_test() const
{
  return outline_panel.symbols;
}
bool outline_expanded_for_test(int flat_index) const
{
  return flat_index >= 0 && flat_index < (int)outline_panel.expanded.size()
         && outline_panel.expanded[(size_t)flat_index];
}
// The first body row of the dock's outline and how many it shows (the
// renderer's own arithmetic: panel_y + 3, panel_h - 3).
int outline_body_y_for_test() const
{
  return side_column_top() + 3;
}
int outline_body_h_for_test() const
{
  const int panel_h = std::max(1, grid_height() - status_height - side_column_top());
  return std::max(0, panel_h - 3);
}
// Runs the outline's rebuild gate directly, so a case can prove a fold survives
// the rebuild a keystroke schedules without typing into the buffer.
void ensure_outline_fresh_for_test(bool force)
{
  ensure_outline_fresh(force);
}
bool terminal_resize_dragging_for_test() const
{
  return terminal_resize_dragging;
}
// Feeds the terminal top-border drag through the real private handlers.
bool terminal_resize_begin_for_test(int x, int y)
{
  return begin_terminal_resize_drag(x, y);
}
bool terminal_resize_update_for_test(int y)
{
  return update_terminal_resize_drag(y);
}
void terminal_resize_end_for_test()
{
  end_terminal_resize_drag();
}
int ui_width_for_test() const
{
  return grid_width();
}
int ui_height_for_test() const
{
  return grid_height();
}
// Applies a theme the way the chooser does, without persisting it: a test
// asserts the palette the engine produced, not the config write.
bool apply_theme_for_test(const std::string &name)
{
  return apply_theme(name, false, false);
}
// What the theme chooser lists, sorted and de-duplicated.
std::vector<std::string> available_themes_for_test()
{
  return list_available_themes();
}
// The name of the active theme, as the chooser reports it (the resolved
// name, so a legacy alias reads back as the theme it resolved to).
const std::string &theme_name_for_test() const
{
  return current_theme_name;
}
// The active theme, for tests that assert on painted colors.
const Theme &theme_for_test() const
{
  return theme;
}
// Absolute bracket depth at the start of `line` (the same value the renderer
// seeds a scrolled-to line with), so tests can assert that painted rainbow
// colors match file position.
int bracket_depth_for_test(int line)
{
  return bracket_depth_at_line_start(get_buffer(), line);
}
// The syntax colors one buffer line resolves to, so a test can assert that a
// path which rewrites the buffer in place (the save-time trim) leaves
// highlighting where it was.
std::vector<std::pair<int, int>> line_syntax_colors_for_test(int line)
{
  return get_line_syntax_colors(get_buffer(), line);
}
// The Emmet entry point the snippet keymap calls from Tab (see
// Editor::expand_emmet_abbreviation). Exposed for tests because the real
// trigger goes through the Lua keymap, which the raw-key test path does not
// run.
bool expand_emmet_for_test()
{
  return expand_emmet_abbreviation();
}
// Places the cursor and lets the viewport follow it through the same
// ensure_cursor_visible the editing paths use, so tests can drive vertical and
// horizontal scrolling without faking scroll offsets the editor would clamp.
void scroll_cursor_to_for_test(int line, int col)
{
  FileBuffer &buf = get_buffer();
  buf.cursor = {col, line};
  buf.preferred_x = col;
  ensure_cursor_visible();
}
// Workspace diagnostics read from the per-server store, so a test needs a way
// to put something in it without a server. `client_key` stands in for the
// server, which is what the dedupe across servers keys on.
void seed_lsp_diagnostic_for_test(const std::string &client_key,
                                  const std::string &path,
                                  int line,
                                  int severity,
                                  const std::string &message);
// The typing-pause hold on live diagnostics: run the merge a publish runs,
// age the last keystroke instead of sleeping through the quiet window, run
// the frame timer's flush, and ask what is waiting. The hold itself is
// decided from the stamps notify_lsp_change/notify_lsp_save set, so a case
// drives it through the real edit and save paths.
void refresh_lsp_diagnostics_for_test(const std::string &path)
{
  refresh_lsp_diagnostics_for(path);
}
void age_lsp_typing_for_test(const std::string &path, int ms)
{
  const auto it = lsp_last_edit_ms_.find(path);
  if (it != lsp_last_edit_ms_.end())
  {
    it->second -= ms;
  }
}
void paint_held_lsp_diagnostics_for_test()
{
  maybe_paint_held_lsp_diagnostics();
}
bool lsp_diagnostics_held_for_test(const std::string &path) const
{
  return lsp_diagnostics_held_.count(path) > 0;
}
// Seeds the completion popup the way a landed server response does: the site
// (token under the caret, prefix as typed) is recorded through the same
// arm_lsp_completion a request uses, and `items` are filtered through the same
// pass a keystroke runs. Returns whether anything is left to show. This is how
// a case about what the caret previews while typing reaches the code path
// without a server.
bool seed_lsp_completion_for_test(std::vector<LSPCompletionItem> items)
{
  arm_lsp_completion(get_buffer().filepath, false);
  lsp_completion_all_items = std::move(items);
  return refresh_lsp_completion_filter();
}
// The workspace's CSS vocabulary (workspace/web_completion.h) as the editor
// holds it, so a case can complete against a workspace without scanning one.
void set_web_index_for_test(WebCompletion::Index index)
{
  apply_web_index(std::move(index));
}
// The labels the index contributes for the caret's context -- the half of the
// request-time list append_web_index_completions is responsible for. Empty
// when the caret is somewhere a name does not belong.
std::vector<std::string> web_index_completions_for_test()
{
  std::vector<std::string> labels;
  std::vector<LSPCompletionItem> items;
  if (!append_web_index_completions(items))
  {
    return labels;
  }
  labels.reserve(items.size());
  for (const LSPCompletionItem &item : items)
  {
    labels.push_back(item.label);
  }
  return labels;
}
// The preview the caret would paint: the selected item's insert text with the
// typed prefix taken off its front (see update_lsp_completion_ghost).
std::string lsp_completion_ghost_for_test() const
{
  return lsp_completion_ghost_text;
}
// Which row of the popup's list is selected, and what the rows are -- the two
// things Up/Down and the per-frame re-filter move (see
// refresh_lsp_completion_filter).
int lsp_completion_selected_for_test() const
{
  return lsp_completion_selected;
}
std::vector<std::string> lsp_completion_labels_for_test() const
{
  std::vector<std::string> labels;
  labels.reserve(lsp_completion_items.size());
  for (const LSPCompletionItem &item : lsp_completion_items)
  {
    labels.push_back(item.label);
  }
  return labels;
}
// Where the popup painted its box on the last frame (border included), so a
// mouse test can aim the wheel at it the way a user does. w is 0 while the
// popup is not on screen; see the LSP state's lsp_completion_box_*.
int lsp_completion_box_x_for_test() const
{
  return lsp_completion_box_x;
}
int lsp_completion_box_y_for_test() const
{
  return lsp_completion_box_y;
}
int lsp_completion_box_w_for_test() const
{
  return lsp_completion_box_w;
}
int lsp_completion_box_h_for_test() const
{
  return lsp_completion_box_h;
}
std::string lsp_completion_prefix_for_test() const
{
  return lsp_completion_prefix;
}
bool lsp_completion_visible_for_test() const
{
  return lsp_completion_visible;
}
// How many times the ranking read the buffer's words back (see
// rebuild_completion_context): one per word being completed, not one per
// keystroke of it.
unsigned long long completion_context_rebuilds_for_test() const
{
  return completion_context_rebuilds;
}
// Asks for completions the way a typed key does, so a case can pin what is
// offered where (the builtin tag list is only for a tag name, the word right
// after the `<` that opens the tag).
void request_lsp_completion_for_test(char trigger_character)
{
  request_lsp_completion(false, trigger_character);
}
// Accepts the selected row the way the Tab key does (the Lua binding and the
// native path both land here), returning whether anything was written.
bool apply_selected_lsp_completion_for_test()
{
  return apply_selected_lsp_completion();
}
// What the insert path does on every keystroke before the frame paints: the
// held items are filtered against the word the caret now sits in.
void refresh_lsp_completion_for_test()
{
  refresh_lsp_completion_filter();
}
// The preview is there but the typing has not paused yet, so the painter is
// leaving the caret's row alone until the clock passes the delay.
bool lsp_completion_preview_withheld_for_test() const
{
  return lsp_completion_preview_withheld();
}
// Ages that clock by `ms`, the way a pause would -- the preview lands once it
// is older than `lsp_completion_ghost_delay_ms`.
void age_lsp_completion_typing_for_test(int ms)
{
  lsp_completion_typing_ms -= ms;
}
std::vector<QuickPickItem> workspace_diagnostics_for_test() const
{
  return workspace_diagnostic_quick_pick_items();
}
// The C++ definition checks normally run on the worker thread; a case runs the
// same job on the calling thread and lands it through the same apply path, so
// what it asserts is what a real scan publishes. The unsaved buffers' text
// goes with it, exactly as the worker's own call does.
void run_cpp_definitions_scan_for_test()
{
  apply_cpp_definitions(CppDefinitions::scan_workspace(
      root_dir, CppDefinitions::ScanLimits(), cpp_definitions_unsaved_texts()));
}
int cpp_definitions_missing_for_test() const
{
  return cpp_defs_stats.missing;
}
int cpp_definitions_duplicates_for_test() const
{
  return cpp_defs_stats.duplicates;
}
int cpp_definitions_files_for_test() const
{
  return cpp_defs_stats.files_scanned;
}
// What the poll timer asks with: whether the unsaved C/C++ buffers now hold
// signatures the last scan could not have seen.
bool cpp_definitions_dirty_changed_for_test()
{
  return cpp_definitions_dirty_changed();
}
std::string cpp_definitions_summary_for_test() const
{
  return cpp_definitions_summary();
}
// The findings as the `:cppcheck next` walk sees them: the definition checks'
// rows in file/line order (the panel's list merges the servers in and sorts by
// severity, which is a different order).
std::vector<QuickPickItem> cpp_definition_findings_for_test() const
{
  return cpp_definition_quick_pick_items();
}
// The Problems view's header row, built as the renderer builds it.
std::string problems_header_for_test(int max_width) const
{
  return problems_summary_header(max_width);
}
// Steps to a definition finding through the same path `:cppcheck next|prev`
// and an announced scan use, so a case can assert where the caret lands.
bool cpp_definitions_jump_for_test(int direction)
{
  return goto_next_cpp_definition_issue(direction);
}
// What a file's diagnostics hold after the merge (LSP slices + the definition
// checks), read through the same store the gutter and the picker read.
std::size_t diagnostics_count_for_test(const std::string &path) const
{
  std::error_code ec;
  for (const auto &buf : buffers)
  {
    const bool same =
        buf.filepath == path
        || (!buf.filepath.empty() && !path.empty() && std::filesystem::exists(buf.filepath, ec)
            && std::filesystem::exists(path, ec)
            && std::filesystem::equivalent(buf.filepath, path, ec));
    if (same)
    {
      return buf.diagnostics.size();
    }
  }
  const auto it = cpp_def_diags.find(path);
  return it == cpp_def_diags.end() ? 0 : it->second.size();
}
// --- bottom panel: view switching and the Problems list (test) ---
void set_bottom_panel_view_for_test(int view)
{
  bottom_panel_view = (BottomPanelView)view;
}
int bottom_panel_view_for_test() const
{
  return (int)bottom_panel_view;
}
int problems_selected_for_test() const
{
  return problems_selected;
}
int problems_scroll_for_test() const
{
  return problems_scroll;
}
bool bottom_panel_key_for_test(int ch, bool ctrl = false, bool shift = false, bool alt = false)
{
  return handle_bottom_panel_input(ch, ctrl, shift, alt);
}
bool bottom_panel_mouse_for_test(int x, int y, bool click)
{
  return handle_bottom_panel_mouse(x, y, click);
}
bool problems_scroll_input_for_test(int x, int y, bool up, bool down)
{
  return handle_problems_scroll(x, y, up, down);
}
int bottom_panel_view_tabs_width_for_test() const
{
  return bottom_panel_view_tabs_width();
}
static const char *bottom_panel_view_label_for_test(int view)
{
  return bottom_panel_view_label(view);
}
int bottom_panel_view_tab_x_for_test(int view) const
{
  return bottom_panel_view_tab_x(view);
}
int bottom_panel_view_tab_y_for_test() const
{
  return bottom_panel_view_tab_y();
}
int bottom_panel_shell_list_w_for_test() const
{
  return bottom_panel_shell_list_w();
}
int bottom_panel_shell_list_x_for_test() const
{
  return bottom_panel_shell_list_x();
}
int bottom_panel_shell_list_at_for_test(int x, int y) const
{
  return bottom_panel_shell_list_at(x, y);
}
int integrated_terminal_count_for_test() const
{
  return (int)integrated_terminals.size();
}
int current_integrated_terminal_for_test() const
{
  return current_integrated_terminal;
}
std::string integrated_terminal_session_label_for_test(int index) const
{
  return integrated_terminal_session_label(index);
}
std::string integrated_terminal_display_name_for_test(int index) const
{
  return integrated_terminal_display_name(index);
}
// The terminal's own name (get_label), as opposed to the label the session
// list draws: the two differ once a custom name was elided for display.
std::string integrated_terminal_name_for_test(int index) const
{
  if (index < 0 || index >= (int)integrated_terminals.size() || !integrated_terminals[index])
  {
    return {};
  }
  return integrated_terminals[index]->get_label();
}
int bottom_panel_content_y_for_test() const
{
  return bottom_panel_content_y();
}
int bottom_panel_content_h_for_test() const
{
  return bottom_panel_content_h();
}
void show_problems_panel_for_test()
{
  show_problems_panel();
}
int panel_reserved_h_for_test() const
{
  return integrated_terminal_reserved_h();
}
int focus_state_for_test() const
{
  return (int)focus_state;
}
void focus_sidebar_for_test()
{
  show_sidebar = true;
  focus_state = FOCUS_SIDEBAR;
}
void editor_input_for_test(int ch, bool ctrl = false, bool shift = false, bool alt = false)
{
  handle_input(ch, ctrl, shift, alt, ch);
}
bool editor_running_for_test() const
{
  return running;
}
bool terminal_focused_for_test()
{
  IntegratedTerminal *term = get_integrated_terminal();
  return term != nullptr && term->is_focused();
}
// Selection manipulation: drive the real commands from a test.
void keep_primary_selection_for_test()
{
  keep_primary_selection();
}
bool rotate_primary_for_test(int direction)
{
  return rotate_primary_selection(direction);
}
bool add_caret_adjacent_for_test(int direction)
{
  return add_caret_on_adjacent_line(direction);
}
bool split_lines_for_test()
{
  return split_selection_on_newlines();
}
bool select_occurrences_for_test()
{
  return select_all_occurrences();
}
// Textobjects: drive the real commands from a test.
bool expand_selection_for_test()
{
  return expand_selection_to_node();
}
bool shrink_selection_for_test()
{
  return shrink_selection_to_node();
}
bool select_textobject_for_test(const std::string &kind, bool inner)
{
  return select_textobject(kind, inner);
}
bool goto_function_for_test(int direction)
{
  return goto_relative_function(direction);
}
// True when the buffer has a parsed syntax tree, so a test can skip rather
// than fail on a machine with no grammar installed. Parses on demand: the
// editor builds a buffer's tree when a frame first paints it, and a headless
// test draws nothing, so this asks for the parse the way the renderer would.
bool syntax_tree_ready_for_test()
{
#ifdef JOT_TREESITTER
  init_ts_for_buffer(get_buffer());
  return get_buffer().ts_tree != nullptr;
#else
  return false; // no tree-sitter in this build, so no buffer ever has a tree
#endif
}
// The invalidation the paths that rewrite a buffer from outside the edit hooks
// call (the on-save formatters, LSP formats, Lua buffer writes): a headless
// test drives the rewrite through this instead of the external tool.
void invalidate_syntax_cache_for_test()
{
  invalidate_syntax_cache(get_buffer());
}
// The parse-tree retention budget (see kMaxCachedSyntaxTrees): a headless test
// cannot drive the render loop, so it parses and stamps buffers through these
// seams and then runs the sweep the loop runs at the end of a frame.
#ifdef JOT_TREESITTER
void init_syntax_for_buffer_for_test(int id)
{
  init_ts_for_buffer(buffers[(size_t)id]);
}
// The stamp render_pane gives a buffer it drew this frame.
void mark_syntax_buffer_shown_for_test(int id)
{
  buffers[(size_t)id].ts_last_use_seq = ++ts_use_seq_;
}
void run_syntax_tree_sweep_for_test()
{
  evict_stale_syntax_trees();
}
int syntax_tree_holder_count_for_test() const
{
  int holders = 0;
  for (const FileBuffer &buf : buffers)
  {
    if (buf.ts_tree)
    {
      holders++;
    }
  }
  return holders;
}
#endif
// Runs an ex command line the way the palette does, so command plumbing can
// be asserted without typing into a prompt.
// The plan the installer would run for a language server: its id, the shell
// script and the status message. Lets a test pin the manager choice (a
// bundled payload vs a download) without spawning a background job.
// Defined in test_hooks.cpp: editor.h only forward-declares LuaAPI.
bool lsp_install_plan_for_test(const std::string &name,
                               std::string *id,
                               std::string *script,
                               std::string *message);
// The HTTP client minus the network: rest_select_for_test picks and resolves
// the request `:rest` would (it never sends), rest_deliver_for_test lands
// canned curl output through the same path a real reply takes, and
// rest_response_lines_for_test reads the response tab back.
bool rest_select_for_test(const std::string &name, HttpFile::Resolved &out)
{
  return rest.prepare(name, out);
}
void rest_deliver_for_test(const HttpFile::Resolved &request, const std::string &output)
{
  rest.show_response(request, output);
}
std::vector<std::string> rest_response_lines_for_test() const
{
  for (const FileBuffer &buffer : buffers)
  {
    if (buffer.filepath.rfind("[Response]", 0) == 0)
    {
      return buffer.lines;
    }
  }
  return {};
}
bool rest_request_running_for_test() const
{
  return rest.running();
}
// The Bible reader minus the network: the fetch is skipped by pre-seeding the
// cache (see test/test_bible_client.cpp), and the passage it paints is read
// back here. The verse-of-the-day accessors let a case assert the day's pick
// without depending on which chapter today happens to land on.
std::vector<std::string> bible_view_lines_for_test() const
{
  for (const FileBuffer &buffer : buffers)
  {
    if (buffer.filepath.rfind("[Bible]", 0) == 0)
    {
      return buffer.lines;
    }
  }
  return {};
}
std::string bible_verse_of_the_day_label_for_test() const
{
  return bible_votd_label;
}
std::string bible_verse_of_the_day_text_for_test() const
{
  return bible_votd_text;
}
std::string bible_verse_of_the_day_for_test()
{
  return bible_verse_of_the_day();
}
bool bible_request_running_for_test() const
{
  return bible_request_running;
}
void run_ex_for_test(const std::string &line)
{
  execute_ex_command(line);
}
// Rename prompt: open it and drive its input handler.
void open_rename_prompt_for_test()
{
  open_rename_prompt();
}
void rename_prompt_input_for_test(int ch)
{
  handle_rename_prompt(ch);
}
bool rename_prompt_visible_for_test() const
{
  return show_rename_prompt;
}
// The save / quit prompts: the file menu and Ctrl+Q raise them when a buffer
// is dirty. A test drives the flag instead of dirtying a buffer first, so the
// modal's frame can be rendered on its own.
void open_quit_prompt_for_test()
{
  show_quit_prompt = true;
  needs_redraw = true;
}
void open_save_prompt_for_test()
{
  show_save_prompt = true;
  needs_redraw = true;
}
bool quit_prompt_visible_for_test() const
{
  return show_quit_prompt;
}
void dismiss_quit_prompt_for_test()
{
  show_quit_prompt = false;
  needs_redraw = true;
}
const std::string &rename_prompt_text_for_test() const
{
  return rename_prompt_input;
}
// Jumplist: drive the real commands and read the history back. The probe
// editor is shared between cases, so tests reset the history first.
void reset_jumplist_for_test()
{
  jump_history.clear();
  jump_index = -1;
  jump_pending = false;
  jump_pending_location = {};
}
void record_jump_for_test()
{
  record_jump();
}
void jump_back_for_test()
{
  jump_back();
}
void jump_forward_for_test()
{
  jump_forward();
}
int jump_count_for_test() const
{
  return (int)jump_history.size();
}
int jump_position_for_test() const
{
  return jump_index;
}
std::string jump_path_for_test(int index) const
{
  if (index < 0 || index >= (int)jump_history.size())
  {
    return {};
  }
  return jump_history[(size_t)index].filepath;
}
int jump_line_for_test(int index) const
{
  if (index < 0 || index >= (int)jump_history.size())
  {
    return -1;
  }
  return jump_history[(size_t)index].cursor.y;
}
// Git status polling: how many refreshes actually parsed the porcelain
// output into the per-file map. A poll whose output digests to the map that
// is already installed must not move this, which is what the memory fix
// rests on (that parse is the allocation a huge repo made the largest in
// the process).
long long git_status_parse_count_for_test() const
{
  return git_status_parse_count;
}
int git_untracked_count_for_test() const
{
  return git_untracked_count;
}
int git_staged_count_for_test() const
{
  return git_staged_count;
}
int git_unstaged_count_for_test() const
{
  return git_unstaged_count;
}
std::string git_branch_for_test() const
{
  return git_branch;
}
// The poll the 1.5 s timer runs, inline so a test does not wait for it; and
// the repo drop the open-file / workspace paths make.
void refresh_git_status_for_test(bool force = true)
{
  refresh_git_status(force);
}
void clear_git_status_for_test()
{
  clear_git_status();
}
