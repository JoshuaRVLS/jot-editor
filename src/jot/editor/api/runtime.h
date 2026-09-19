// ---------------------------------------------------------------------------
// Runtime services: timers, pollers, LSP lifecycle
// ---------------------------------------------------------------------------
//
// The smooth-scroll animation, the polls the main loop drives (LSP clients and
// installs, debugger sessions, tree-sitter installs, file-tree changes), the fd
// watches, and the LSP client lifecycle (attach, notify, restart, stop).
//
// A fragment of the Editor class body, included by src/jot/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
  // --- Smooth scrolling (features/smooth_scroll.h: neoscroll.nvim's model) ---
  // A viewport-only scroll animates scroll_offset toward its destination over
  // a few frames instead of jumping there. One animation at a time: it belongs
  // to the pane the wheel was over, and it is dropped the moment anything else
  // moves that viewport (see advance_smooth_scroll).
  struct SmoothScrollAnim
  {
    bool active = false;
    int buffer_id = -1;  // the pane's buffer when the animation started
    int from = 0;        // offset the animation started from
    int applied = -1;    // offset this animation wrote last (the drift probe)
    int notch_lines = 1; // one wheel step: the duration's reference distance
    SmoothScroll::Easing easing = SmoothScroll::Easing::Linear;
    SmoothScroll::InFlight in_flight;
    long long start_ms = 0;
    long long duration_ms = 0;
  };
  SmoothScrollAnim smooth_scroll_;
  // Off until the config says otherwise (see Config::load_defaults).
  bool smooth_scroll_enabled_ = false;
  SmoothScroll::Easing smooth_scroll_easing_ = SmoothScroll::Easing::Linear;
  double smooth_scroll_duration_multiplier_ = 1.0;
  // Animates a viewport-only scroll of `lines` visible lines (negative scrolls
  // up) over `base_ms`, merging with an animation already in flight. Returns
  // true when the viewport moved or is about to. Jumps immediately when smooth
  // scrolling is off, and never animates in GUI mode (the GUI frontend already
  // eases the content shift pixel by pixel).
  bool scroll_view_smooth(int lines, int base_ms);
  // Advances the running animation to `now_ms`; true when it changed the
  // viewport, so the caller has to repaint.
  bool advance_smooth_scroll(long long now_ms);
  void cancel_smooth_scroll();
  void poll_lsp_clients();
  // Marks the per-file inlay-hint cache stale (after a did_change flush).
  void mark_lsp_inlay_hints_dirty(const std::string &filepath);
  // Stores a fresh inlay-hint answer for its file.
  void handle_lsp_inlay_hints_result(const LSPInlayHintResult &result);
  // Requests inlay hints for the current buffer's visible range when the
  // cache is stale or scrolled past; called from poll_lsp_clients.
  void refresh_lsp_inlay_hints_if_needed();
  void poll_lsp_installs();
  void poll_debugger_sessions();
  void watch_lsp_client_fds(LSPClient *client);
  void unwatch_lsp_client_fds(LSPClient *client);
  void watch_debugger_client_fds(DebuggerClient *client);
  void unwatch_debugger_client_fds(DebuggerClient *client);
  void watch_integrated_terminal_fd(IntegratedTerminal *term);
  void unwatch_integrated_terminal_fd(IntegratedTerminal *term);
  void arm_file_tree_watch();
  bool lsp_work_pending() const
  {
    return !lsp_pending_changes.empty() || !lsp_clients.empty();
  }
  // Applies one new grid size: re-dimensions the UI (which schedules a single
  // full repaint), re-fits the panes and notifies Lua. Resize bursts are
  // coalesced before this is called (see the input drains), so a live drag does
  // one relayout per wake instead of one per compositor step.
  void apply_resize(int cols, int rows);
  LSPClient *find_lsp_client(const std::string &language, const std::string &root_path);

  void handle_terminal_event(const Event &ev);
  void render_frame();
  // GUI frontend event pump: drains SDL events (translated to the same
  // Event convention as the terminal) and renders one frame. Backs the
  // 4ms repeating timer in run() while gui_mode is set.
  void pump_gui_events();
  // Starts (or reuses) a single client process for one server id at a root;
  // shared by the primary attach and the extra policy servers.
  LSPClient *ensure_lsp_client_process(const std::string &server,
                                       const std::string &root_path,
                                       const std::vector<std::string> &command,
                                       const std::vector<std::string> &library_dirs,
                                       const std::string &initialization_options = {});
  // Installs a server the package vendors (share/jot/payload/<bin>) the first
  // time a buffer needs it, at most once per binary per session. The install
  // job attaches the waiting buffers when it lands.
  void auto_install_bundled_lsp(const std::string &bin);
  // All live clients that should receive document notifications for a file:
  // the primary server plus policy extras attached at the same workspace
  // root. Root is returned for callers that need it.
  std::vector<LSPClient *> attached_lsp_clients_for(const std::string &filepath,
                                                    std::string *root_out,
                                                    std::string *primary_out);
  // Merges the per-server diagnostic slices for one file into the buffer.
  void refresh_lsp_diagnostics_for(const std::string &filepath);
  // Drops one server's slices and refreshes the affected files (client died,
  // server removed, …).
  void drop_lsp_diagnostics_for_client(const std::string &server, const std::string &root);
  // Routes :format through the attached LSP server (textDocument/formatting)
  // when one is ready; returns false so the caller falls back to re-indent.
  bool lsp_format_active_buffer();
  // Applies server text edits (format results) to the buffer in place.
  void apply_lsp_text_edits(const std::string &filepath, const std::vector<LSPTextEdit> &edits);
  LSPClient *ensure_lsp_for_file(const std::string &filepath);
  void notify_lsp_open(const std::string &filepath);
  // Attaches any already-open buffers whose language matches `language` to a
  // freshly available server (e.g. right after an install completes), so a
  // file that was open before the server existed does not wait for a reopen.
  void heal_lsp_attach_for(const std::string &language);
  void notify_lsp_change(const std::string &filepath);
  void notify_lsp_save(const std::string &filepath);
  void notify_lsp_close(const std::string &filepath);
  void stop_all_lsp_clients();
  void restart_all_lsp_clients();
  // Replaces this process with a fresh jot so a freshly rebuilt binary loads
  // immediately (used by the Lua :update run flow). Refuses while any buffer
  // has unsaved changes unless `force`. Returns whether a restart started.
  bool restart_editor(bool force = false);
  void set_lsp_server_enabled(const std::string &server, bool enabled);
  bool install_lsp_server(const std::string &name);
  bool remove_lsp_server(const std::string &name);
  // One-shot toolkit presets from the Lua policy (currently "web": the
  // typescript/html/css/json servers plus their tree-sitter parsers).
  void install_web_toolchain();
  // Registry-owned "id1|id2|..." list for usage messages / completions.
  std::string lsp_install_usage_hint() const;
  bool install_tree_sitter_language(const std::string &language);
  void show_tree_sitter_status();
  void reload_tree_sitter();
  void poll_tree_sitter_installs();
  bool handle_tree_sitter_status_input(int ch);
  void show_lsp_status();
  void open_lsp_status_modal();
  void render_lsp_status_modal();
  bool handle_lsp_status_input(int ch);
  // Live diagnostic totals for one attached language, summed over open buffers
  // (severity 1=Error 2=Warning 3=Info 4=Hint).
  void lsp_server_diagnostic_counts(
      const std::string &language, int *errors, int *warnings, int *infos, int *hints) const;
