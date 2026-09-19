// ---------------------------------------------------------------------------
// Editing: caches, input dispatch, edit commands
// ---------------------------------------------------------------------------
//
// The syntax and decoration caches the renderer reads, the input dispatchers
// (keys, mouse, modes), the editing commands, and the small surfaces they own:
// the popup, the context menu and the menu bar.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  std::string get_buffer_text(const FileBuffer &buf) const;
  // Anchored decorations (see core/app/decorations.cpp): rebase_begin runs
  // before every edit (snapshot + mark dirty), ensure_* lazily shifts the
  // decorations through the pending edit window once, on the next render.
  void decoration_rebase_begin(FileBuffer &buf);
  void ensure_decorations_anchored(FileBuffer &buf);
  // Resolves a theme group name ("DiagnosticError", "@keyword", "status")
  // to the theme's current fg/bg; returns false when unknown.
  bool theme_group_color(const std::string &name, int &fg, int &bg) const;
  const std::vector<std::pair<int, int>> &
  get_line_syntax_colors(FileBuffer &buf, int line_idx, int byte_limit = 0x7fffffff);
  void invalidate_syntax_cache(FileBuffer &buf);
  // Absolute token-aware bracket depth at the start of `line` (see
  // render/buffer.cpp). Uses the same skip rule as the visible-row walk, so
  // rainbow bracket colors depend only on file position.
  int bracket_depth_at_line_start(FileBuffer &buf, int line);

#ifdef JOT_TREESITTER
  void reparse_tree(FileBuffer &buf);
  void init_ts_for_buffer(FileBuffer &buf);
  // Memoizes its filesystem/content probe on the buffer (see syntax.cpp), so
  // the buffer reference is mutable.
  std::string tree_sitter_extension_for_buffer(FileBuffer &buf);
  // Called just before a text mutation while the tree-sitter tree is in sync:
  // snapshots the current text so the next rebuild can reparse incrementally.
  void ts_begin_edit(FileBuffer &buf);
#ifdef JOT_TREESITTER
  // True when the buffer has a usable syntax tree (parsing it if it is small
  // enough to do synchronously); reports why not when it does not.
  bool syntax_tree_ready(FileBuffer &buf);
  bool select_byte_range(uint32_t start, uint32_t end);
#endif
  // Drains finished background parses and installs them into their buffers
  // (main thread only; see init_ts_for_buffer for the queue side).
  void install_finished_parses();
  int buffer_index_of(const FileBuffer &buf) const;
#endif

  void handle_input(int ch,
                    bool is_ctrl = false,
                    bool is_shift = false,
                    bool is_alt = false,
                    int original_ch = 0);
  void handle_mouse_input(int x,
                          int y,
                          bool is_click,
                          bool is_scroll_up,
                          bool is_scroll_down,
                          bool is_scroll_left = false,
                          bool is_scroll_right = false);

  void handle_modeless_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  void handle_command_palette(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  // Mouse for the command palette / quick pick / search panel: hover selects
  // a row (follow-window keeps the list put), click activates it, wheel
  // moves the selection. Returns true when the event was consumed.
  bool handle_palette_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
  bool handle_quick_pick_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
  bool execute_ex_command(const std::string &line);
  bool
  execute_ex_command_tail(const std::string &lcmd, const std::string &arg, const std::string &line);
  void show_command_help(const std::string &topic);
  void submit_command_palette();
  void handle_telescope(int ch);
  void handle_save_prompt(int ch);
  void handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_telescope_mouse(
      int x,
      int y,
      bool is_click,
      bool is_double_click,
      bool is_scroll_up,
      bool is_scroll_down,
      bool is_motion = false);
  bool handle_home_menu_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_home_menu_mouse(int x, int y, bool is_click);
  // Applies one settings-menu change through the normal config pipeline:
  // config.set + apply_config_live() + save, plus GUI font-size handling.
  void apply_settings_value(const std::string &key, const std::string &value);
  bool handle_menu_bar_input(int ch);
  bool handle_menu_bar_mouse(int x, int y, bool is_click, bool is_motion);
  bool handle_integrated_terminal_mouse(
      int x, int y, bool is_click, bool is_motion, bool is_click_release);
  bool handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
  // Mouse selection in the integrated terminal: anchors live in full-space
  // row/col coordinates (see IntegratedTerminal::get_total_rows) so they
  // survive scrolls and redraws.
  void begin_terminal_selection(int x, int y);
  void update_terminal_selection_pos(int x, int y);
  void finish_terminal_selection();
  std::string terminal_selection_text();
  bool handle_debugger_mouse(
      int x, int y, bool activate = true, bool wheel_up = false, bool wheel_down = false);
  // Debugger panel navigation: scrolls the output history, cycles the active
  // thread / frame of the current session. No-ops with a status message when
  // there is no stopped session to act on.
  void debugger_scroll_output(int delta_lines);
  void debugger_cycle_thread(int delta);
  void debugger_cycle_frame(int delta);
  // Opens the frame's source at the paused location (used by the stack-trace
  // handler and frame navigation).
  void jump_to_debugger_frame(const DebuggerFrame &frame);
  void place_integrated_terminal_cursor();
  void handle_mouse(void *event);

  void move_cursor(int dx, int dy, bool extend_selection = false);
  bool insert_char(char c);
  void insert_string(const std::string &str);
  void delete_char(bool forward = true);
  // Deletes at every caret: the main cursor (point when no primary
  // selection), every active extra-caret span, and every inactive point
  // caret. Backs the delete/backspace keys while multi-cursor is active.
  bool delete_at_all_carets(bool forward);
  // Restarts the blink clock (cursor and carets show immediately) and
  // suspends blinking for a short window, so the cursor stays solid right
  // after any input instead of blinking mid-keystroke.
  void restart_blink();
  void delete_word_backward();
  void delete_word_forward();
  void delete_selection();
  void delete_line();

  void new_line();
  void insert_line_below();
  void insert_line_above();
  void duplicate_line();
  void move_line_up();
  void move_line_down();
  void indent_selection();
  void outdent_selection();
  void toggle_comment();

  void show_popup(const std::string &text, const std::string &title = "");
  void show_hover_popup(const std::string &text, int x, int y);
  void hide_popup();
  bool handle_popup_input(int ch);
  void open_context_menu(int x,
                         int y,
                         ContextMenuSurface surface,
                         const std::vector<ContextMenuItem> &items);
  void close_context_menu();
  bool handle_context_menu_input(int ch);
  bool handle_context_menu_mouse(int x, int y, bool is_click);
  bool open_context_menu_for_mouse(int x, int y);
  void execute_context_menu_item(int index);
  std::vector<MenuBarMenu> build_menu_bar_model() const;
  void close_menu_bar();
  void open_menu_bar(int index);
  void execute_menu_bar_item(int menu_index, int item_index);
  void set_diagnostics(const std::string &filepath, const std::vector<Diagnostic> &diagnostics);
  void add_diagnostic(const std::string &filepath, const Diagnostic &diagnostic);
