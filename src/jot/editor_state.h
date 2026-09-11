#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#include <atomic>
#include <cstdint>

#include "config.h"
#include "discord_rpc.h"
#include "features/color_codes.h"
#include "features/color_definitions.h"
#include "jot/workspace/git_panel_models.h"
#include "editor_models.h"
#include "event_loop.h"
#include "imageviewer.h"
#include "syntax_highlighter.h"
#include "task_queue.h"
#include "telescope.h"
#include "terminal.h"
#include "tools/lsp/client.h"
#include "tools/terminal/integrated.h"
#include "tree_sitter/manager.h"
#include "ui.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DebuggerClient;
class EditorHostAPI;
class LuaAPI;

struct EditorState
{
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
  std::uint64_t transient_message_timer = 0;
  std::uint64_t message_generation = 0;
  std::string clipboard;

  bool show_command_palette;
  // Scroll anchor of the visible palette window (follow-window: the selected
  // row stays visible, but the window only moves when selection leaves it,
  // so mouse hover over a visible row never shifts the list under the
  // pointer). Kept separate from the selection like quick_pick_scroll.
  int command_palette_scroll = 0;
  std::string command_palette_query;
  // Query text remembered when the palette closes with Esc; restored on the
  // next Ctrl+P open so an abandoned search can be picked up where it left
  // off. Cleared when the palette closes with an empty input.
  std::string command_palette_last_query;
  std::vector<CommandPaletteSuggestion> command_palette_results;
  int command_palette_selected;
  bool command_palette_theme_mode;
  std::string command_palette_theme_original;

  QuickPickKind quick_pick_kind;
  bool show_quick_pick;
  // Scroll anchor of the visible quick-pick window (follow-window, same
  // contract as command_palette_scroll).
  int quick_pick_scroll = 0;
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

  // Which-key style keybind helper. It appears automatically when the user
  // presses a chord that is a prefix of longer plugin keymap sequences (e.g.
  // "Ctrl+T" when "Ctrl+T N" / "Ctrl+T D" exist) and lists the next chord
  // options above the status line. which_key_path holds the canonical chords
  // pressed so far (e.g. {"Ctrl+T", "N"}) — never empty while open.
  bool show_which_key = false;
  std::vector<std::string> which_key_path;
  int which_key_selected = 0;

  // Lua UI surface close-tracking: remembered visibility from the previous
  // frame so render() can notify handlers (fn(nil)) when a surface closes.
  bool lua_ui_prev_command_palette = false;
  bool lua_ui_prev_quick_pick = false;
  bool lua_ui_prev_popup = false;
  bool lua_ui_prev_save_prompt = false;
  bool lua_ui_prev_quit_prompt = false;
  bool lua_ui_prev_tree_sitter_status = false;
  bool lua_ui_prev_lsp_status = false;
  bool lua_ui_prev_telescope = false;
  bool lua_ui_prev_lsp_completion = false;
  bool lua_ui_prev_lsp_signature = false;
  bool lua_ui_prev_context_menu = false;
  bool lua_ui_prev_menu_dropdown = false;
  bool lua_ui_prev_search = false;
  bool lua_ui_prev_home = false;
  bool lua_ui_prev_sidebar = false;
  bool lua_ui_prev_side_panel = false;
  bool lua_ui_prev_settings = false;

  bool show_menu_bar_dropdown;
  int menu_bar_active;
  int menu_bar_selected;
  std::vector<MenuBarSegment> menu_bar_segments;

