// ---------------------------------------------------------------------------
// Docked panels
// ---------------------------------------------------------------------------
//
// The minimap, the bottom dock (integrated terminal + Problems), terminal
// tasks, the debugger panel and sessions, the command palette commands and the
// command dispatcher.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  void toggle_minimap();
  void toggle_integrated_terminal();
  // Ctrl+J: show/hide the whole panel, whatever view it hosts.
  void toggle_bottom_panel();
  // Ctrl+Shift+M: reveal the panel on its Problems view.
  void show_problems_panel();
  // The Problems view's own input: the panel is a dock, so its keys are read
  // here when it owns focus rather than being forwarded to a shell.
  bool handle_bottom_panel_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_bottom_panel_mouse(int x, int y, bool is_click);
  bool handle_problems_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
  // Opens the file behind a Problems row at its location, through the same
  // accept path the workspace-diagnostics picker uses.
  void jump_to_problem(int index);
  void create_integrated_terminal();
  void create_integrated_terminal(const std::string &label, const std::string &cwd = "");
  // Terminal panel geometry. Zoomed, the terminal owns the whole pane area
  // (below the pane tab strip, above the status line, full width);
  // otherwise it is the bottom panel whose height is drag-adjustable.
  int integrated_terminal_panel_y() const;
  int integrated_terminal_panel_h() const;
  int integrated_terminal_panel_w() const;
  // Height the terminal reserves from the pane area (0 while zoomed).
  int integrated_terminal_reserved_h() const;
  void toggle_terminal_zoom();
  bool begin_terminal_resize_drag(int x, int y);
  bool update_terminal_resize_drag(int y);
  void end_terminal_resize_drag();
  void close_integrated_terminal(int index);
  void activate_integrated_terminal(int index, bool focus = true);
  void load_terminal_tasks();
  std::vector<std::string> list_terminal_task_names();
  void show_terminal_tasks();
  bool run_terminal_task(const std::string &name, bool force_new = false);
  bool rerun_last_terminal_task();
  void toggle_debugger_panel();
  bool start_debugger_session(DebuggerSessionConfig config);
  bool start_debugger_command(const std::string &adapter, const std::string &command_line);
  bool attach_debugger_command(const std::string &adapter, const std::string &pid_text);
  void stop_debugger_session();
  void restart_debugger_session();
  void continue_debugger_session();
  void pause_debugger_session();
  void step_debugger_in();
  void step_debugger_next();
  void step_debugger_out();
  void show_debugger_threads();
  void request_debugger_memory(const std::string &expression, int bytes = 128);
  void request_debugger_disassembly(const std::string &expression = "");
  bool toggle_debugger_breakpoint(const std::string &filepath, int line);
  bool has_debugger_breakpoint(const std::string &filepath, int line) const;
  void update_debugger_breakpoint_hover(int pane_index, int buffer_id, int line);
  void clear_debugger_breakpoint_hover();
  bool is_debugger_breakpoint_hover(int buffer_id, int line) const;
  void load_debugger_configs();
  std::vector<std::string> list_debugger_config_names();
  bool run_debugger_config(const std::string &name);
  DebuggerClient *get_debugger_session(int index = -1);
  void toggle_command_palette();
  void open_command_palette(const std::string &query);
  void open_theme_chooser();
  void execute_command(const std::string &cmd);

  // Root used when launching the telescope file finder: the workspace root
  // when one is open, otherwise the git/project root detected from the
  // current file's directory (falling back to the process cwd).
