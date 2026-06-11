#ifndef EDITOR_H
#define EDITOR_H

#include <atomic>

#include "autoclose.h"
#include "bracket.h"
#include "config.h"
#include "types.h"
#include "discord_rpc.h"
#include "event_loop.h"
#include "imageviewer.h"
#include "integrated_terminal.h"
#include "lsp_client.h"
#include "task_queue.h"
#include "telescope.h"
#include "terminal.h"
#include "ui.h"
#ifdef JOT_TREESITTER
#include "tree_sitter_manager.h"
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// #include "python_api.h"

class SyntaxHighlighter {
private:
  std::vector<SyntaxRule> rules;
  std::string file_extension;

public:
  void set_language(const std::string &ext);
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

  // Telescope finder
  Telescope telescope;
  bool waiting_for_space_f;

  // Search panel
  bool show_search;
  std::string search_query;
  std::vector<std::pair<int, int>> search_results; // (line, col)
  int search_result_index;
  bool search_case_sensitive;
  bool search_whole_word;
  
  // Save Prompt
  bool show_save_prompt;
  std::string save_prompt_input;

  // Quit Prompt
  bool show_quit_prompt;

  // Minimap
  bool show_minimap;
  int minimap_width;
  bool show_integrated_terminal;
  int integrated_terminal_height;

  SyntaxHighlighter highlighter;
  Config config;
  DiscordRPC discord_rpc;
  ImageViewer image_viewer;
  std::vector<std::unique_ptr<IntegratedTerminal>> integrated_terminals;
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

  EventLoop event_loop_;
  std::unique_ptr<TaskQueue> task_queue_;

  int status_height;
  int tab_height;
  int tab_size;
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
  bool pane_resize_dragging;
  int pane_resize_node;
  bool pane_resize_vertical;
  int pane_resize_start_pos;
  float pane_resize_start_ratio;
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
    CONTEXT_ACTION_TERMINAL_FOCUS,
    CONTEXT_ACTION_TERMINAL_NEW,
    CONTEXT_ACTION_TERMINAL_CLOSE,
    CONTEXT_ACTION_TERMINAL_RESET_SCROLL
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
  std::string context_menu_target_path;
  bool context_menu_target_is_dir;

  bool lsp_completion_visible;
  bool lsp_completion_manual_request;
  int lsp_completion_selected;
  Cursor lsp_completion_anchor;
  std::string lsp_completion_filepath;
  std::vector<LSPCompletionItem> lsp_completion_items;

  Popup popup; // New

  struct ClosedBufferSnapshot {
    std::string filepath;
    std::vector<std::string> lines;
    Cursor cursor;
    Selection selection;
    int scroll_offset;
    int scroll_x;
    bool modified;
  };
  std::vector<ClosedBufferSnapshot> closed_buffer_history;
  std::vector<std::string> recent_files;
  std::vector<std::string> recent_workspaces;
  std::unordered_map<std::string, int> workspace_diagnostic_severity;
  std::string git_root;
  std::string git_branch;
  int git_dirty_count;
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
  bool show_sidebar;
  int sidebar_width;
  std::string root_dir;
  bool workspace_session_enabled;
  std::string workspace_session_root;
  std::vector<FileNode> file_tree;
  int file_tree_selected;
  int file_tree_scroll; // New
  bool sidebar_show_hidden;

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

  struct FileTabSegment {
    int buffer_id = -1;
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
    std::string overflow_label;
    int overflow_x = 0;
    int hidden_count = 0;
  };

  enum EditorFocus { FOCUS_EDITOR, FOCUS_SIDEBAR };
  EditorFocus focus_state;

  // Vim-like modal editing
  EditorMode mode;
  Cursor visual_start;      // anchor position when entering Visual mode
  bool visual_line_mode;    // true = V (line visual), false = v (char visual)
  char pending_key;         // first char of a two-key sequence (g, d, y, c…)
  bool has_pending_key;

