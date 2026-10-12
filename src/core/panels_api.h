// ---------------------------------------------------------------------------
// Docked panels
// ---------------------------------------------------------------------------
//
// The minimap, the bottom dock (integrated terminal + Problems), terminal
// tasks, the compile session and the status-line prompt that starts it, the
// debugger panel and sessions, the command palette commands and the command
// dispatcher.
//
// A fragment of the Editor class body, included by src/core/editor.h. It is
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
// Text scale of the terminal frontend, 1 (the classic one character per cell)
// to kMaxTuiTextScale. A terminal program cannot change its own font -- the
// emulator owns the glyphs -- so asking for a bigger grid means asking the
// terminal to draw each cell as a scale x scale block, which is what kitty's
// text sizing protocol provides (see Terminal::write_scaled_text). A terminal
// without it keeps the screen at 1:1 and says so.
void set_tui_text_scale(int scale, bool persist = true);
// Ctrl/Alt +/= and -/_: one step up or down, clamped at the ends.
void tui_text_zoom(int step);
bool begin_terminal_resize_drag(int x, int y);
bool update_terminal_resize_drag(int y);
void end_terminal_resize_drag();
void close_integrated_terminal(int index);
void activate_integrated_terminal(int index, bool focus = true);
// The floating terminal: its own shell in a box over the active pane
// (src/terminal/terminal_float.cpp). Esc hides it without ending the
// shell, so the next toggle returns to the same prompt.
void toggle_floating_terminal();
void open_floating_terminal();
void hide_floating_terminal(const std::string &message = "");
// The rectangle each terminal view draws in and reads its mouse positions
// from: the docked panel's content rows, or the floating box.
TerminalView docked_terminal_view();
TerminalView floating_terminal_view() const;
// The URL under a screen cell in a terminal view, detected with the editor's
// own rules (src/base/url_span.h) on the row's text; the out-params take
// the half-open cell range it covers ("" when the cell is not on a link).
std::string terminal_link_at(const TerminalView &view, int x, int y, int &start_col, int &end_col);
// Ctrl+click on a link: opens it and reports it, exactly as the editor path
// does. Returns true when a link was there (the click is the link's).
bool open_terminal_link_at(const TerminalView &view, int x, int y);
// Ctrl+hover over a terminal link, driven from the mouse dispatcher for
// every event: arms the underline over the URL the click would open, and
// drops it when the pointer leaves the link, the view or Ctrl.
void update_terminal_link_hover(int x, int y);
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
// --- The status-line prompt (the minibuffer) --------------------------------
// Opens the prompt on the editor's last row and gives it the keyboard. The
// prefill is what the user is most likely to want again: the last compile
// command, the path typed in this session, the query of the running search.
void open_minibuffer(MinibufferKind kind, const std::string &label, const std::string &prefill = "");
void close_minibuffer();
bool handle_minibuffer_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
// Enter: whatever the prompt was opened for.
void submit_minibuffer();
// Tab: complete a path (find file) or toggle the pattern flag (isearch).
void minibuffer_complete();
void render_minibuffer();
void place_minibuffer_cursor();
void open_compile_prompt();
void open_isearch_prompt();
void open_find_file_prompt();
void open_ex_prompt();
// I-search: runs the query against the buffer and moves the caret to the match
// `direction` finds from where the last one landed. `first` is the keystroke
// that extended the query, which searches from the origin rather than from the
// match the last keystroke found (Emacs' incremental behaviour).
void isearch_step(int direction, bool from_origin);
void isearch_cancel();

// --- The compile session ----------------------------------------------------
// Runs a shell command in the project root with its output streaming into a
// `[Compile]` buffer shown in a split below the pane that asked for it. The
// locations the output names are parsed as it arrives, so `:cnext` / `:cprev`
// (and Enter on a row) can jump while the build is still running.
void compile_run(const std::string &command);
void compile_stop();
void poll_compile_job();
// The build command with no argument: the last one, else the `build_command`
// setting, else nothing (and the prompt opens instead).
std::string default_compile_command();
// Reads what the job appended and lands it in the buffer, decorating the rows
// that name a problem as they arrive.
void compile_append_output(const std::string &chunk);
// The rows themselves, once the chunk is split into lines: the escapes are
// taken out, the colours in effect become spans the renderer paints, and the
// locations are read off the text that is left.
void compile_append_rows(const std::vector<std::string> &raw_lines);
// Not a line, but still output: whatever the job left with no final newline.
void compile_flush_pending();
void compile_finish(int status);
// Keeps the compilation pane at the end of the log while the build writes.
void compile_follow_tail();
bool compile_open_finding(int index);
bool compile_jump(int direction);
bool compile_jump_from_row(int row);
bool compile_buffer_focused() const;
int compile_finding_count() const;
// The `[Compile]` buffer, created on first use and reused from then on.
int compile_ensure_buffer();

void toggle_command_palette();
void open_command_palette(const std::string &query);
void open_theme_chooser();
void execute_command(const std::string &cmd);

// Root used when launching the telescope file finder: the workspace root
// when one is open, otherwise the git/project root detected from the
// current file's directory (falling back to the process cwd).
