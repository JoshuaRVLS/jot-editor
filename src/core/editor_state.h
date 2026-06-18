#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#include <atomic>

#include "config.h"
#include "discord_rpc.h"
#include "editor_models.h"
#include "event_loop.h"
#include "imageviewer.h"
#include "syntax_highlighter.h"
#include "task_queue.h"
#include "telescope.h"
#include "terminal.h"
#include "tools/lsp/client.h"
#include "tools/terminal/integrated.h"
#include "ui.h"
#ifdef JOT_TREESITTER
#include "tree_sitter/manager.h"
#endif
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DebuggerClient;
class EditorHostAPI;
class PythonAPI;

struct EditorState {
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

  bool show_command_palette;
  std::string command_palette_query;
  std::vector<CommandPaletteSuggestion> command_palette_results;
  int command_palette_selected;
  bool command_palette_theme_mode;
  std::string command_palette_theme_original;

  QuickPickKind quick_pick_kind;
  bool show_quick_pick;
  std::string quick_pick_title;
  std::string quick_pick_query;
  std::vector<QuickPickItem> quick_pick_all_items;
  std::vector<QuickPickItem> quick_pick_items;
  int quick_pick_selected;

  Telescope telescope;

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

  bool show_save_prompt;
  std::string save_prompt_input;
  bool show_quit_prompt;

  bool show_menu_bar_dropdown;
  int menu_bar_active;
  int menu_bar_selected;
  std::vector<MenuBarSegment> menu_bar_segments;

  bool show_minimap;
  int minimap_width;
  bool show_integrated_terminal;
  int integrated_terminal_height;
  bool show_debugger_panel;
  int debugger_panel_height;
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
  std::vector<TerminalTask> terminal_tasks;
  std::string last_terminal_task_name;
  Terminal terminal;
  UI *ui = nullptr;
  Theme theme;
  std::string current_theme_name;

#ifdef JOT_TREESITTER
  TreeSitterManager ts_manager_;
#endif

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
  std::vector<LSPJumpLocation> lsp_jump_stack;
  bool lsp_definition_jump_pending;
  LSPLocation lsp_definition_pending_location;
  bool lsp_back_jump_pending;
  LSPJumpLocation lsp_back_pending_location;

  Popup popup;

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

  bool show_home_menu;
  int home_menu_selected;
  int home_menu_panel_x;
  int home_menu_panel_y;
  int home_menu_panel_w;
  int home_menu_panel_h;
  std::vector<HomeMenuEntry> home_menu_entries;

  bool show_sidebar;
  SidebarView active_sidebar_view;
  int sidebar_width;
  std::string root_dir;
  bool workspace_session_enabled;
  std::string workspace_session_root;
  std::vector<FileNode> file_tree;
  int file_tree_selected;
  int file_tree_scroll;
  int git_sidebar_selected;
  int git_sidebar_scroll;
  bool sidebar_show_hidden;
  std::string file_tree_watch_signature_;
  bool file_tree_watch_ready_;
  std::string file_tree_event_watch_root_;
  SidebarRenderCache sidebar_render_cache_;

  EditorFocus focus_state;
  std::vector<int> recent_keys;
  int easter_egg_timer;

  PythonAPI *python_api;
  std::unique_ptr<EditorHostAPI> host_api;
};

#endif