  // Easter egg — Konami code
  std::vector<int> recent_keys;   // ring buffer of last 8 normal-mode keys
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
  void render_scrollbar(const SplitPane &pane, int draw_w);
  void render_telescope();
  void render_minimap(int x, int y, int w, int h, int buffer_id);
  void render_image_viewer();
  void render_integrated_terminal();
  void render_status_line();
  void render_command_palette();
  void render_search_panel();
  void render_context_menu();
  void render_save_prompt();
  void render_quit_prompt();
  void render_popup(); // New
  void render_home_menu();
  void render_buffer_content(const SplitPane &pane, int buffer_id);
  void poll_lsp_clients();
  // True when there is LSP work pending (pending change notifications
  // or active clients). The event loop uses this to decide whether
  // to register the 50ms LSP poll timer at all.
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
  void request_lsp_completion(bool manual, char trigger_character = '\0');
  void hide_lsp_completion();
  bool apply_selected_lsp_completion();
  void render_lsp_completion();
  std::string get_buffer_text(const FileBuffer &buf) const;
  const std::vector<std::pair<int, int>> &
  get_line_syntax_colors(FileBuffer &buf, int line_idx);
  void invalidate_syntax_cache(FileBuffer &buf);

#ifdef JOT_TREESITTER
  void reparse_tree(FileBuffer &buf);
  void init_ts_for_buffer(FileBuffer &buf);
#endif

  void handle_input(int ch, bool is_ctrl = false, bool is_shift = false,
                    bool is_alt = false, int original_ch = 0);
  void handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                          bool is_scroll_down);

  void handle_normal_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  void handle_insert_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  void handle_visual_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  void handle_command_palette(int ch);
  void submit_command_palette();
  void handle_search_panel(int ch, bool is_ctrl = false,
                           bool is_shift = false, bool is_alt = false);
  void handle_telescope(int ch);
  void handle_save_prompt(int ch);
  void handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift,
                                        bool is_alt);
  bool handle_home_menu_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_home_menu_mouse(int x, int y, bool is_click);
  bool handle_integrated_terminal_mouse(int x, int y);
  bool handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up,
                                         bool is_scroll_down);
  void place_integrated_terminal_cursor();
  void handle_mouse(void *event);

  void enter_normal_mode();
  void enter_insert_mode();
  void enter_visual_mode(bool line_mode = false);

  void vim_delete_line();
  void vim_delete_char();
  void vim_yank();
  void vim_paste();

  void move_cursor(int dx, int dy, bool extend_selection = false);
  void insert_char(char c);
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
  void set_diagnostics(const std::string &filepath,
                       const std::vector<Diagnostic> &diagnostics);
  void add_diagnostic(const std::string &filepath,
                      const Diagnostic &diagnostic);

public:
  // Sidebar methods
  void toggle_sidebar();
  void load_file_tree(const std::string &path);
  void open_workspace(const std::string &path, bool restore_session = true);
  void set_home_menu_visible(bool visible);

private:
  void handle_sidebar_input(int ch);
  void handle_sidebar_mouse(int x, int y, bool is_click,
                            bool is_double_click = false);
  void render_sidebar();
  void build_tree(const std::string &path, std::vector<FileNode> &nodes,
                  int depth);
  void invalidate_sidebar_tree_cache();
  void invalidate_sidebar_diagnostics_cache();
  void invalidate_sidebar_git_cache();
  void ensure_sidebar_render_cache();
  void rebuild_sidebar_tree_cache();
  void rebuild_sidebar_diagnostics_cache();
  void rebuild_sidebar_git_cache();

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
           git_dirty_count != 0 || !git_file_status.empty() ||
           (workspace_session_enabled && !workspace_session_root.empty()) ||
           !root_dir.empty();
  }
  std::string run_git_capture(const std::string &args) const;
  std::string to_git_relative_path(const std::string &path) const;

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
  void toggle_search();
  void toggle_command_palette();
  void open_theme_chooser();
  void execute_command(const std::string &cmd);

  void find_next();
  void find_prev();
  void perform_search();

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
  void increment_number_at_cursor(int delta);
  void toggle_auto_indent_setting();
  void change_tab_size(int delta);
  std::vector<std::string> list_available_themes();
  void apply_theme(const std::string &name, bool persist = true,
                   bool announce = true);
  int detect_indent_width(const std::vector<std::string> &lines) const;

  FileBuffer &get_buffer(int id = -1);
  SplitPane &get_pane(int id = -1);
  // EditorMode get_mode() const { return mode; }
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
