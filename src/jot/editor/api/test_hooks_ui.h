// ---------------------------------------------------------------------------
// Test surface: surfaces and lifecycle
// ---------------------------------------------------------------------------
//
// Mouse and terminal input hooks, the terminal tab model, the entry points the
// suite uses to boot and drive the editor itself: render, run, host, reload,
// load_file.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
public:
  // The hosting terminal's size. Meaningless under --gui (it keeps the
  // constructor default because the terminal is never initialised there) --
  // exposed so tests can pin that trap.
  int terminal_height_for_test() const
  {
    return terminal.get_height();
  }
  // Ctrl+hover goto-definition underline (see the mouse dispatcher): true while
  // Ctrl is held over a token, whatever the frontend. The span is the token the
  // underline covers, in logical columns.
  bool ctrl_hover_active_for_test() const
  {
    return ctrl_hover_active;
  }
  bool ctrl_hover_span_for_test(int &start, int &end) const
  {
    if (!ctrl_hover_active)
    {
      return false;
    }
    start = ctrl_hover_start;
    end = ctrl_hover_end;
    return true;
  }
  // A headless test feeds clicks far faster than a hand can, so the mouse
  // dispatcher's 350 ms double/triple-click window would read them as one
  // cluster and turn the later ones into word/line selections. Tests reset it
  // between clicks to keep every click a single click.
  void reset_mouse_clicks_for_test()
  {
    last_left_click_ms = 0;
    last_left_click_count = 0;
    last_left_click_pos = {0, 0};
  }
  // Terminal mouse-selection hooks for headless tests: feeds clicks,
  // motions and releases through the real private handlers. `ctrl` is the
  // modifier state the dispatcher passes through: a Ctrl+click on a link
  // opens it instead of starting a selection.
  bool terminal_mouse_for_test(
      int x, int y, bool click, bool motion, bool release, bool ctrl = false)
  {
    return handle_integrated_terminal_mouse(x, y, click, motion, release, ctrl);
  }
  // The focused docked terminal, for tests that feed it output or read rows.
  IntegratedTerminal *terminal_for_test()
  {
    return get_integrated_terminal();
  }
  // The armed terminal link underline, if any: a full-space row and half-open
  // cell columns, the target the Ctrl+click would open.
  bool terminal_link_hover_span_for_test(int &row, int &start, int &end) const
  {
    if (!terminal_link_hover_active)
    {
      return false;
    }
    row = terminal_link_hover_row;
    start = terminal_link_hover_start;
    end = terminal_link_hover_end;
    return true;
  }
  bool terminal_sel_active_for_test() const
  {
    return terminal_sel_active;
  }
  bool terminal_sel_dragging_for_test() const
  {
    return terminal_sel_dragging;
  }
  int terminal_sel_anchor_row_for_test() const
  {
    return terminal_sel_anchor_row;
  }
  int terminal_sel_anchor_col_for_test() const
  {
    return terminal_sel_anchor_col;
  }
  int terminal_sel_cur_row_for_test() const
  {
    return terminal_sel_cur_row;
  }
  int terminal_sel_cur_col_for_test() const
  {
    return terminal_sel_cur_col;
  }
  // The selected run, exactly as a release hands it to the clipboard.
  std::string terminal_selection_text_for_test()
  {
    return terminal_selection_text();
  }
  // Which view owns the live selection: the anchors are shared between the
  // docked panel and the floating box.
  bool terminal_sel_in_float_for_test() const
  {
    return terminal_sel_in_float;
  }
  // Registers a shell-less terminal so mouse/key handlers have a live
  // vterm-backed target without spawning a process. `label` seeds a custom tab
  // name (what task/plugin terminals carry); empty keeps the generated one.
  void add_terminal_for_test(const std::string &label = "")
  {
    auto term = std::make_unique<IntegratedTerminal>();
    term->mark_active_for_test();
    term->set_label(label);
    integrated_terminals.push_back(std::move(term));
    current_integrated_terminal = (int)integrated_terminals.size() - 1;
    show_integrated_terminal = true;
  }
  // Closes the terminal at the given index through the real handler.
  void close_terminal_for_test(int index)
  {
    close_integrated_terminal(index);
  }
  // Opens/closes the settings menu (the :settings command path).
  void toggle_settings_menu_for_test()
  {
    toggle_settings_menu();
  }
  // Forces the menu closed so each test case starts from a known state.
  void close_settings_menu_for_test()
  {
    close_settings_menu();
  }
  bool mouse_selecting_for_test() const;
  // Grid resize plumbing (jot/app/resize.cpp) and the sidebar's width-driven
  // auto-hide: the same paths the terminal/GUI resize handlers drive.
  void apply_resize_for_test(int cols, int rows)
  {
    apply_resize(cols, rows);
  }
  bool sidebar_root_selected_for_test() const
  {
    return file_tree_selected == -1;
  }
  std::string command_palette_query_for_test() const
  {
    return command_palette_query;
  }
  // The palette's model as the last refresh left it: what the grid has to show,
  // what the explanation box says about a row, and where the selection sits.
  int command_palette_result_count_for_test() const
  {
    return (int)command_palette_results.size();
  }
  std::string command_palette_label_for_test(int index) const
  {
    if (index < 0 || index >= (int)command_palette_results.size())
    {
      return {};
    }
    return command_palette_results[(size_t)index].label;
  }
  // The explanation box's sentence for a row: the curated help when there is
  // any, the one-line detail otherwise (the renderer's own fallback).
  std::string command_palette_help_for_test(int index) const
  {
    if (index < 0 || index >= (int)command_palette_results.size())
    {
      return {};
    }
    const CommandPaletteSuggestion &suggestion = command_palette_results[(size_t)index];
    return suggestion.help.empty() ? suggestion.detail : suggestion.help;
  }
  std::string command_palette_aliases_for_test(int index) const
  {
    if (index < 0 || index >= (int)command_palette_results.size())
    {
      return {};
    }
    return command_palette_results[(size_t)index].aliases;
  }
  std::string command_palette_args_for_test(int index) const
  {
    if (index < 0 || index >= (int)command_palette_results.size())
    {
      return {};
    }
    return command_palette_results[(size_t)index].args;
  }
  int command_palette_selected_for_test() const
  {
    return command_palette_selected;
  }
  bool command_palette_open_for_test() const
  {
    return show_command_palette;
  }
  // The painted geometry: the same builder the renderer, the cursor placement
  // and the mouse hit-test read, so a test asserts what is on screen.
  CommandPaletteLayout command_palette_layout_for_test()
  {
    return build_command_palette_layout();
  }
  // Grid navigation and the palette's mouse path, through the real handlers.
  void command_palette_move_for_test(int delta_rows, int delta_cols)
  {
    move_command_palette_selection(delta_rows, delta_cols);
  }
  bool command_palette_mouse_for_test(
      int x, int y, bool is_click, bool scroll_up = false, bool scroll_down = false)
  {
    return handle_palette_mouse(x, y, is_click, scroll_up, scroll_down);
  }
  // Drops the bundled UI kit's handler for one surface, so the native renderer
  // paints it: the kit is what normally runs, and the native fallback (a kit
  // that errors or is absent) still has to paint the same cells.
  // Defined in test_hooks.cpp: editor.h only forward-declares LuaAPI.
  void clear_lua_ui_handler_for_test(const std::string &name);
  bool sidebar_visible_for_test() const
  {
    return show_sidebar;
  }
  int sidebar_width_for_test() const
  {
    return effective_sidebar_width();
  }
  // Feeds one raw key code through the frontend's key path: the same
  // decode_key_event -> handle_input sequence the terminal and GUI backends use,
  // so a binding's routing can be tested without a terminal.
  void raw_key_for_test(int raw_ch);
  // Feeds a key through Lua float/keymap arbitration and then native dispatch,
  // matching the terminal frontend's EVENT_KEY path.
  void terminal_key_for_test(int ch, bool ctrl = false, bool shift = false, bool alt = false)
  {
    Event ev{};
    ev.type = EVENT_KEY;
    ev.key = {ch, ctrl, shift, alt};
    handle_terminal_event(ev);
  }
  bool sidebar_auto_hidden_for_test() const
  {
    return sidebar_hidden_for_width_;
  }
  void toggle_sidebar_for_test()
  {
    toggle_sidebar();
  }
  // Discord presence session (jot/editor/discord_controller.h) for headless
  // tests: the same entry points the 1s timer, focus reporting and :discord
  // use.
  void discord_poll_for_test(long long now_ms)
  {
    discord.poll(now_ms);
  }
  void discord_focus_for_test(bool focused, long long now_ms)
  {
    discord.note_focus(focused, now_ms);
  }
  std::string discord_status_for_test() const
  {
    return discord.status();
  }
  bool discord_idle_cleared_for_test() const
  {
    return discord.idle_cleared();
  }
  bool discord_connected_for_test() const
  {
    return discord.rpc().is_connected();
  }
  bool discord_pending_activity_for_test() const
  {
    return discord.rpc().has_pending_activity();
  }
  std::string discord_last_error_for_test() const
  {
    return discord.rpc().last_error();
  }
  std::string discord_command_for_test(const std::string &argument)
  {
    return discord.command(argument);
  }
  // A pending repaint: proves a status change reaches the screen instead of
  // waiting for the next unrelated redraw.
  bool needs_redraw_for_test() const
  {
    return needs_redraw;
  }
  void clear_needs_redraw_for_test()
  {
    needs_redraw = false;
  }
  // Emulates what a settings change does (apply_config_live marks the frame
  // dirty), so a test can render a config change that has no other trigger.
  void request_redraw_for_test()
  {
    needs_redraw = true;
  }
  // The local coding total (features/coding_time.h): planting a number on it is
  // how a test gets an hour on the chip without waiting an hour, and the window
  // is left long idle so nothing is credited on top of what was planted. The
  // last reading is cleared with it, so the next frame re-formats the label
  // instead of trusting the one it already has.
  void set_coding_time_ms_for_test(long long total_ms)
  {
    coding_time.total_ms = total_ms;
    coding_time.last_activity_ms = 0;
    coding_time.last_credit_ms = 0;
    status_time_checked_ms = 0;
  }
  // Points the local coding store at a workspace (features/coding_time.h). A
  // real session does this by opening one, and the store then reads that
  // workspace's own total for today instead of the one it was keyed on before.
  // The window is left long idle, so nothing is credited on top of what is
  // planted either side of this call.
  void set_workspace_root_for_test(const std::string &root)
  {
    root_dir = root;
    coding_time_key = coding_time_current_key();
    coding_time.total_ms = coding_time::total_for(coding_time_totals, coding_time_key);
    coding_time.last_activity_ms = 0;
    coding_time.last_credit_ms = 0;
  }
  // WakaTime's side of the chip (features/wakatime.h): the state the cli answers
  // into. A test sets it here because the real path needs a cli on PATH and a
  // worker queue -- the pty probe covers that end, and this covers what the bar
  // does with an answer.
  void set_wakatime_state_for_test(bool enabled, bool cli_ready, const std::string &today_text)
  {
    wakatime_enabled = enabled;
    wakatime_cli_ready = cli_ready;
    wakatime_today_text = today_text;
    status_time_checked_ms = 0;
  }
  // Overrides a setting the way :settings does, without writing it to the real
  // user config (tests run against a scratch JOT_CONFIG_HOME).
  void config_set_for_test(const std::string &key, const std::string &value)
  {
    config.set(key, value);
  }
  // Applies the settings the way a :settings edit or a Lua jot.config.set does
  // (config.set + apply_config_live), so a test can change a live-read setting
  // without going through the menu surface.
  void apply_config_live_for_test()
  {
    apply_config_live();
  }
  // Headless surface tests: the recording cell grid, and how many visible
  // floats a Lua surface (jot.ui.handler name, e.g. "sidebar", "home_screen")
  // currently owns. The editor constructor already boots the UI kit through
  // initialize_lua_runtime(), so the handlers are live in tests.
  UI *ui_for_test();
  // The chrome heights the row budget is built from: the local tab strip and
  // the breadcrumb winbar (test_pane_row_budget.cpp).
  int tab_height_for_test() const
  {
    return tab_height;
  }
  int winbar_height_for_test() const
  {
    return winbar_height;
  }
  // --- per-pane tab strips (test) ---
  // The strip mode (`pane_tabs`), the window a pane's strip last measured, and
  // the drag session, so a case can carry a tab between panes without a pty
  // (test_pane_tabs.cpp).
  void set_pane_tabs_mode_for_test(int mode)
  {
    pane_tabs_mode = (PaneTabStripMode)mode;
    refresh_pane_headers();
    needs_redraw = true;
  }
  bool pane_shows_tab_strip_for_test(int pane_index) const
  {
    return pane_index >= 0 && pane_index < (int)panes.size()
           && pane_shows_tab_strip(panes[(size_t)pane_index]);
  }
  PaneTabLayout pane_tab_layout_for_test(int pane_index) const
  {
    return build_pane_tab_layout(pane_index);
  }
  bool pane_tab_mouse_for_test(
      int x, int y, bool click, bool release = false, bool middle = false, bool motion = false)
  {
    return handle_pane_tab_mouse(x, y, click, release, middle, motion);
  }
  bool pane_tab_wheel_for_test(int x, int y, bool up, bool down)
  {
    return handle_pane_tab_scroll(x, y, up, down);
  }
  bool pane_tab_drag_active_for_test() const
  {
    return pane_tab_drag_active;
  }
  int pane_tab_drop_kind_for_test() const
  {
    return (int)pane_tab_drop.kind;
  }
  int pane_tab_drop_pane_for_test() const
  {
    return pane_tab_drop.pane;
  }
  int pane_tab_drop_slot_for_test() const
  {
    return pane_tab_drop.slot;
  }
  int pane_tab_drop_edge_for_test() const
  {
    return pane_tab_drop.edge;
  }
  bool pane_tab_drop_on_strip_for_test() const
  {
    return pane_tab_drop.on_strip;
  }
  int pane_tab_scroll_index_for_test(int pane_index) const
  {
    if (pane_index < 0 || pane_index >= (int)panes.size())
    {
      return -1;
    }
    return panes[(size_t)pane_index].tab_scroll_index;
  }
  // The breadcrumb winbar (test_winbar.cpp): the row the active pane spends on
  // it, the chain it shows, and the menu a crumb opens. The mouse and key entry
  // points are the ones the real dispatcher calls, so a case can press a crumb
  // and pick a row without a pty.
  Winbar::WinbarLayout winbar_layout_for_test();
  bool pane_has_winbar_for_test() const
  {
    return !panes.empty() && pane_has_winbar(panes[(size_t)std::clamp(current_pane, 0, (int)panes.size() - 1)]);
  }
  void set_winbar_mode_for_test(Winbar::WinbarMode mode)
  {
    winbar_mode = mode;
    needs_redraw = true;
  }
  bool winbar_press_for_test(int x, int y)
  {
    return handle_winbar_mouse(x, y, true, false, false);
  }
  bool winbar_move_for_test(int x, int y)
  {
    return handle_winbar_mouse(x, y, false, false, true);
  }
  bool winbar_menu_key_for_test(int ch)
  {
    return handle_winbar_menu_input(ch);
  }
  bool winbar_menu_open_for_test() const
  {
    return winbar_menu_open();
  }
  // The cascade: one entry per open level, level 0 first. The per-level getters
  // default to the crumb's own menu and take a level index for the folders.
  int winbar_menu_level_count_for_test() const
  {
    return (int)winbar_menus.size();
  }
  int winbar_menu_selected_for_test(int level = 0) const
  {
    return winbar_menu_level(level).selected;
  }
  int winbar_menu_x_for_test(int level = 0) const
  {
    return winbar_menu_level(level).x;
  }
  int winbar_menu_y_for_test(int level = 0) const
  {
    return winbar_menu_level(level).y;
  }
  int winbar_menu_w_for_test(int level = 0) const
  {
    return winbar_menu_level(level).w;
  }
  int winbar_menu_h_for_test(int level = 0) const
  {
    return winbar_menu_level(level).h;
  }
  std::string winbar_menu_title_for_test(int level = 0) const
  {
    return winbar_menu_level(level).title;
  }
  std::vector<std::string> winbar_menu_labels_for_test(int level = 0) const
  {
    std::vector<std::string> out;
    for (const Winbar::Entry &entry : winbar_menu_level(level).entries)
    {
      out.push_back(entry.label);
    }
    return out;
  }
  std::vector<bool> winbar_menu_dirs_for_test(int level = 0) const
  {
    std::vector<bool> out;
    for (const Winbar::Entry &entry : winbar_menu_level(level).entries)
    {
      out.push_back(entry.is_dir);
    }
    return out;
  }
  // The private helper the getters read through: an out-of-range level reads as
  // an empty one, so a closed menu can never index past the cascade.
  const WinbarMenuLevel &winbar_menu_level(int level) const
  {
    static const WinbarMenuLevel empty;
    if (level < 0 || level >= (int)winbar_menus.size())
    {
      return empty;
    }
    return winbar_menus[(size_t)level];
  }
  int lua_float_count_for_test(const std::string &surface) const;
  void load_file(const std::string &fname);
  void run();
  // Reloads configuration from disk (settings.conf overlay + config.lua) and
  // live-applies every setting. Backing of the `:reload` command.
  void reload_config();
  EditorHostAPI &host();
  const EditorHostAPI &host() const;

