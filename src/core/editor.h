#ifndef EDITOR_H
#define EDITOR_H

#include <atomic>

#include "autoclose.h"
#include "bracket.h"
#include "config.h"
#include "types.h"
#include "discord_rpc.h"
#include "tools/debugger/client.h"
#include "event_loop.h"
#include "imageviewer.h"
#include "tools/terminal/integrated.h"
#include "tools/lsp/client.h"
#include "task_queue.h"
#include "telescope.h"
#include "terminal.h"
#include "ui.h"
#ifdef JOT_TREESITTER
#include "tree_sitter/manager.h"
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

// #include "python_bridge/api.h"

class SyntaxHighlighter {
private:
  std::vector<SyntaxRule> rules;
  std::string file_extension;

public:
  void set_language(const std::string &ext);
  bool has_rules() const { return !rules.empty(); }
  std::vector<std::pair<int, int>> get_colors(const std::string &line);
};

class PythonAPI; // Forward declaration
class EditorHostAPI;
class HostCoreAPI;
class HostRenderAPI;
class HostIOAPI;

class Editor {
  friend class PythonAPI; // Allow PythonAPI to access private members
  friend class EditorHostAPI;
  friend class HostCoreAPI;
  friend class HostRenderAPI;
  friend class HostIOAPI;

private:
  struct PaneTreeNode {
    bool leaf = true;
    int pane_index = -1;
    int parent = -1;
    int first = -1;
    int second = -1;
    bool vertical = true; // true: left/right, false: up/down
    float ratio = 0.5f;   // first child ratio
  };

  std::vector<FileBuffer> buffers;
  std::vector<SplitPane> panes;
  std::vector<float> pane_weights;
  std::vector<PaneTreeNode> pane_tree;
  int pane_root;
  int current_pane;
  int current_buffer;
  PaneLayoutMode pane_layout_mode;
  
  bool running;
  std::string message;
  std::string clipboard;

  // Command palette
  struct CommandPaletteSuggestion {
    std::string insert_text;
    std::string label;
    std::string category;
    std::string detail;
    int score = 0;
  };

  bool show_command_palette;
  std::string command_palette_query;
  std::vector<CommandPaletteSuggestion> command_palette_results;
  int command_palette_selected;
  bool command_palette_theme_mode;
  std::string command_palette_theme_original;

  enum QuickPickKind {
    QUICK_PICK_NONE,
    QUICK_PICK_PROJECT_SEARCH,
    QUICK_PICK_DIAGNOSTICS,
    QUICK_PICK_SYMBOLS,
    QUICK_PICK_PLUGIN
  };

  struct QuickPickItem {
    std::string label;
    std::string detail;
    std::string preview;
    std::string filepath;
    int line = 0;
    int col = 0;
    int severity = 0;
  };

  QuickPickKind quick_pick_kind;
  bool show_quick_pick;
  std::string quick_pick_title;
  std::string quick_pick_query;
  std::vector<QuickPickItem> quick_pick_all_items;
  std::vector<QuickPickItem> quick_pick_items;
  int quick_pick_selected;

  // Telescope finder
  Telescope telescope;

  // Search panel
  struct SearchMatch {
    int line = 0;
    int col = 0;
    int len = 0;

    bool operator<(const SearchMatch &other) const {
      return std::tie(line, col, len) <
             std::tie(other.line, other.col, other.len);
    }
  };
  bool show_search;
  std::string search_query;
  std::string search_replace_text;
  std::vector<SearchMatch> search_results;
  int search_result_index;
  bool search_case_sensitive;
  bool search_whole_word;
  bool search_regex;
  bool search_replace_visible;
  bool search_focus_replace;
  bool search_scoped_to_selection;
  Cursor search_scope_start;
  Cursor search_scope_end;
  
  // Save Prompt
  bool show_save_prompt;
  std::string save_prompt_input;

  // Quit Prompt
  bool show_quit_prompt;

