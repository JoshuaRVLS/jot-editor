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
  // motions and releases through the real private handlers.
  bool terminal_mouse_for_test(int x, int y, bool click, bool motion, bool release)
  {
    return handle_integrated_terminal_mouse(x, y, click, motion, release);
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
  int lua_float_count_for_test(const std::string &surface) const;
  void load_file(const std::string &fname);
  void run();
  // Reloads configuration from disk (settings.conf overlay + config.lua) and
  // live-applies every setting. Backing of the `:reload` command.
  void reload_config();
  EditorHostAPI &host();
  const EditorHostAPI &host() const;

