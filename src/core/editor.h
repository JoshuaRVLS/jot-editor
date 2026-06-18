#ifndef EDITOR_H
#define EDITOR_H

#include "autoclose.h"
#include "bracket.h"
#include "editor_state.h"
#include "host_api.h"
#include "tools/lsp/client.h"
#include <string>
#include <utility>
#include <vector>

class PythonAPI;
class EditorHostAPI;
class HostCoreAPI;
class HostRenderAPI;
class HostIOAPI;

class Editor : private EditorState {
  friend class PythonAPI;
  friend class EditorHostAPI;
  friend class HostCoreAPI;
  friend class HostRenderAPI;
  friend class HostIOAPI;

private:
  using PaneTreeNode = ::PaneTreeNode;
  using CommandPaletteSuggestion = ::CommandPaletteSuggestion;
  using QuickPickKind = ::QuickPickKind;
  using QuickPickItem = ::QuickPickItem;
  using SearchMatch = ::SearchMatch;
  using MenuBarAction = ::MenuBarAction;
  using MenuBarItem = ::MenuBarItem;
  using MenuBarMenu = ::MenuBarMenu;
  using MenuBarSegment = ::MenuBarSegment;
  using RightPanelTab = ::RightPanelTab;
  using GitDiffPanel = ::GitDiffPanel;
  using DebuggerSessionState = ::DebuggerSessionState;
  using TerminalTask = ::TerminalTask;
  using TreeSitterInstallJob = ::TreeSitterInstallJob;
  using MouseSelectionMode = ::MouseSelectionMode;
  using ContextMenuSurface = ::ContextMenuSurface;
  using ContextMenuAction = ::ContextMenuAction;
  using ContextMenuItem = ::ContextMenuItem;
  using LSPJumpLocation = ::LSPJumpLocation;
  using ClosedBufferSnapshot = ::ClosedBufferSnapshot;
  using HomeMenuEntry = ::HomeMenuEntry;
  using SidebarView = ::SidebarView;
  using SidebarRenderRow = ::SidebarRenderRow;
  using SidebarRenderCache = ::SidebarRenderCache;
  using GitSidebarRow = ::GitSidebarRow;
  using FileTabSegment = ::FileTabSegment;
  using FileTabLayout = ::FileTabLayout;
  using EditorFocus = ::EditorFocus;

  static constexpr QuickPickKind QUICK_PICK_NONE = ::QUICK_PICK_NONE;
  static constexpr QuickPickKind QUICK_PICK_PROJECT_SEARCH =
      ::QUICK_PICK_PROJECT_SEARCH;
  static constexpr QuickPickKind QUICK_PICK_DIAGNOSTICS =
      ::QUICK_PICK_DIAGNOSTICS;
  static constexpr QuickPickKind QUICK_PICK_SYMBOLS = ::QUICK_PICK_SYMBOLS;
  static constexpr QuickPickKind QUICK_PICK_PLUGIN = ::QUICK_PICK_PLUGIN;