  // Top application menu bar
  enum MenuBarAction {
    MENU_ACTION_NONE,
    MENU_ACTION_COMMAND,
    MENU_ACTION_NEW_FILE,
    MENU_ACTION_OPEN_FINDER,
    MENU_ACTION_SAVE,
    MENU_ACTION_SAVE_AS,
    MENU_ACTION_CLOSE_FILE,
    MENU_ACTION_QUIT,
    MENU_ACTION_UNDO,
    MENU_ACTION_REDO,
    MENU_ACTION_CUT,
    MENU_ACTION_COPY,
    MENU_ACTION_PASTE,
    MENU_ACTION_SELECT_ALL,
    MENU_ACTION_SELECT_LINE,
    MENU_ACTION_DUPLICATE_LINE,
    MENU_ACTION_MOVE_LINE_UP,
    MENU_ACTION_MOVE_LINE_DOWN,
    MENU_ACTION_TOGGLE_COMMENT,
    MENU_ACTION_COMMAND_PALETTE,
    MENU_ACTION_TOGGLE_SIDEBAR,
    MENU_ACTION_TOGGLE_MINIMAP,
    MENU_ACTION_THEME,
    MENU_ACTION_HOME,
    MENU_ACTION_TOGGLE_TERMINAL,
    MENU_ACTION_NEW_TERMINAL,
    MENU_ACTION_TASKS,
    MENU_ACTION_RERUN_TASK,
    MENU_ACTION_TOGGLE_DEBUG_PANEL,
    MENU_ACTION_DEBUG_STOP,
    MENU_ACTION_DEBUG_CONTINUE,
    MENU_ACTION_DEBUG_PAUSE,
    MENU_ACTION_DEBUG_STEP_IN,
    MENU_ACTION_DEBUG_STEP_OVER,
    MENU_ACTION_DEBUG_STEP_OUT,
    MENU_ACTION_LSP_DEFINITION,
    MENU_ACTION_LSP_BACK,
    MENU_ACTION_HELP
  };

  struct MenuBarItem {
    std::string label;
    MenuBarAction action = MENU_ACTION_NONE;
    std::string command;
    bool enabled = true;
  };

  struct MenuBarMenu {
    std::string label;
    std::vector<MenuBarItem> items;
  };

  struct MenuBarSegment {
    int menu_index = -1;
    int x = 0;
    int end_x = 0;
  };

  bool show_menu_bar_dropdown;
  int menu_bar_active;
  int menu_bar_selected;
  std::vector<MenuBarSegment> menu_bar_segments;

  // Minimap
  bool show_minimap;
  int minimap_width;
  bool show_integrated_terminal;
  int integrated_terminal_height;
  bool show_debugger_panel;
  int debugger_panel_height;
  enum RightPanelTab {
    RIGHT_PANEL_DEBUG,
    RIGHT_PANEL_GIT_DIFF,
    RIGHT_PANEL_PLUGIN
  };
  
  struct GitDiffPanel {
    bool visible = false;
    bool staged = false;
    std::string path;
    std::vector<std::string> lines;
    int scroll = 0;
  };
  
  bool show_right_panel;
  int right_panel_width;
  RightPanelTab active_right_panel_tab;
  GitDiffPanel git_diff_panel;
  std::string active_plugin_panel;
  std::string plugin_quick_pick_select_callback;

  SyntaxHighlighter highlighter;
  Config config;
  DiscordRPC discord_rpc;
  ImageViewer image_viewer;
  std::vector<std::unique_ptr<IntegratedTerminal>> integrated_terminals;
  std::vector<std::unique_ptr<DebuggerClient>> debugger_sessions;
  int current_debugger_session;
  struct DebuggerSessionState {
    std::string name;
    std::string adapter;
    std::string program;
    bool running = false;
    bool stopped = false;
    int active_thread_id = 0;
    int active_frame_id = 0;
    bool supports_read_memory = false;
    bool supports_disassemble = false;
    std::vector<DebuggerThread> threads;
    std::vector<DebuggerVariable> variables;
    std::vector<DebuggerMemoryRow> memory_rows;
    std::vector<DebuggerInstruction> instructions;
    std::string output;
    std::string last_error;
  };
  std::vector<DebuggerSessionState> debugger_session_state;
  std::map<std::string, std::vector<DebuggerBreakpoint>> debugger_breakpoints;
  std::vector<DebuggerSessionConfig> debugger_configs;
  bool debugger_breakpoint_hover_visible;
  int debugger_breakpoint_hover_pane;
  int debugger_breakpoint_hover_buffer;
  int debugger_breakpoint_hover_line;
  std::vector<std::unique_ptr<LSPClient>> lsp_clients;
  std::unordered_map<std::string, long long> lsp_pending_changes;
  int current_integrated_terminal;
  struct TerminalTask {
    std::string name;
    std::string command;
    std::string source_path;
    std::string source_kind;
    std::string cwd;
  };
  std::vector<TerminalTask> terminal_tasks;
  std::string last_terminal_task_name;
  Terminal terminal;
  UI *ui = nullptr;
  Theme theme;
  std::string current_theme_name;

#ifdef JOT_TREESITTER
  TreeSitterManager ts_manager_;
#endif // tracks active color scheme

