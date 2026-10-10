// ---------------------------------------------------------------------------
// Editing: caches, input dispatch, edit commands
// ---------------------------------------------------------------------------
//
// The syntax and decoration caches the renderer reads, the input dispatchers
// (keys, mouse, modes), the editing commands, and the small surfaces they own:
// the popup, the context menu and the menu bar.
//
// A fragment of the Editor class body, included by src/core/editor.h. It is
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
// Drops the parse trees of buffers the renderer has not drawn recently,
// keeping the newest kMaxCachedSyntaxTrees of them (see syntax.cpp). A buffer
// shown again re-parses on demand, through the same lazy path every large
// file already takes on open, so the CST of a file left in the background
// does not sit in memory for the rest of the session. Called once per painted
// frame, after the panes have stamped the buffers they drew.
void evict_stale_syntax_trees();
// Recency counter behind FileBuffer::ts_last_use_seq, bumped once per drawn
// buffer; syntax_sweep_seq_ is where the previous sweep left it, so the
// sweep can tell what was on screen since it last ran.
std::uint64_t ts_use_seq_ = 0;
std::uint64_t syntax_sweep_seq_ = 0;
#endif

void handle_input(
    int ch, bool is_ctrl = false, bool is_shift = false, bool is_alt = false, int original_ch = 0);
void handle_mouse_input(int x,
                        int y,
                        bool is_click,
                        bool is_scroll_up,
                        bool is_scroll_down,
                        bool is_scroll_left = false,
                        bool is_scroll_right = false);

void handle_modeless_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);

void handle_command_palette(int ch, bool is_ctrl, bool is_shift, bool is_alt);
// Moves the palette's selection by grid steps: +1 row is the next item down
// in the same column, +1 column the same row in the next column. The grid
// geometry decides where the column ends, so a step never lands on a cell the
// strip did not paint.
void move_command_palette_selection(int delta_rows, int delta_cols);
// Mouse for the command palette / quick pick / search panel: hover selects
// a row (follow-window keeps the list put), click activates it, wheel
// moves the selection. Returns true when the event was consumed.
bool handle_palette_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
bool handle_quick_pick_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
bool execute_ex_command(const std::string &line);
bool execute_ex_command_tail(const std::string &lcmd,
                             const std::string &arg,
                             const std::string &line);
void show_command_help(const std::string &topic);
void submit_command_palette();
void handle_telescope(int ch);
void handle_save_prompt(int ch);
void handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
bool handle_telescope_mouse(int x,
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
    int x, int y, bool is_click, bool is_motion, bool is_click_release, bool ctrl = false);
bool handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
void handle_floating_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
bool handle_floating_terminal_mouse(
    int x, int y, bool is_click, bool is_motion, bool is_click_release, bool ctrl = false);
bool handle_floating_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
void begin_floating_terminal_selection(int x, int y);
// One terminal view's rows, drawn from the vterm: the trimming, the
// scrollback window and the selection band live here so the docked panel and
// the floating box cannot drift apart.
void render_terminal_rows(const TerminalView &view, int term_fg, int term_bg);
// Mouse selection in the integrated terminal: anchors live in full-space
// row/col coordinates (see IntegratedTerminal::get_total_rows) so they
// survive scrolls and redraws.
void begin_terminal_selection(int x, int y);
void begin_terminal_selection_in(const TerminalView &view, int x, int y);
// The view the live selection belongs to (the anchors are shared state).
TerminalView terminal_selection_view();
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
void place_floating_terminal_cursor();
void handle_mouse(void *event);

void move_cursor(int dx, int dy, bool extend_selection = false);
// The read-only gate in front of every edit path: true for an ordinary
// buffer, false for a rendered view the user must not type into. A refusal
// says so on the message line (transiently, so holding a key does not stack
// toasts) and the caller leaves the text alone.
bool edit_allowed();
bool insert_char(char c);
// Carries the partner of the markup tag name the cursor sits in along with the
// edit that just landed, so an opening tag and its closing tag rename together
// (see HtmlFeatures::find_tag_pair). A no-op outside markup buffers, and
// whenever the cursor is not inside a tag name.
void sync_markup_tag_name();
// Expands the Emmet abbreviation ending at the cursor (features/emmet.*) and
// starts a snippet session on it, so `div.box>p` becomes markup with the caret
// inside it and `m10-20` becomes a declaration. Returns false when there is
// nothing to expand -- the `emmet` setting is off, the file is not markup or a
// style sheet, or the token does not parse -- which is what lets a Tab fall
// through to indentation.
bool expand_emmet_abbreviation();
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
// Restarts the blink clock for a pointer move. The caret shows at its new
// home and then keeps the ordinary rhythm: a click is not a typing burst, so
// there is nothing to stay solid through, and the typing hold made the blink
// look like it stopped instead of restarting.
void restart_blink_pointer();
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