  static constexpr MenuBarAction MENU_ACTION_NONE = ::MENU_ACTION_NONE;
  static constexpr MenuBarAction MENU_ACTION_COMMAND = ::MENU_ACTION_COMMAND;
  static constexpr MenuBarAction MENU_ACTION_NEW_FILE = ::MENU_ACTION_NEW_FILE;
  static constexpr MenuBarAction MENU_ACTION_OPEN_FINDER =
      ::MENU_ACTION_OPEN_FINDER;
  static constexpr MenuBarAction MENU_ACTION_SAVE = ::MENU_ACTION_SAVE;
  static constexpr MenuBarAction MENU_ACTION_SAVE_AS = ::MENU_ACTION_SAVE_AS;
  static constexpr MenuBarAction MENU_ACTION_CLOSE_FILE =
      ::MENU_ACTION_CLOSE_FILE;
  static constexpr MenuBarAction MENU_ACTION_QUIT = ::MENU_ACTION_QUIT;
  static constexpr MenuBarAction MENU_ACTION_UNDO = ::MENU_ACTION_UNDO;
  static constexpr MenuBarAction MENU_ACTION_REDO = ::MENU_ACTION_REDO;
  static constexpr MenuBarAction MENU_ACTION_CUT = ::MENU_ACTION_CUT;
  static constexpr MenuBarAction MENU_ACTION_COPY = ::MENU_ACTION_COPY;
  static constexpr MenuBarAction MENU_ACTION_PASTE = ::MENU_ACTION_PASTE;
  static constexpr MenuBarAction MENU_ACTION_SELECT_ALL =
      ::MENU_ACTION_SELECT_ALL;
  static constexpr MenuBarAction MENU_ACTION_SELECT_LINE =
      ::MENU_ACTION_SELECT_LINE;
  static constexpr MenuBarAction MENU_ACTION_DUPLICATE_LINE =
      ::MENU_ACTION_DUPLICATE_LINE;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_UP =
      ::MENU_ACTION_MOVE_LINE_UP;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_DOWN =
      ::MENU_ACTION_MOVE_LINE_DOWN;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_COMMENT =
      ::MENU_ACTION_TOGGLE_COMMENT;
  static constexpr MenuBarAction MENU_ACTION_COMMAND_PALETTE =
      ::MENU_ACTION_COMMAND_PALETTE;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_SIDEBAR =
      ::MENU_ACTION_TOGGLE_SIDEBAR;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_MINIMAP =
      ::MENU_ACTION_TOGGLE_MINIMAP;
  static constexpr MenuBarAction MENU_ACTION_THEME = ::MENU_ACTION_THEME;
  static constexpr MenuBarAction MENU_ACTION_HOME = ::MENU_ACTION_HOME;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_TERMINAL =
      ::MENU_ACTION_TOGGLE_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_NEW_TERMINAL =
      ::MENU_ACTION_NEW_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_TASKS = ::MENU_ACTION_TASKS;
  static constexpr MenuBarAction MENU_ACTION_RERUN_TASK =
      ::MENU_ACTION_RERUN_TASK;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_DEBUG_PANEL =
      ::MENU_ACTION_TOGGLE_DEBUG_PANEL;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STOP =
      ::MENU_ACTION_DEBUG_STOP;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_CONTINUE =
      ::MENU_ACTION_DEBUG_CONTINUE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_PAUSE =
      ::MENU_ACTION_DEBUG_PAUSE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_IN =
      ::MENU_ACTION_DEBUG_STEP_IN;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OVER =
      ::MENU_ACTION_DEBUG_STEP_OVER;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OUT =
      ::MENU_ACTION_DEBUG_STEP_OUT;
  static constexpr MenuBarAction MENU_ACTION_LSP_DEFINITION =
      ::MENU_ACTION_LSP_DEFINITION;
  static constexpr MenuBarAction MENU_ACTION_LSP_BACK =
      ::MENU_ACTION_LSP_BACK;
  static constexpr MenuBarAction MENU_ACTION_HELP = ::MENU_ACTION_HELP;

  static constexpr RightPanelTab RIGHT_PANEL_DEBUG = ::RIGHT_PANEL_DEBUG;
  static constexpr RightPanelTab RIGHT_PANEL_GIT_DIFF = ::RIGHT_PANEL_GIT_DIFF;
  static constexpr RightPanelTab RIGHT_PANEL_PLUGIN = ::RIGHT_PANEL_PLUGIN;

  static constexpr MouseSelectionMode MOUSE_SELECT_CHAR = ::MOUSE_SELECT_CHAR;
  static constexpr MouseSelectionMode MOUSE_SELECT_WORD = ::MOUSE_SELECT_WORD;
  static constexpr MouseSelectionMode MOUSE_SELECT_LINE = ::MOUSE_SELECT_LINE;

  static constexpr ContextMenuSurface CONTEXT_MENU_NONE = ::CONTEXT_MENU_NONE;
  static constexpr ContextMenuSurface CONTEXT_MENU_EDITOR =
      ::CONTEXT_MENU_EDITOR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TAB = ::CONTEXT_MENU_TAB;
  static constexpr ContextMenuSurface CONTEXT_MENU_SIDEBAR =
      ::CONTEXT_MENU_SIDEBAR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TERMINAL =
      ::CONTEXT_MENU_TERMINAL;