  struct TreeSitterInstallJob {
    std::string language;
    int terminal_index = -1;
    bool running = true;
    bool succeeded = false;
    bool failed = false;
    std::string progress;
  };
  bool show_tree_sitter_status_modal;
  int tree_sitter_status_scroll;
  std::vector<TreeSitterInstallJob> tree_sitter_install_jobs;

  EventLoop event_loop_;
  std::unique_ptr<TaskQueue> task_queue_;

  int status_height;
  int tab_height;
  int tab_size;
  bool show_indent_guides;
  bool relative_line_numbers;
  int tab_scroll_index;
  int preview_buffer_index;
  long long last_sidebar_click_ms;
  int last_sidebar_click_row;
  long long last_tab_click_ms;
  int last_tab_clicked_index;
  bool auto_indent;
  bool needs_redraw;
  bool mouse_selecting;
  enum MouseSelectionMode {
    MOUSE_SELECT_CHAR,
    MOUSE_SELECT_WORD,
    MOUSE_SELECT_LINE
  };
  MouseSelectionMode mouse_selection_mode;
  Cursor mouse_start;
  Cursor mouse_anchor_end;
  int mouse_press_screen_x;
  int mouse_press_screen_y;
  int mouse_press_buf_x;
  int mouse_press_buf_y;
  bool mouse_drag_started;
  bool lsp_mouse_hover_enabled;
  bool lsp_mouse_hover_pending;
  bool lsp_mouse_hover_visible;
  long long lsp_mouse_hover_deadline_ms;
  int lsp_mouse_hover_pane;
  int lsp_mouse_hover_buffer;
  int lsp_mouse_hover_line;
  int lsp_mouse_hover_col;
  int lsp_mouse_hover_token_start;
  int lsp_mouse_hover_token_end;
  int lsp_mouse_hover_screen_x;
  int lsp_mouse_hover_screen_y;
  std::string lsp_mouse_hover_filepath;
  bool pane_resize_dragging;
  int pane_resize_node;
  bool pane_resize_vertical;
  int pane_resize_start_pos;
  float pane_resize_start_ratio;
  bool sidebar_resize_dragging;
  bool sidebar_resize_opening;
  int sidebar_resize_start_x;
  int sidebar_resize_start_width;
  bool right_panel_resize_dragging;
  int right_panel_resize_start_x;
  int right_panel_resize_start_width;
  bool scrollbar_dragging;
  int scrollbar_drag_pane;
  int scrollbar_drag_start_y;
  int scrollbar_drag_start_scroll;
  int scrollbar_drag_track_y;
  int scrollbar_drag_track_h;
  int scrollbar_drag_thumb_h;
  int scrollbar_drag_max_scroll;
  long long last_left_click_ms;
  Cursor last_left_click_pos;
  int last_left_click_count;

  int idle_frame_count;
  int cursor_blink_frame;
  bool cursor_visible;
  int render_fps;
  int idle_fps;
  int lsp_change_debounce_ms;
  int last_cursor_shape;
  bool smart_paste_indent;
  long long keyboard_press_count;

