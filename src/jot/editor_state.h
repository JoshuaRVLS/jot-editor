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
  int pane_root = 0;
  int current_pane = 0;
  int current_buffer = 0;
  PaneLayoutMode pane_layout_mode;

  bool running = false;
  std::string message;
  std::uint64_t transient_message_timer = 0;
  std::uint64_t message_generation = 0;
  std::string clipboard;

  bool show_command_palette = false;
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
  int command_palette_selected = 0;
  bool command_palette_theme_mode = false;
  std::string command_palette_theme_original;

  QuickPickKind quick_pick_kind;
  bool show_quick_pick = false;
  // Scroll anchor of the visible quick-pick window (follow-window, same
  // contract as command_palette_scroll).
  int quick_pick_scroll = 0;
  std::string quick_pick_title;
  std::string quick_pick_query;
  std::vector<QuickPickItem> quick_pick_all_items;
  std::vector<QuickPickItem> quick_pick_items;
  int quick_pick_selected = 0;

  Telescope telescope;

  bool show_search = false;
  std::string search_query;
  std::string search_replace_text;
  std::vector<SearchMatch> search_results;
  int search_result_index = 0;
  bool search_case_sensitive = false;
  bool search_whole_word = false;
  bool search_regex = false;
  bool search_replace_visible = false;
  bool search_focus_replace = false;
  bool search_scoped_to_selection = false;
  Cursor search_scope_start;
  Cursor search_scope_end;

  bool show_save_prompt = false;
  std::string save_prompt_input;
  bool show_quit_prompt = false;
  // Interactive LSP rename: the prompt is seeded with the identifier under the
  // cursor and commits through lsp_rename_symbol.
  // Uninitialised, this decides whether the rename prompt renders from
  // whatever happened to be in memory -- it showed up over a blank editor.
  bool show_rename_prompt = false;
  std::string rename_prompt_input;

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
  bool lua_ui_prev_rename_prompt = false;
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

  bool show_menu_bar_dropdown = false;
  int menu_bar_active = 0;
  int menu_bar_selected = 0;
  std::vector<MenuBarSegment> menu_bar_segments;

  bool show_minimap = false;
  int minimap_width = 0;
  bool show_integrated_terminal = false;
  int integrated_terminal_height = 0;
  // Which view the bottom panel shows, and the Problems view's list cursor.
  // The panel is one dock: both views share its height and its tab strip.
  BottomPanelView bottom_panel_view = BOTTOM_PANEL_TERMINAL;
  int problems_selected = 0;
  int problems_scroll = 0;
  // True while the terminal fills the whole pane area (like pane zoom);
  // panes stay laid out underneath but are covered and their clicks route
  // to the terminal until the zoom is toggled off.
  bool terminal_zoom_active = false;
  // Mouse selection in the integrated terminal: anchors live in full-space
  // row/col coordinates (see IntegratedTerminal::get_total_rows) so they
  // survive scrolls and redraws. active = a selection exists (rendered),
  // dragging = the mouse button is held and motions extend it.
  bool terminal_sel_active = false;
  bool terminal_sel_dragging = false;
  int terminal_sel_anchor_row = 0;
  int terminal_sel_anchor_col = 0;
  int terminal_sel_cur_row = 0;
  int terminal_sel_cur_col = 0;
  bool show_debugger_panel = false;
  int debugger_panel_height = 0;
  bool show_right_panel = false;
  int right_panel_width = 0;
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
  int current_debugger_session = 0;
  std::vector<DebuggerSessionState> debugger_session_state;
  std::map<std::string, std::vector<DebuggerBreakpoint>> debugger_breakpoints;
  std::vector<DebuggerSessionConfig> debugger_configs;
  bool debugger_breakpoint_hover_visible = false;
  int debugger_breakpoint_hover_pane = 0;
  int debugger_breakpoint_hover_buffer = 0;
  int debugger_breakpoint_hover_line = 0;
  std::vector<std::unique_ptr<LSPClient>> lsp_clients;
  std::unordered_map<std::string, long long> lsp_pending_changes;
  std::vector<LspInstallJob> lsp_install_jobs;
  std::set<std::string> lsp_disabled_servers;
  // LSP published diagnostics kept per (server|root -> filepath) so several
  // servers attached to one buffer merge instead of clobbering each other.
  std::map<std::string, std::map<std::string, std::vector<Diagnostic>>> lsp_diag_slices_;
  int current_integrated_terminal = 0;
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

  bool show_tree_sitter_status_modal = false;
  int tree_sitter_status_scroll = 0;
  // Hovered row inside the status modals (mouse motion), -1 when none.
  int tree_sitter_status_hover_row = -1;
  std::vector<TreeSitterInstallJob> tree_sitter_install_jobs;

  bool show_lsp_status_modal = false;
  int lsp_status_scroll = 0;
  int lsp_status_hover_row = -1;

  EventLoop event_loop_;
  std::unique_ptr<TaskQueue> task_queue_;

  int status_height = 0;
  int tab_height = 0;
  int tab_size = 0;
  bool show_indent_guides = false;
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
  bool relative_line_numbers = false;
  bool highlight_cursor_line = false;
  int tab_scroll_index = 0;
  int preview_buffer_index = 0;
  long long last_sidebar_click_ms;
  long long last_git_panel_click_ms = 0;
  int last_git_panel_click_row = -1;
  // Mouse hover tracking (motion events): the model row under the pointer in
  // the git panel and the hovered right-dock tab, -1 when none. Purely
  // visual — selection is never overwritten by hover.
  int git_panel_hover_row = -1;
  int right_panel_hover_tab = -1;
  int last_sidebar_click_row = 0;
  long long last_tab_click_ms;
  int last_tab_clicked_index = 0;
  bool auto_indent = false;
  bool needs_redraw = false;
  bool mouse_selecting = false;
  MouseSelectionMode mouse_selection_mode;
  Cursor mouse_start;
  Cursor mouse_anchor_end;
  int mouse_press_screen_x = 0;
  int mouse_press_screen_y = 0;
  int mouse_press_buf_x = 0;
  int mouse_press_buf_y = 0;
  bool mouse_drag_started = false;
  bool lsp_mouse_hover_enabled = false;
  bool lsp_mouse_hover_pending = false;
  bool lsp_mouse_hover_visible = false;
  long long lsp_mouse_hover_deadline_ms;
  int lsp_mouse_hover_pane = 0;
  int lsp_mouse_hover_buffer = 0;
  int lsp_mouse_hover_line = 0;
  int lsp_mouse_hover_col = 0;
  int lsp_mouse_hover_token_start = 0;
  int lsp_mouse_hover_token_end = 0;
  int lsp_mouse_hover_screen_x = 0;
  int lsp_mouse_hover_screen_y = 0;
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
  bool pane_resize_dragging = false;
  int pane_resize_node = 0;
  bool pane_resize_vertical = false;
  int pane_resize_start_pos = 0;
  float pane_resize_start_ratio = 0;
  bool pane_zoom_active; // true while one pane is expanded over the others
  int pane_zoom_pane;    // pane index expanded while zoomed (-1 when off)
  // Dragging the terminal panel's top border resizes its height live.
  bool terminal_resize_dragging = false;
  int terminal_resize_start_y = 0;
  int terminal_resize_start_height = 0;
  bool sidebar_resize_dragging = false;
  bool sidebar_resize_opening = false;
  int sidebar_resize_start_x = 0;
  int sidebar_resize_start_width = 0;
  bool right_panel_resize_dragging = false;
  int right_panel_resize_start_x = 0;
  int right_panel_resize_start_width = 0;
  bool scrollbar_dragging = false;
  int scrollbar_drag_pane = 0;
  int scrollbar_drag_start_y = 0;
  int scrollbar_drag_start_scroll = 0;
  int scrollbar_drag_track_y = 0;
  int scrollbar_drag_track_h = 0;
  int scrollbar_drag_thumb_h = 0;
  int scrollbar_drag_max_scroll = 0;
  long long last_left_click_ms;
  Cursor last_left_click_pos;
  int last_left_click_count = 0;

  int render_fps = 0;
  int idle_fps = 0;
  int lsp_change_debounce_ms = 0;
  int last_cursor_shape = 0;
  bool smart_paste_indent = false;
  long long keyboard_press_count;

  // One software blink clock for the terminal cursor and the extra-caret
  // highlights: anchor in steady-clock ms, a suspension window (input keeps
  // the cursor solid for a moment), and the effective visibility applied to
  // both the cursor and the caret paint.
  long long blink_anchor_ms;
  long long blink_suspend_until_ms;
  bool blink_visible = false;

  bool show_context_menu = false;
  ContextMenuSurface context_menu_surface;
  std::vector<ContextMenuItem> context_menu_items;
  int context_menu_x = 0;
  int context_menu_y = 0;
  int context_menu_w = 0;
  int context_menu_h = 0;
  int context_menu_selected = 0;
  int context_menu_target_buffer = 0;
  int context_menu_target_pane = 0;
  int context_menu_target_terminal = 0;
  int context_menu_target_line = 0;
  std::string context_menu_target_path;
  bool context_menu_target_is_dir = false;

  bool lsp_completion_visible = false;
  bool lsp_completion_manual_request = false;
  int lsp_completion_selected = 0;
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
  bool lsp_signature_visible = false;
  // Position of the '(' that opened the call shown by the signature popup.
  // While the caret stays past this paren on the same line the popup keeps
  // tracking the call.
  int lsp_signature_open_paren_line = 0;
  int lsp_signature_open_paren_col = 0;
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
  // Go-to-definition arms its landing spot and applies it inside open_file, the
  // same way a jumplist restore does.
  bool lsp_definition_jump_pending = false;
  LSPLocation lsp_definition_pending_location;
  // Navigation history. Every jump records where it landed, so back/forward
  // walk the places the cursor has been rather than one LSP-only stack: the
  // history has a cursor of its own (`jump_index`), and a new jump drops
  // whatever was ahead of it, the way a new branch replaces a redo tail.
  std::vector<JumpLocation> jump_history;
  int jump_index = -1;
  // Set while a restore is in flight: restoring moves the cursor too, and that
  // must not be recorded as a new jump.
  bool jump_restoring = false;
  // A location waiting to be restored. The cursor can only be placed once the
  // file is open, and open_file can finish asynchronously, so the jump is armed
  // here and applied from open_file (and again by its caller, which is
  // idempotent -- the flag is cleared on apply).
  bool jump_pending = false;
  JumpLocation jump_pending_location;

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
  int git_dirty_count = 0;
  int git_staged_count = 0;
  int git_unstaged_count = 0;
  int git_untracked_count = 0;
  int git_deleted_count = 0;
  int git_renamed_count = 0;
  int git_conflict_count = 0;
  std::atomic<bool> git_refresh_pending_{false};
  std::unordered_map<std::string, std::string> git_file_status;
  long long git_last_refresh_ms;
  // Git panel (right dock): lazygit-style files / branches / commits / stash
  // views. State and row lists live here so render + input share one model.
  jot_git_panel::State git_panel;
  bool auto_save_enabled = false;
  int auto_save_interval_ms = 0;
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

  bool show_home_menu = false;
  int home_menu_selected = 0;
  int home_menu_panel_x = 0;
  int home_menu_panel_y = 0;
  int home_menu_panel_w = 0;
  int home_menu_panel_h = 0;
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

  bool show_sidebar = false;
  // Activity bar: the icon rail down the left edge that would switch which view
  // the primary sidebar shows. It is off, so the sidebar is a plain file
  // explorer -- no rail, the view pinned to the explorer, and Tab inert. The
  // git panel lives in the secondary sidebar (the right dock) instead.
  bool show_activity_bar = false;
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
  int sidebar_width = 0;
  std::string root_dir;
  bool workspace_session_enabled = false;
  std::string workspace_session_root;
  std::vector<FileNode> file_tree;
  int file_tree_selected = 0;
  int file_tree_scroll = 0;
  int git_sidebar_selected = 0;
  int git_sidebar_scroll = 0;
  bool sidebar_show_hidden = false;
  std::string file_tree_watch_signature_;
  bool file_tree_watch_ready_ = false;
  std::string file_tree_event_watch_root_;
  SidebarRenderCache sidebar_render_cache_;

  EditorFocus focus_state;
  std::vector<int> recent_keys;
  int easter_egg_timer = 0;

  LuaAPI *lua_api;
  std::unique_ptr<EditorHostAPI> host_api;
};

#endif