  static constexpr ContextMenuAction CONTEXT_ACTION_NONE =
      ::CONTEXT_ACTION_NONE;
  static constexpr ContextMenuAction CONTEXT_ACTION_COPY =
      ::CONTEXT_ACTION_COPY;
  static constexpr ContextMenuAction CONTEXT_ACTION_CUT = ::CONTEXT_ACTION_CUT;
  static constexpr ContextMenuAction CONTEXT_ACTION_PASTE =
      ::CONTEXT_ACTION_PASTE;
  static constexpr ContextMenuAction CONTEXT_ACTION_SAVE_BUFFER =
      ::CONTEXT_ACTION_SAVE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_CLOSE_BUFFER =
      ::CONTEXT_ACTION_CLOSE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_OPEN =
      ::CONTEXT_ACTION_SIDEBAR_OPEN;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_NEW_FILE =
      ::CONTEXT_ACTION_SIDEBAR_NEW_FILE;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_NEW_FOLDER =
      ::CONTEXT_ACTION_SIDEBAR_NEW_FOLDER;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_RENAME =
      ::CONTEXT_ACTION_SIDEBAR_RENAME;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_REFRESH =
      ::CONTEXT_ACTION_SIDEBAR_REFRESH;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_COPY_PATH =
      ::CONTEXT_ACTION_SIDEBAR_COPY_PATH;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE =
      ::CONTEXT_ACTION_GIT_STAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_UNSTAGE =
      ::CONTEXT_ACTION_GIT_UNSTAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF =
      ::CONTEXT_ACTION_GIT_DIFF;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF_STAGED =
      ::CONTEXT_ACTION_GIT_DIFF_STAGED;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE_ALL =
      ::CONTEXT_ACTION_GIT_STAGE_ALL;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_REFRESH =
      ::CONTEXT_ACTION_GIT_REFRESH;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_FOCUS =
      ::CONTEXT_ACTION_TERMINAL_FOCUS;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_NEW =
      ::CONTEXT_ACTION_TERMINAL_NEW;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_CLOSE =
      ::CONTEXT_ACTION_TERMINAL_CLOSE;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_RESET_SCROLL =
      ::CONTEXT_ACTION_TERMINAL_RESET_SCROLL;
  static constexpr ContextMenuAction CONTEXT_ACTION_TOGGLE_FOLD =
      ::CONTEXT_ACTION_TOGGLE_FOLD;

  static constexpr SidebarView SIDEBAR_VIEW_EXPLORER = ::SIDEBAR_VIEW_EXPLORER;
  static constexpr SidebarView SIDEBAR_VIEW_GIT = ::SIDEBAR_VIEW_GIT;

  static constexpr EditorFocus FOCUS_EDITOR = ::FOCUS_EDITOR;
  static constexpr EditorFocus FOCUS_SIDEBAR = ::FOCUS_SIDEBAR;

  void render();
  void render_tabs();
  void render_panes();
  void render_pane_resize_guides();
  void render_easter_egg();
  void render_pane(const SplitPane &pane);
  FileTabLayout build_file_tab_layout(const SplitPane &pane, int draw_w);
  int find_local_tab_index(const SplitPane &pane, int buffer_id) const;
  void clamp_tab_scroll(SplitPane &pane);
  void reveal_local_tab(SplitPane &pane, int target_index, int draw_w);
  bool scroll_local_tabs(SplitPane &pane, int delta);
  bool switch_to_local_tab(int target_index);
  bool cycle_local_tab(int delta);
  void render_scrollbar(const SplitPane &pane, int draw_w);
  void render_telescope();
  void render_minimap(int x, int y, int w, int h, int buffer_id);
  void render_image_viewer();
  void render_integrated_terminal();
  void render_debugger_panel();
  void render_git_diff_panel();
  void render_plugin_panel();
  int effective_right_panel_width() const;
  void render_menu_bar();
  void render_menu_dropdown();
  void render_status_line();
  void render_command_palette();
  void render_quick_pick();
  void render_search_panel();
  void render_context_menu();
  void render_tree_sitter_status_modal();
  void render_save_prompt();
  void render_quit_prompt();
  void render_popup();
  void render_home_menu();
  void render_buffer_content(const SplitPane &pane, int buffer_id);
  void poll_lsp_clients();
  void poll_debugger_sessions();
  void watch_lsp_client_fds(LSPClient *client);
  void unwatch_lsp_client_fds(LSPClient *client);
  void watch_debugger_client_fds(DebuggerClient *client);
  void unwatch_debugger_client_fds(DebuggerClient *client);
  void watch_integrated_terminal_fd(IntegratedTerminal *term);
  void unwatch_integrated_terminal_fd(IntegratedTerminal *term);
  void arm_file_tree_watch();
  bool lsp_work_pending() const {
    return !lsp_pending_changes.empty() || !lsp_clients.empty();
  }
  void poll_discord_rpc(long long now_ms);
  LSPClient *find_lsp_client(const std::string &language,
                             const std::string &root_path);