  bool show_minimap;
  int minimap_width;
  bool show_integrated_terminal;
  int integrated_terminal_height;
  // True while the terminal fills the whole pane area (like pane zoom);
  // panes stay laid out underneath but are covered and their clicks route
  // to the terminal until the zoom is toggled off.
  bool terminal_zoom_active;
  // Mouse selection in the integrated terminal: anchors live in full-space
  // row/col coordinates (see IntegratedTerminal::get_total_rows) so they
  // survive scrolls and redraws. active = a selection exists (rendered),
  // dragging = the mouse button is held and motions extend it.
  bool terminal_sel_active;
  bool terminal_sel_dragging;
  int terminal_sel_anchor_row;
  int terminal_sel_anchor_col;
  int terminal_sel_cur_row;
  int terminal_sel_cur_col;
  bool show_debugger_panel;
  int debugger_panel_height;
  bool show_right_panel;
  int right_panel_width;
  RightPanelTab active_right_panel_tab;
  // Ordered list of panels opened in the right dock (VSCode-style tabs).
  // Commands like :gitpanel add their tab here; the tab strip at the top of
  // the panel switches between them and each tab can be closed individually.
  std::vector<RightPanelTab> right_panel_tabs;
  GitDiffPanel git_diff_panel;
  OutlinePanelState outline_panel;
  std::string active_plugin_panel;
  std::string plugin_quick_pick_select_callback;
  // Actions offered by the last code-action response, indexed by the quick
  // pick selection; cleared once the selection is applied.
  std::vector<LSPCodeAction> lsp_code_actions_pending;

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
  std::vector<LspInstallJob> lsp_install_jobs;
  std::set<std::string> lsp_disabled_servers;
  // LSP published diagnostics kept per (server|root -> filepath) so several
  // servers attached to one buffer merge instead of clobbering each other.
  std::map<std::string, std::map<std::string, std::vector<Diagnostic>>> lsp_diag_slices_;
  int current_integrated_terminal;
  std::vector<TerminalTask> terminal_tasks;
  std::string last_terminal_task_name;
  Terminal terminal;
  UI *ui = nullptr;
  // True when the SDL3/OpenGL GUI frontend owns the screen instead of the
  // terminal backend (jot --gui). Skips raw-mode/stdio setup in run().
  bool gui_mode = false;
  Theme theme;
  std::string current_theme_name;

  TreeSitterManager ts_manager_;

  bool show_tree_sitter_status_modal;
  int tree_sitter_status_scroll;
  // Hovered row inside the status modals (mouse motion), -1 when none.
  int tree_sitter_status_hover_row = -1;
  std::vector<TreeSitterInstallJob> tree_sitter_install_jobs;

  bool show_lsp_status_modal;
  int lsp_status_scroll;
  int lsp_status_hover_row = -1;

  EventLoop event_loop_;
  std::unique_ptr<TaskQueue> task_queue_;

  int status_height;
  int tab_height;
  int tab_size;
  bool show_indent_guides;
  // Inline colour preview (features/color_codes.cpp): its options are read from
  // config at point of use in render_buffer_content, which is what makes a
  // settings change (or :reload) apply on the next frame with no plumbing. The
  // scan memo is content-hash validated, so it needs no invalidation and is
  // shared across buffers (the key is the line's bytes).
  jot_color::SpanCache colorizer_cache;
  // Colour-preview variable definitions (--name: value / $name: value) for the
  // buffer being rendered, plus a version that is bumped on every rebuild so the
  // line cache above re-resolves references instead of serving a stale colour.
  jot_color::Definitions colorizer_defs;
  std::uint64_t colorizer_defs_version = 0;
  std::string colorizer_defs_path;
  bool colorizer_defs_dirty = true;
  bool relative_line_numbers;
  bool highlight_cursor_line;
  int tab_scroll_index;
  int preview_buffer_index;
  long long last_sidebar_click_ms;
  long long last_git_panel_click_ms = 0;
  int last_git_panel_click_row = -1;
  // Mouse hover tracking (motion events): the model row under the pointer in
  // the git panel and the hovered right-dock tab, -1 when none. Purely
  // visual — selection is never overwritten by hover.
  int git_panel_hover_row = -1;
  int right_panel_hover_tab = -1;
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
  // VSCode-style Ctrl+hover goto-definition affordance: while Ctrl is held
  // and the mouse rests on a word, the token under the cursor is underlined
  // (straight underline in the definition-link color) to signal that
  // Ctrl+click will jump to its definition. Cleared on any motion without
  // Ctrl, click, keypress or scroll. Buffer id + token range so stale
  // state never paints after buffer switches.
  bool ctrl_hover_active = false;
  int ctrl_hover_buffer = -1;
  int ctrl_hover_line = -1;
  int ctrl_hover_start = -1;
  int ctrl_hover_end = -1;
  bool pane_resize_dragging;
  int pane_resize_node;
  bool pane_resize_vertical;
  int pane_resize_start_pos;
  float pane_resize_start_ratio;
  bool pane_zoom_active; // true while one pane is expanded over the others
  int pane_zoom_pane;    // pane index expanded while zoomed (-1 when off)
  // Dragging the terminal panel's top border resizes its height live.
  bool terminal_resize_dragging;
  int terminal_resize_start_y;
  int terminal_resize_start_height;
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

  int render_fps;
  int idle_fps;
  int lsp_change_debounce_ms;
  int last_cursor_shape;
  bool smart_paste_indent;
  long long keyboard_press_count;