  bool show_context_menu;
  enum ContextMenuSurface {
    CONTEXT_MENU_NONE,
    CONTEXT_MENU_EDITOR,
    CONTEXT_MENU_TAB,
    CONTEXT_MENU_SIDEBAR,
    CONTEXT_MENU_TERMINAL
  };
  enum ContextMenuAction {
    CONTEXT_ACTION_NONE,
    CONTEXT_ACTION_COPY,
    CONTEXT_ACTION_CUT,
    CONTEXT_ACTION_PASTE,
    CONTEXT_ACTION_SAVE_BUFFER,
    CONTEXT_ACTION_CLOSE_BUFFER,
    CONTEXT_ACTION_SIDEBAR_OPEN,
    CONTEXT_ACTION_SIDEBAR_NEW_FILE,
    CONTEXT_ACTION_SIDEBAR_NEW_FOLDER,
    CONTEXT_ACTION_SIDEBAR_RENAME,
    CONTEXT_ACTION_SIDEBAR_REFRESH,
    CONTEXT_ACTION_SIDEBAR_COPY_PATH,
    CONTEXT_ACTION_GIT_STAGE,
    CONTEXT_ACTION_GIT_UNSTAGE,
    CONTEXT_ACTION_GIT_DIFF,
    CONTEXT_ACTION_GIT_DIFF_STAGED,
    CONTEXT_ACTION_GIT_STAGE_ALL,
    CONTEXT_ACTION_GIT_REFRESH,
  CONTEXT_ACTION_TERMINAL_FOCUS,
  CONTEXT_ACTION_TERMINAL_NEW,
  CONTEXT_ACTION_TERMINAL_CLOSE,
  CONTEXT_ACTION_TERMINAL_RESET_SCROLL,
  CONTEXT_ACTION_TOGGLE_FOLD
};
  struct ContextMenuItem {
    std::string label;
    ContextMenuAction action = CONTEXT_ACTION_NONE;
    bool enabled = true;
  };
  ContextMenuSurface context_menu_surface;
  std::vector<ContextMenuItem> context_menu_items;
  int context_menu_x;
  int context_menu_y;
  int context_menu_w;
  int context_menu_h;
  int context_menu_selected;
  int context_menu_target_buffer;
  int context_menu_target_pane;
  int context_menu_target_terminal;
  int context_menu_target_line;
  std::string context_menu_target_path;
  bool context_menu_target_is_dir;

  bool lsp_completion_visible;
  bool lsp_completion_manual_request;
  int lsp_completion_selected;
  Cursor lsp_completion_anchor;
  Cursor lsp_completion_replace_start;
  std::string lsp_completion_filepath;
  std::string lsp_completion_prefix;
  std::vector<LSPCompletionItem> lsp_completion_all_items;
  std::vector<LSPCompletionItem> lsp_completion_items;
  struct LSPJumpLocation {
    std::string filepath;
    Cursor cursor;
    int scroll_offset = 0;
    int scroll_x = 0;
    bool preview = false;
  };
  std::vector<LSPJumpLocation> lsp_jump_stack;
  bool lsp_definition_jump_pending;
  LSPLocation lsp_definition_pending_location;
  bool lsp_back_jump_pending;
  LSPJumpLocation lsp_back_pending_location;

  Popup popup; // New

  struct ClosedBufferSnapshot {
    std::string filepath;
    std::vector<std::string> lines;
    Cursor cursor;
    Selection selection;
    int scroll_offset;
    int scroll_x;
    bool modified;
    std::vector<FoldRange> collapsed_folds;
  };
  std::vector<ClosedBufferSnapshot> closed_buffer_history;
  std::vector<std::string> recent_files;
  std::vector<std::string> recent_workspaces;
  std::unordered_map<std::string, int> workspace_diagnostic_severity;
  std::string git_root;
  std::string git_branch;
  int git_dirty_count;
  int git_staged_count;
  int git_unstaged_count;
  int git_untracked_count;
  int git_deleted_count;
  int git_renamed_count;
  int git_conflict_count;
  std::atomic<bool> git_refresh_pending_{false};
  std::unordered_map<std::string, std::string> git_file_status;
  long long git_last_refresh_ms;
  bool auto_save_enabled;
  int auto_save_interval_ms;
  long long last_auto_save_ms;

  std::string discord_rpc_last_details;
  std::string discord_rpc_last_state;