  void handle_terminal_event(const Event &ev);
  void render_frame();
  LSPClient *ensure_lsp_for_file(const std::string &filepath);
  void notify_lsp_open(const std::string &filepath);
  void notify_lsp_change(const std::string &filepath);
  void notify_lsp_save(const std::string &filepath);
  void stop_all_lsp_clients();
  void restart_all_lsp_clients();
  void show_lsp_status();
  void show_lsp_manager();
  bool install_lsp_server(const std::string &name);
  bool remove_lsp_server(const std::string &name);
  bool install_tree_sitter_language(const std::string &language);
  void show_tree_sitter_status();
  void reload_tree_sitter();
  void poll_tree_sitter_installs();
  bool handle_tree_sitter_status_input(int ch);
  bool handle_quick_pick_input(int ch);
  void open_quick_pick(QuickPickKind kind, const std::string &title,
                       std::vector<QuickPickItem> items,
                       const std::string &query = "");
  void close_quick_pick();
  void refresh_quick_pick();
  int quick_pick_match_score(const std::string &query,
                             const QuickPickItem &item) const;
  void accept_quick_pick();
  void show_project_search(const std::string &query = "");
  void show_diagnostics_picker();
  bool goto_next_diagnostic(int direction);
  std::vector<QuickPickItem> diagnostic_quick_pick_items() const;
  void show_symbol_picker();
  void request_document_symbols();
  void handle_document_symbols_result(const LSPDocumentSymbolResult &result);
  std::vector<QuickPickItem> fallback_symbol_items();
  void request_lsp_completion(bool manual, char trigger_character = '\0');
  void request_lsp_hover();
  void request_lsp_hover_at(int pane_index, int buffer_id, const Cursor &pos,
                            int token_start, int token_end, int screen_x,
                            int screen_y);
  void cancel_lsp_mouse_hover(bool hide_popup = true);
  void maybe_fire_lsp_mouse_hover();
  void request_lsp_definition();
  void handle_lsp_hover_result(const LSPHoverResult &hover);
  void handle_lsp_definition_result(const LSPDefinitionResult &definition);
  bool apply_pending_lsp_definition_jump();
  bool apply_pending_lsp_back_jump();
  void return_from_lsp_definition();
  void hide_lsp_completion();
  bool refresh_lsp_completion_filter();
  bool apply_selected_lsp_completion();
  void accept_telescope_selection();
  void render_lsp_completion();
  std::string get_buffer_text(const FileBuffer &buf) const;
  const std::vector<std::pair<int, int>> &
  get_line_syntax_colors(FileBuffer &buf, int line_idx);
  void invalidate_syntax_cache(FileBuffer &buf);

#ifdef JOT_TREESITTER
  void reparse_tree(FileBuffer &buf);
  void init_ts_for_buffer(FileBuffer &buf);
  std::string tree_sitter_extension_for_buffer(const FileBuffer &buf);
#endif