  // One software blink clock for the terminal cursor and the extra-caret
  // highlights: anchor in steady-clock ms, a suspension window (input keeps
  // the cursor solid for a moment), and the effective visibility applied to
  // both the cursor and the caret paint.
  long long blink_anchor_ms;
  long long blink_suspend_until_ms;
  bool blink_visible;

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
  // Server that produced the current items (LSPClient::server_id), used by the
  // completion popup to pick per-server label presentation.
  std::string lsp_completion_server;
  std::string lsp_completion_prefix;
  // nvim-cmp-style ghost text: the selected item's insert text minus the
  // typed prefix, previewed dimmed at the cursor while the popup is open.
  std::string lsp_completion_ghost_text;
  std::vector<LSPCompletionItem> lsp_completion_all_items;
  std::vector<LSPCompletionItem> lsp_completion_items;
  bool lsp_signature_visible;
  // Position of the '(' that opened the call shown by the signature popup.
  // While the caret stays past this paren on the same line the popup keeps
  // tracking the call.
  int lsp_signature_open_paren_line;
  int lsp_signature_open_paren_col;
  std::string lsp_signature_filepath;
  LSPSignatureHelpResult lsp_signature_result;
  // Per-file cache of textDocument/inlayHint results (parameter-name virtual
  // text on existing code). start_line/end_line are the requested range;
  // dirty means the file changed since the last answer; in_flight guards
  // against stacking requests for the same file.
  struct LspInlayHintCache
  {
    int start_line = -1;
    int end_line = -1;
    bool in_flight = false;
    bool dirty = true;
    std::vector<LSPInlayHint> hints;
  };
  std::map<std::string, LspInlayHintCache> lsp_inlay_hint_caches;
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
  std::map<char, GlobalMark> global_marks; // global marks ('A'-'Z'), cross-file
  std::string git_root;
  std::string git_branch;
  int git_ahead = 0;  // commits ahead of the upstream branch
  int git_behind = 0; // commits behind the upstream branch
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
  // Git panel (right dock): lazygit-style files / branches / commits / stash
  // views. State and row lists live here so render + input share one model.
  jot_git_panel::State git_panel;
  bool auto_save_enabled;
  int auto_save_interval_ms;
  long long last_auto_save_ms;

  // Discord Rich Presence session (jot/app/discord_session.cpp). The presence
  // is sent when this signature changes (it covers the two text rows and the
  // artwork, so a language switch counts); `discord_status` is the short state
  // the status-line chip and :discord status report.
  std::string discord_last_signature;
  std::string discord_status;
  std::string discord_pattern_error;
  std::string discord_remote_root;
  std::string discord_remote_url;
  long long discord_presence_start_ms = 0;
  long long discord_last_send_ms = 0;
  long long discord_unfocused_since_ms = 0;
  long long discord_remote_fetched_ms = 0;
  bool discord_idle_cleared = false;

  bool show_home_menu;
  int home_menu_selected;
  int home_menu_panel_x;
  int home_menu_panel_y;
  int home_menu_panel_w;
  int home_menu_panel_h;
  std::vector<HomeMenuEntry> home_menu_entries;

  // Cell-based settings menu (:settings, Ctrl+, in GUI mode). Lists every
  // config key (defaults + Lua-registered) with its current value; bools
  // toggle on Enter, ints/strings edit through an inline input row.
  bool show_settings_menu = false;
  int settings_selected = 0;
  int settings_scroll = 0;
  int settings_panel_x = 0;
  int settings_panel_y = 0;
  int settings_panel_w = 0;
  int settings_panel_h = 0;
  std::vector<SettingsEntry> settings_entries;

  bool show_sidebar;
  // True when the *renderer* hid the sidebar because the window was too narrow
  // for it. Growing the window back re-shows it; a sidebar the user closed
  // themselves stays closed.
  bool sidebar_hidden_for_width_ = false;
  // Zen focus mode: sidebar + right panel hidden, status line suppressed and
  // the pane area narrowed to zen_content_width and centered. Sidebar / panel
  // / status-height values are remembered on entry so leaving zen restores
  // the exact pre-zen layout.
  bool zen_mode = false;
  bool zen_saved_sidebar_ = true;
  bool zen_saved_panel_ = false;
  int zen_saved_status_height_ = 2;
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

  LuaAPI *lua_api;
  std::unique_ptr<EditorHostAPI> host_api;
};

#endif