  struct HomeMenuEntry {
    int action;
    int recent_index;
    int recent_workspace_index;
    int x;
    int y;
    int w;
  };
  bool show_home_menu;
  int home_menu_selected;
  int home_menu_panel_x;
  int home_menu_panel_y;
  int home_menu_panel_w;
  int home_menu_panel_h;
  std::vector<HomeMenuEntry> home_menu_entries;

  // Sidebar
  enum SidebarView {
    SIDEBAR_VIEW_EXPLORER,
    SIDEBAR_VIEW_GIT
  };
  bool show_sidebar;
  SidebarView active_sidebar_view;
  int sidebar_width;
  std::string root_dir;
  bool workspace_session_enabled;
  std::string workspace_session_root;
  std::vector<FileNode> file_tree;
  int file_tree_selected;
  int file_tree_scroll; // New
  int git_sidebar_selected;
  int git_sidebar_scroll;
  bool sidebar_show_hidden;
  std::string file_tree_watch_signature_;
  bool file_tree_watch_ready_;
  std::string file_tree_event_watch_root_;

  struct SidebarRenderRow {
    std::string path;
    std::string normalized_path;
    std::string name;
    std::string label;
    std::string footer_label;
    bool is_dir = false;
    bool expanded = false;
    int depth = 0;
    int diagnostic_severity = 0;
    std::string git_status;
  };

  struct SidebarRenderCache {
    std::vector<SidebarRenderRow> rows;
    std::unordered_map<std::string, int> path_to_row;
    std::string root_label;
    std::string normalized_root;
    bool tree_dirty = true;
    bool diagnostics_dirty = true;
    bool git_dirty = true;
  };
  SidebarRenderCache sidebar_render_cache_;

  struct GitSidebarRow {
    std::string path;
    std::string relative_path;
    std::string status;
  };

  struct FileTabSegment {
    int buffer_id = -1;
    int tab_index = -1;
    int x = 0;
    int label_x = 0;
    int close_x = 0;
    int end_x = 0;
    std::string label;
    bool active = false;
    bool modified = false;
    bool preview = false;
  };

  struct FileTabLayout {
    int x = 0;
    int y = 0;
    int w = 0;
    std::vector<FileTabSegment> segments;
    std::string scroll_left_label;
    std::string overflow_label;
    int overflow_x = 0;
    int scroll_left_x = -1;
    int scroll_left_end_x = -1;
    int scroll_right_x = -1;
    int scroll_right_end_x = -1;
    int hidden_before = 0;
    int hidden_after = 0;
    int hidden_count = 0;
  };

  enum EditorFocus { FOCUS_EDITOR, FOCUS_SIDEBAR };
  EditorFocus focus_state;

  // Easter egg — Konami code
  std::vector<int> recent_keys;   // ring buffer of recent navigation keys
  int easter_egg_timer;           // frames remaining to show the easter egg

  PythonAPI *python_api;
  std::unique_ptr<EditorHostAPI> host_api;

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
  void render_popup(); // New
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
  // True when there is LSP work pending (pending change notifications
  // or active clients). The event loop uses this to decide whether
  // background LSP polling has anything useful to do.
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

  // API methods
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
  // Sidebar methods
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
  void ensure_cursor_visible(bool adjust_horizontal = true); // New method
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
  // True when a git background refresh could find anything to do. Used
  // by the event loop to decide whether to register the git refresh
  // timer at all (an empty-file session with no repo should not be
  // running a 1.5s timer that always returns immediately).
  bool git_status_active() const {
    return !git_root.empty() || !git_branch.empty() ||
           git_dirty_count != 0 || git_staged_count != 0 ||
           git_unstaged_count != 0 || git_untracked_count != 0 ||
           git_deleted_count != 0 || git_renamed_count != 0 ||
           git_conflict_count != 0 || !git_file_status.empty() ||
           (workspace_session_enabled && !workspace_session_root.empty()) ||
           !root_dir.empty();
  }
  std::string run_git_capture(const std::string &args) const;
  std::string to_git_relative_path(const std::string &path) const;
  bool open_git_diff_panel(const std::string& path, bool staged);
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
  bool adjust_pane_split_ratio(int node_index, int delta, bool clamp_only = false);

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
  void replace_all_text(const std::string &needle, const std::string &replacement,
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