  void handle_input(int ch, bool is_ctrl = false, bool is_shift = false,
                    bool is_alt = false, int original_ch = 0);
  void handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                          bool is_scroll_down);

  void handle_modeless_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  void handle_command_palette(int ch);
  bool execute_ex_command(const std::string &line);
  void show_command_help(const std::string &topic);
  void submit_command_palette();
  void handle_search_panel(int ch, bool is_ctrl = false,
                           bool is_shift = false, bool is_alt = false);
  void handle_telescope(int ch);
  void handle_save_prompt(int ch);
  void handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift,
                                        bool is_alt);
  bool handle_telescope_mouse(int x, int y, bool is_click,
                              bool is_double_click, bool is_scroll_up,
                              bool is_scroll_down);
  bool handle_home_menu_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_home_menu_mouse(int x, int y, bool is_click);
  bool handle_menu_bar_input(int ch);
  bool handle_menu_bar_mouse(int x, int y, bool is_click, bool is_motion);
  bool handle_integrated_terminal_mouse(int x, int y);
  bool handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up,
                                         bool is_scroll_down);
  bool handle_debugger_mouse(int x, int y, bool activate = true);
  void place_integrated_terminal_cursor();
  void handle_mouse(void *event);

  void move_cursor(int dx, int dy, bool extend_selection = false);
  bool insert_char(char c);
  void insert_string(const std::string &str);
  void delete_char(bool forward = true);
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

  void show_popup(const std::string &text, int x, int y);
  void hide_popup();
  void open_context_menu(int x, int y, ContextMenuSurface surface,
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
  void set_diagnostics(const std::string &filepath,
                       const std::vector<Diagnostic> &diagnostics);
  void add_diagnostic(const std::string &filepath,
                      const Diagnostic &diagnostic);

public:
  void toggle_sidebar();
  void load_file_tree(const std::string &path);
  void open_workspace(const std::string &path, bool restore_session = true);
  bool resume_last_workspace_session();
  void set_home_menu_visible(bool visible);

private:
  void handle_sidebar_input(int ch);
  void handle_sidebar_mouse(int x, int y, bool is_click,
                            bool is_double_click = false);
  void render_sidebar();
  void render_collapsed_sidebar_handle();
  int sidebar_activity_rail_width() const { return 4; }
  int min_sidebar_width() const { return sidebar_activity_rail_width() + 18; }
  int sidebar_close_threshold() const { return 12; }
  int max_sidebar_width() const;
  int effective_sidebar_width() const;
  bool collapsed_sidebar_handle_hit_test(int x, int y) const;
  bool sidebar_resize_hit_test(int x, int y) const;
  bool begin_sidebar_resize_drag(int x, int y);
  bool update_sidebar_resize_drag(int x);
  void end_sidebar_resize_drag();
  int min_right_panel_width() const { return 28; }
  int max_right_panel_width() const;
  bool right_panel_resize_hit_test(int x, int y) const;
  bool begin_right_panel_resize_drag(int x, int y);
  bool update_right_panel_resize_drag(int x);
  void end_right_panel_resize_drag();
  void build_tree(const std::string &path, std::vector<FileNode> &nodes,
                  int depth);
  void refresh_tree_children(FileNode &node);
  std::string build_file_tree_signature() const;
  void refresh_file_tree_watch_baseline();
  void poll_file_tree_changes();
  void invalidate_sidebar_tree_cache();
  void invalidate_sidebar_diagnostics_cache();
  void invalidate_sidebar_git_cache();
  void ensure_sidebar_render_cache();
  void rebuild_sidebar_tree_cache();
  void rebuild_sidebar_diagnostics_cache();
  void rebuild_sidebar_git_cache();
  std::vector<GitSidebarRow> build_git_sidebar_rows() const;

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
  void finish_open_file(FileBuffer fb, const std::string &path_to_open,
                        bool preview);
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
  bool git_status_active() const {
    return !git_root.empty() || !git_branch.empty() || git_dirty_count != 0 ||
           git_staged_count != 0 || git_unstaged_count != 0 ||
           git_untracked_count != 0 || git_deleted_count != 0 ||
           git_renamed_count != 0 || git_conflict_count != 0 ||
           !git_file_status.empty() ||
           (workspace_session_enabled && !workspace_session_root.empty()) ||
           !root_dir.empty();
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
  bool git_commit_message(const std::string &message);

  void toggle_minimap();
  void toggle_integrated_terminal();
  void create_integrated_terminal();
  void create_integrated_terminal(const std::string &label,
                                  const std::string &cwd = "");
  void close_integrated_terminal(int index);
  void activate_integrated_terminal(int index, bool focus = true);
  void load_terminal_tasks();
  std::vector<std::string> list_terminal_task_names();
  void show_terminal_tasks();
  bool run_terminal_task(const std::string &name, bool force_new = false);
  bool rerun_last_terminal_task();
  void toggle_debugger_panel();
  bool start_debugger_session(DebuggerSessionConfig config);
  bool start_debugger_command(const std::string &adapter,
                              const std::string &command_line);
  bool attach_debugger_command(const std::string &adapter,
                               const std::string &pid_text);
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
  void update_debugger_breakpoint_hover(int pane_index, int buffer_id,
                                        int line);
  void clear_debugger_breakpoint_hover();
  bool is_debugger_breakpoint_hover(int buffer_id, int line) const;
  void load_debugger_configs();
  std::vector<std::string> list_debugger_config_names();
  bool run_debugger_config(const std::string &name);
  DebuggerClient *get_debugger_session(int index = -1);
  void toggle_search();
  void open_search();
  bool open_scoped_replace_from_selection();
  void toggle_command_palette();
  void open_theme_chooser();
  void execute_command(const std::string &cmd);

  void find_next();
  void find_prev();
  void perform_search();
  void clear_search_scope();
  bool replace_current_search_match();
  bool replace_all_search_matches();
  void refresh_folds(FileBuffer &buf);
  bool toggle_fold_at_line(FileBuffer &buf, int line);
  bool fold_at_cursor();
  bool unfold_at_cursor();
  bool toggle_fold_at_cursor();
  void fold_all();
  void unfold_all();
  bool is_line_hidden_by_fold(const FileBuffer &buf, int line) const;
  int buffer_line_for_visible_row(const FileBuffer &buf, int first_line,
                                  int row) const;

  void split_pane_horizontal();
  void split_pane_vertical();
  void split_pane_left();
  void split_pane_right();
  void split_pane_up();
  void split_pane_down();
  void close_pane();
  void next_pane();
  void prev_pane();
  bool focus_pane_direction(char dir);
  bool resize_current_pane(int delta);
  bool resize_current_pane_direction(char dir, int delta);
  int pane_split_at_position(int x, int y) const;
  bool begin_pane_resize_drag(int x, int y);
  bool update_pane_resize_drag(int x, int y);
  void end_pane_resize_drag();
  bool is_pane_resize_dragging() const { return pane_resize_dragging; }
  bool pane_split_is_resizing(int node_index) const {
    return pane_resize_dragging && pane_resize_node == node_index;
  }
  bool adjust_pane_split_ratio(int node_index, int delta,
                               bool clamp_only = false);

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
                        bool case_sensitive = true, bool whole_word = false);
  void replace_all_regex(const std::string &pattern,
                         const std::string &replacement);
  bool surround_selection_or_word(const std::string &left,
                                  const std::string &right);
  bool unsurround_selection_or_cursor();
  bool change_inside_quote(char quote);
  void increment_number_at_cursor(int delta);
  void toggle_auto_indent_setting();
  void change_tab_size(int delta);
  std::vector<std::string> list_available_themes();
  void apply_theme(const std::string &name, bool persist = true,
                   bool announce = true);
  int detect_indent_width(const std::vector<std::string> &lines) const;

  FileBuffer &get_buffer(int id = -1);
  SplitPane &get_pane(int id = -1);
  std::string get_file_extension(const std::string &path);
  std::string get_filename(const std::string &path);
  Theme &get_theme() { return theme; }
  IntegratedTerminal *get_integrated_terminal(int index = -1);

  void load_runtime_config();
  void initialize_state_defaults();
  void initialize_python_runtime();
  void initialize_terminal_ui();
  void initialize_placeholder_buffer();

  int create_pane(int x, int y, int w, int h, int buffer_id);
  void update_pane_layout();
  void split_pane_direction(int dx, int dy);
  void refresh_command_palette();
  void set_message(const std::string &msg);
  bool close_active_floating_ui();

public:
  Editor();
  ~Editor();
  void load_file(const std::string &fname);
  void run();
  EditorHostAPI &host();
  const EditorHostAPI &host() const;
};

#endif
