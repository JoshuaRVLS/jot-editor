// ---------------------------------------------------------------------------
// Buffers, files and git
// ---------------------------------------------------------------------------
//
// Clipboard and undo, buffer lifecycle (open, close, save, autosave), recent
// files and workspaces, workspace session persistence, the git status refresh
// and the git panel commands.
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  void copy();
  void cut();
  void paste();

  void save_state();
  void undo();
  void redo();

  void clamp_cursor(int buffer_id);
  void move_word_forward(bool extend_selection = false);
  void move_word_backward(bool extend_selection = false);
  void move_to_line_smart_start(bool extend_selection = false);
  void move_to_line_start(bool extend_selection = false);
  void move_to_line_end(bool extend_selection = false);
  void move_to_file_start(bool extend_selection = false);
  void move_to_file_end(bool extend_selection = false);
  void ensure_cursor_visible(bool adjust_horizontal = true);
  void select_all();
  void select_current_line();
  void clear_selection();

  void open_file(const std::string &path, bool preview = false);
  void finish_open_file(FileBuffer fb, const std::string &path_to_open, bool preview);
  void open_recent_file(const std::string &query = "");
  void reopen_last_closed_buffer();
  void close_buffer_at(int index);
  void close_buffer();
  void create_new_buffer();
  void save_file();
  bool save_buffer_at(int index, bool announce = true);
  void save_file_as();
  void auto_save_modified_buffers();
  void set_auto_save(bool enabled, bool persist = true);
  void set_auto_save_interval(int interval_ms, bool persist = true);
  void track_recent_file(const std::string &path);
  void track_recent_workspace(const std::string &path);
  void load_recent_files();
  void load_recent_workspaces();
  void save_recent_files();
  void save_recent_workspaces();
  void save_file_fold_state(FileBuffer &buf);
  void save_file_fold_states();
  void restore_file_fold_state(FileBuffer &buf);
  void save_workspace_session();
  bool restore_workspace_session();
  void refresh_git_status(bool force = false);
  void clear_git_status();
  bool has_git_repo() const;
  bool git_status_active() const
  {
    return !git_root.empty() || !git_branch.empty() || git_dirty_count != 0 || git_staged_count != 0
           || git_unstaged_count != 0 || git_untracked_count != 0 || git_deleted_count != 0
           || git_renamed_count != 0 || git_conflict_count != 0 || !git_file_status.empty()
           || (workspace_session_enabled && !workspace_session_root.empty()) || !root_dir.empty();
  }
  std::string run_git_capture(const std::string &args) const;
  std::string to_git_relative_path(const std::string &path) const;
  bool open_git_diff_panel(const std::string &path, bool staged);
  void close_git_diff_panel();
  void scroll_git_diff_panel(int delta);
  bool git_stage_path(const std::string &path);
  bool git_unstage_path(const std::string &path);
  bool git_stage_all();
  bool git_unstage_all();
  // Empty on success, otherwise git's error (last line of stderr).
  std::string git_commit_message(const std::string &message);
  // Opens the lazygit TUI in an integrated terminal tab rooted at the
  // workspace (or the current file's directory). Reuses an existing lazygit
  // tab when one is still running.
  void open_git_client();
  bool handle_right_panel_tab_strip_mouse(int x, int y, bool is_click);
  void build_right_panel_tab_strip_view(SidePanelView &view) const;
  void render_right_panel_tab_strip(int panel_x, int panel_y, int panel_w);
  // Git panel (right dock): lazygit-style files / branches / commits / stash
  // views driven by the jot_git_panel::State model (see git_panel_models.h).
  void toggle_git_panel();
  void git_panel_refresh();
  void git_panel_switch_view(int view_number);
  void git_panel_move_selection(int delta);
  void git_panel_page(int delta);
  void git_panel_jump_to_end(bool bottom);
  void git_panel_primary(); // space: stage/unstage, checkout, apply stash
  void git_panel_stage_all();
  void git_panel_unstage_all();
  void git_panel_open_diff_selected();
  void git_panel_commit_prompt();
  void git_panel_discard_or_delete(); // d: discard / delete branch / drop stash
  void git_panel_stash_push();
  void git_panel_stash_pop();
  void git_panel_new_branch_prompt();
  void git_panel_merge_prompt();
  void git_panel_fetch();
  void git_panel_push();
  void git_panel_pull();
  void git_panel_copy();
  bool handle_git_panel_mouse(int x, int y, bool is_click, bool is_double_click);
  void set_clipboard_text(const std::string &text);

