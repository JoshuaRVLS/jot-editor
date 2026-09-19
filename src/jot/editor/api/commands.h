// ---------------------------------------------------------------------------
// Commands, themes and setup
// ---------------------------------------------------------------------------
//
// Bookmarks and text commands (case, sort, surround, increment, ...), the
// theme list/apply, the buffer accessors, construction and config reload, the
// GUI font plumbing and the message line.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  void toggle_bookmark();
  void next_bookmark();
  void prev_bookmark();

  void jump_to_matching_bracket();
  void select_current_function();
  void format_document();
  void trim_trailing_whitespace();
  void transform_selection_uppercase();
  void transform_selection_lowercase();
  void sort_selected_lines();
  void sort_selected_lines_desc();
  void reverse_selected_lines();
  void unique_selected_lines();
  void shuffle_selected_lines();
  void join_lines_selection_or_current();
  void duplicate_selection_or_line();
  void trim_blank_lines_in_selection();
  void copy_current_file_path();
  void copy_current_file_name();
  void insert_current_datetime();
  void show_buffer_stats();
  void replace_all_text(const std::string &needle,
                        const std::string &replacement,
                        bool case_sensitive = true,
                        bool whole_word = false);
  void replace_all_regex(const std::string &pattern, const std::string &replacement);
  bool surround_selection_or_word(const std::string &left, const std::string &right);
  bool unsurround_selection_or_cursor();
  bool change_inside_quote(char quote);
  void increment_number_at_cursor(int delta);
  void toggle_auto_indent_setting();
  void change_tab_size(int delta);
  std::vector<std::string> list_available_themes();
  // Applies a theme; when persist is set, also saves it to the config file so
  // it survives the next session. Returns true on success.
  bool apply_theme(const std::string &name, bool persist = true, bool announce = true);
  int detect_indent_width(const std::vector<std::string> &lines) const;

  FileBuffer &get_buffer(int id = -1);
  SplitPane &get_pane(int id = -1);
  std::string get_file_extension(const std::string &path);
  std::string get_filename(const std::string &path);
  Theme &get_theme()
  {
    return theme;
  }
  IntegratedTerminal *get_integrated_terminal(int index = -1);

  void load_runtime_config();
  void initialize_state_defaults();
  void initialize_lua_runtime();
  void initialize_terminal_ui();
  // GUI frontend (jot --gui): builds the SDL3/OpenGL UIGui and the initial
  // pane. Throws std::runtime_error when the display or font is missing.
  void initialize_gui_ui();
  void initialize_placeholder_buffer();
  // Re-reads every config key that maps to live editor state and applies it
  // immediately (no restart). Idempotent; called after Lua config/plugin load
  // and by reload_config().
  void apply_config_live();

  int create_pane(int x, int y, int w, int h, int buffer_id);
  void update_pane_layout();
  void split_pane_direction(int dx, int dy);
  void refresh_command_palette();
  // The GUI frontend, or nullptr when this build has no GUI (JOT_GUI=OFF) or the
  // terminal frontend is in use. Every GUI call goes through here so the
  // compile-time switch lives in one place: naming UIGui at all needs its vtable
  // and typeinfo, which only exist when the GUI sources are compiled in, and a
  // build without them would fail to link rather than simply find nullptr.
  UIGui *gui_ui();
  const UIGui *gui_ui() const;
  // Lists every installed fixed-width family and applies the chosen one.
  void open_font_picker();
  // Switches the GUI to `family` (empty = the built-in font), persists it and
  // reports the result. False means nothing changed: either this frontend has
  // no font to change, or the name matched no installed family. The reason is
  // left in the statusline either way, so callers need not explain it again.
  //
  // Kept here rather than letting callers reach into UIGui: the GUI header
  // needs the GL loader, which the input layer does not link.
  bool apply_gui_font_family(const std::string &family);
  // The family in use, empty when the built-in font is loaded.
  std::string gui_font_family_name() const;
  // Installed fixed-width families, sorted. Reads the font directories, so it
  // is a picker-time call, not a per-frame one.
  std::vector<std::string> gui_font_families() const;
  // The quiet surface for routine news ("Saved", "3 lines joined"): the status
  // line shows it and the transient timer clears it. A toast is opt-in per call
  // (`toast = true`) for news that genuinely wants attention — making it the
  // default put a toast over the editor for every ordinary action.
  void set_message(const std::string &msg, bool toast = false);
  void set_transient_message(const std::string &msg, int duration_ms = 5000, bool toast = false);
  bool close_active_floating_ui();

