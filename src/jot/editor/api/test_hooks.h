// ---------------------------------------------------------------------------
// Test surface: core
// ---------------------------------------------------------------------------
//
// The headless hooks the suite drives the engine with: multicursor helpers,
// smooth scroll, settings, panels and terminal geometry, themes, selection and
// textobjects, the rename prompt and the jumplist.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
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
  void delete_selection_for_test();
  void delete_char_for_test(bool forward);
  void insert_string_for_test(const std::string &str);
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
  void create_new_buffer_for_test();
  void move_to_line_start_for_test();
  void render_for_test();
  // One full frame-loop step (the per-frame blink/scheduling logic plus a
  // render), for tests that need to observe behaviour over time.
  void render_frame_for_test();
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
  SplitPane &pane_for_test(int id = -1);
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
  std::string config_value_for_test(const std::string &key)
  {
    return config.get(key, "");
  }
  int config_int_for_test(const std::string &key)
  {
    return config.get_int(key, 0);
  }
  // Feeds one key through the settings menu's real input handler.
  bool settings_input_for_test(int ch)
  {
    return handle_settings_input(ch);
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
  // Sidebar panel height as render_sidebar() computes it: the pane area
  // minus the terminal's real reserved footprint.
  int sidebar_panel_h_for_test() const
  {
    return content_column().h;
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
  std::vector<QuickPickItem> workspace_diagnostics_for_test() const
  {
    return workspace_diagnostic_quick_pick_items();
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
  int bottom_panel_view_tab_y_for_test() const
  {
    return bottom_panel_view_tab_y();
  }
  int bottom_panel_terminal_tab_y_for_test() const
  {
    return bottom_panel_terminal_tab_y();
  }
  std::string integrated_terminal_tab_label_for_test(int index) const
  {
    return integrated_terminal_tab_label(index);
  }
  // The terminal's own name (get_label), as opposed to the label the strip
  // draws: the two differ once a custom name was elided for display.
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
  // than fail on a machine with no grammar installed.
  bool syntax_tree_ready_for_test()
  {
    return get_buffer().ts_tree != nullptr;
  }
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
