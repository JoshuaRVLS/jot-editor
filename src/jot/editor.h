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

class LuaAPI;
class EditorHostAPI;
class HostCoreAPI;
class HostRenderAPI;
class HostIOAPI;
class UIGui;
struct SidePanelView; // defined in jot/lua/api.h (included by renderers)

class Editor : private EditorState
{
  friend class LuaAPI;
  friend class EditorHostAPI;
  friend class HostCoreAPI;
  friend class HostRenderAPI;
  friend class HostIOAPI;

private:
  // Temporary presentation gates. Keep underlying features intact while the
  // compact editor layout is evaluated.
  static constexpr bool kTopBarVisible = false;
  static constexpr bool kExplorerOnly = true;

  int topbar_height() const
  {
    return kTopBarVisible ? 1 : 0;
  }

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
  static constexpr QuickPickKind QUICK_PICK_PROJECT_SEARCH = ::QUICK_PICK_PROJECT_SEARCH;
  static constexpr QuickPickKind QUICK_PICK_DIAGNOSTICS = ::QUICK_PICK_DIAGNOSTICS;
  static constexpr QuickPickKind QUICK_PICK_SYMBOLS = ::QUICK_PICK_SYMBOLS;
  static constexpr QuickPickKind QUICK_PICK_PLUGIN = ::QUICK_PICK_PLUGIN;
  static constexpr QuickPickKind QUICK_PICK_FONT = ::QUICK_PICK_FONT;

  static constexpr MenuBarAction MENU_ACTION_NONE = ::MENU_ACTION_NONE;
  static constexpr MenuBarAction MENU_ACTION_COMMAND = ::MENU_ACTION_COMMAND;
  static constexpr MenuBarAction MENU_ACTION_NEW_FILE = ::MENU_ACTION_NEW_FILE;
  static constexpr MenuBarAction MENU_ACTION_OPEN_FINDER = ::MENU_ACTION_OPEN_FINDER;
  static constexpr MenuBarAction MENU_ACTION_SAVE = ::MENU_ACTION_SAVE;
  static constexpr MenuBarAction MENU_ACTION_SAVE_AS = ::MENU_ACTION_SAVE_AS;
  static constexpr MenuBarAction MENU_ACTION_CLOSE_FILE = ::MENU_ACTION_CLOSE_FILE;
  static constexpr MenuBarAction MENU_ACTION_QUIT = ::MENU_ACTION_QUIT;
  static constexpr MenuBarAction MENU_ACTION_UNDO = ::MENU_ACTION_UNDO;
  static constexpr MenuBarAction MENU_ACTION_REDO = ::MENU_ACTION_REDO;
  static constexpr MenuBarAction MENU_ACTION_CUT = ::MENU_ACTION_CUT;
  static constexpr MenuBarAction MENU_ACTION_COPY = ::MENU_ACTION_COPY;
  static constexpr MenuBarAction MENU_ACTION_PASTE = ::MENU_ACTION_PASTE;
  static constexpr MenuBarAction MENU_ACTION_SELECT_ALL = ::MENU_ACTION_SELECT_ALL;
  static constexpr MenuBarAction MENU_ACTION_SELECT_LINE = ::MENU_ACTION_SELECT_LINE;
  static constexpr MenuBarAction MENU_ACTION_DUPLICATE_LINE = ::MENU_ACTION_DUPLICATE_LINE;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_UP = ::MENU_ACTION_MOVE_LINE_UP;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_DOWN = ::MENU_ACTION_MOVE_LINE_DOWN;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_COMMENT = ::MENU_ACTION_TOGGLE_COMMENT;
  static constexpr MenuBarAction MENU_ACTION_COMMAND_PALETTE = ::MENU_ACTION_COMMAND_PALETTE;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_SIDEBAR = ::MENU_ACTION_TOGGLE_SIDEBAR;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_MINIMAP = ::MENU_ACTION_TOGGLE_MINIMAP;
  static constexpr MenuBarAction MENU_ACTION_THEME = ::MENU_ACTION_THEME;
  static constexpr MenuBarAction MENU_ACTION_HOME = ::MENU_ACTION_HOME;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_TERMINAL = ::MENU_ACTION_TOGGLE_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_NEW_TERMINAL = ::MENU_ACTION_NEW_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_TERMINAL_ZOOM = ::MENU_ACTION_TERMINAL_ZOOM;
  static constexpr MenuBarAction MENU_ACTION_TASKS = ::MENU_ACTION_TASKS;
  static constexpr MenuBarAction MENU_ACTION_RERUN_TASK = ::MENU_ACTION_RERUN_TASK;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_DEBUG_PANEL = ::MENU_ACTION_TOGGLE_DEBUG_PANEL;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STOP = ::MENU_ACTION_DEBUG_STOP;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_CONTINUE = ::MENU_ACTION_DEBUG_CONTINUE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_PAUSE = ::MENU_ACTION_DEBUG_PAUSE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_IN = ::MENU_ACTION_DEBUG_STEP_IN;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OVER = ::MENU_ACTION_DEBUG_STEP_OVER;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OUT = ::MENU_ACTION_DEBUG_STEP_OUT;
  static constexpr MenuBarAction MENU_ACTION_LSP_DEFINITION = ::MENU_ACTION_LSP_DEFINITION;
  static constexpr MenuBarAction MENU_ACTION_LSP_BACK = ::MENU_ACTION_LSP_BACK;
  static constexpr MenuBarAction MENU_ACTION_HELP = ::MENU_ACTION_HELP;

  static constexpr RightPanelTab RIGHT_PANEL_DEBUG = ::RIGHT_PANEL_DEBUG;
  static constexpr RightPanelTab RIGHT_PANEL_GIT_DIFF = ::RIGHT_PANEL_GIT_DIFF;
  static constexpr RightPanelTab RIGHT_PANEL_SYMBOLS = ::RIGHT_PANEL_SYMBOLS;
  static constexpr RightPanelTab RIGHT_PANEL_PLUGIN = ::RIGHT_PANEL_PLUGIN;

  static constexpr MouseSelectionMode MOUSE_SELECT_CHAR = ::MOUSE_SELECT_CHAR;
  static constexpr MouseSelectionMode MOUSE_SELECT_WORD = ::MOUSE_SELECT_WORD;
  static constexpr MouseSelectionMode MOUSE_SELECT_LINE = ::MOUSE_SELECT_LINE;

  static constexpr ContextMenuSurface CONTEXT_MENU_NONE = ::CONTEXT_MENU_NONE;
  static constexpr ContextMenuSurface CONTEXT_MENU_EDITOR = ::CONTEXT_MENU_EDITOR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TAB = ::CONTEXT_MENU_TAB;
  static constexpr ContextMenuSurface CONTEXT_MENU_SIDEBAR = ::CONTEXT_MENU_SIDEBAR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TERMINAL = ::CONTEXT_MENU_TERMINAL;

  static constexpr ContextMenuAction CONTEXT_ACTION_NONE = ::CONTEXT_ACTION_NONE;
  static constexpr ContextMenuAction CONTEXT_ACTION_COPY = ::CONTEXT_ACTION_COPY;
  static constexpr ContextMenuAction CONTEXT_ACTION_CUT = ::CONTEXT_ACTION_CUT;
  static constexpr ContextMenuAction CONTEXT_ACTION_PASTE = ::CONTEXT_ACTION_PASTE;
  static constexpr ContextMenuAction CONTEXT_ACTION_SAVE_BUFFER = ::CONTEXT_ACTION_SAVE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_CLOSE_BUFFER = ::CONTEXT_ACTION_CLOSE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_OPEN = ::CONTEXT_ACTION_SIDEBAR_OPEN;
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
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE = ::CONTEXT_ACTION_GIT_STAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_UNSTAGE = ::CONTEXT_ACTION_GIT_UNSTAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF = ::CONTEXT_ACTION_GIT_DIFF;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF_STAGED =
      ::CONTEXT_ACTION_GIT_DIFF_STAGED;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE_ALL = ::CONTEXT_ACTION_GIT_STAGE_ALL;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_REFRESH = ::CONTEXT_ACTION_GIT_REFRESH;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_FOCUS =
      ::CONTEXT_ACTION_TERMINAL_FOCUS;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_NEW = ::CONTEXT_ACTION_TERMINAL_NEW;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_CLOSE =
      ::CONTEXT_ACTION_TERMINAL_CLOSE;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_RESET_SCROLL =
      ::CONTEXT_ACTION_TERMINAL_RESET_SCROLL;
  static constexpr ContextMenuAction CONTEXT_ACTION_TOGGLE_FOLD = ::CONTEXT_ACTION_TOGGLE_FOLD;

  static constexpr SidebarView SIDEBAR_VIEW_EXPLORER = ::SIDEBAR_VIEW_EXPLORER;
  static constexpr SidebarView SIDEBAR_VIEW_GIT = ::SIDEBAR_VIEW_GIT;

  static constexpr EditorFocus FOCUS_EDITOR = ::FOCUS_EDITOR;
  static constexpr EditorFocus FOCUS_SIDEBAR = ::FOCUS_SIDEBAR;

  void render();
  void render_tabs();
  void render_panes();
  void render_pane_resize_guides();
  void render_easter_egg();
  void render_pane(const SplitPane &pane, int pane_index);
  FileTabLayout build_file_tab_layout(const SplitPane &pane, int draw_w);
  int find_local_tab_index(const SplitPane &pane, int buffer_id) const;
  void clamp_tab_scroll(SplitPane &pane);
  void reveal_local_tab(SplitPane &pane, int target_index, int draw_w);
  bool scroll_local_tabs(SplitPane &pane, int delta);
  bool switch_to_local_tab(int target_index);
  bool cycle_local_tab(int delta);
  void render_telescope();
  void render_minimap(int x, int y, int w, int h, int buffer_id);
  void render_image_viewer();
  void render_integrated_terminal();
  void render_debugger_panel();
  void render_git_diff_panel();
  void render_git_panel();
  void render_outline_panel();
  bool outline_active() const
  {
    return show_right_panel && active_right_panel_tab == RIGHT_PANEL_SYMBOLS;
  }
  void toggle_outline_panel();
  void close_outline_panel();
  void note_outline_edit();
  void ensure_outline_fresh(bool force = false);
  void outline_move_selection(int delta);
  void outline_jump_selected();
  void render_plugin_panel();
  int effective_right_panel_width() const;
  void render_menu_bar();
  void render_menu_dropdown();
  void render_status_line();
  void render_command_palette();
  void render_quick_pick();
  // Which-key style helper for multi-chord plugin keymaps ("Ctrl+T N"):
  // open_which_key shows the next-chord options for a pressed prefix chord,
  // handle_which_key_input advances/runs/closes, and the panel renders above
  // the status line (see ui.cpp / event_loop.cpp).
  void open_which_key(const std::string &chord);
  void close_which_key();
  bool handle_which_key_input(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch);
  void render_which_key_panel();
  void sync_lua_ui_surfaces();
  void render_search_panel();
  void place_command_palette_cursor();
  void place_search_cursor();
  void render_context_menu();
  void render_tree_sitter_status_modal();
  void render_save_prompt();
  void place_save_prompt_cursor();
  void render_quit_prompt();
  void render_popup();
  void render_home_menu();
  // Cell-based settings menu (:settings / Ctrl+, in GUI mode): a quick-
  // pick style panel listing every config key with its value. Bools toggle
  // on Enter; ints/strings edit inline. Lua-registered config keys appear
  // automatically (the menu enumerates config.keys()).
  void render_settings_menu();
  void place_settings_cursor();
  void toggle_settings_menu();
  void close_settings_menu();
  void rebuild_settings_entries();
  bool handle_settings_input(int ch);
  bool handle_settings_mouse(int x, int y, bool is_click);
  void render_buffer_content(const SplitPane &pane, int pane_index, int buffer_id);
  // The regions that share a separator with this pane: the other visible panes
  // (skipping the ones zoom hides), the sidebar when it is up, the right dock,
  // and whatever occupies the rows below the pane area. Used to decide which
  // sides of the pane's box get inked (see render/pane_edges.h).
  std::vector<UIRect> pane_neighbours(const SplitPane &pane, int draw_w) const;
  // The right dock's box sides. Nothing lies to its right and the panes own the
  // separator on its left, so only the status-line edge below it gets ink.
  UIBorderEdges right_dock_edges(const UIRect &panel) const;
  // GUI smooth-scroll tracking: last reported first-visible line per pane,
  // so the fold-aware delta for the scroll animation is computed once per
  // pane per frame (editor side, where the fold ranges live). gui_pane_scroll_xs_
  // is the same per pane for the horizontal window: it has no slide animation,
  // so the GUI uses the change to place the caret instead of easing it.
  std::vector<int> gui_pane_top_lines_;
  std::vector<int> gui_pane_scroll_xs_;
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
  void poll_discord_rpc(long long now_ms);
  // Applies one new grid size: re-dimensions the UI (which schedules a single
  // full repaint), re-fits the panes and notifies Lua. Resize bursts are
  // coalesced before this is called (see the input drains), so a live drag does
  // one relayout per wake instead of one per compositor step.
  void apply_resize(int cols, int rows);
  // Focus reporting (DECSET 1004 in terminals, SDL window events in the GUI):
  // leaving the window starts the idle clock that can clear the presence.
  // `now_ms` comes from jot_discord::monotonic_ms(), the same clock the poll
  // uses, so the two can never disagree (and tests can drive both).
  void discord_note_focus(bool focused, long long now_ms);
  // Backing of the `:discord` command (enable/disable/reconnect/disconnect/
  // status); returns the message it reported.
  std::string discord_command(const std::string &argument);
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
  bool handle_quick_pick_input(int ch);
  void open_quick_pick(QuickPickKind kind,
                       const std::string &title,
                       std::vector<QuickPickItem> items,
                       const std::string &query = "");
  void close_quick_pick();
  void refresh_quick_pick();
  int quick_pick_match_score(const std::string &query, const QuickPickItem &item) const;
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
  void request_lsp_signature_help(char trigger_character = '\0');
  // Re-fires signature help when the caret sits inside an open call's argument
  // list, even when the popup is not up yet (e.g. auto-close already inserted
  // the closing ')' and the user is typing the first argument). No-op when the
  // caret is outside any open call.
  void refresh_lsp_signature_if_in_call();
  void hide_lsp_signature();
  void request_lsp_hover();
  void request_lsp_hover_at(int pane_index,
                            int buffer_id,
                            const Cursor &pos,
                            int token_start,
                            int token_end,
                            int screen_x,
                            int screen_y);
  void cancel_lsp_mouse_hover(bool hide_popup = true);
  void maybe_fire_lsp_mouse_hover();
  // Dismisses a Lua-rendered hover float (notifies the jot.lsp.hover_ui
  // handler); no-op when the Lua hover UI is not registered.
  void close_lua_hover_ui();
  void request_lsp_definition();
  void lsp_rename_symbol(const std::string &new_name);
  void request_lsp_references();
  void handle_lsp_references_results();
  void request_lsp_code_actions();
  void handle_lsp_code_action_results();
  bool apply_selected_lsp_code_action();
  void handle_lsp_hover_result(const LSPHoverResult &hover);
  void handle_lsp_signature_result(const LSPSignatureHelpResult &signature_help);
  void handle_lsp_definition_result(const LSPDefinitionResult &definition);
  bool apply_pending_lsp_definition_jump();
  bool apply_pending_lsp_back_jump();
  void return_from_lsp_definition();
  void hide_lsp_completion();
  bool refresh_lsp_completion_filter();
  void update_lsp_completion_ghost();
  bool apply_selected_lsp_completion();
  void accept_telescope_selection();
  void render_lsp_completion();
  void render_lsp_signature();
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
  // Drains finished background parses and installs them into their buffers
  // (main thread only; see init_ts_for_buffer for the queue side).
  void install_finished_parses();
  int buffer_index_of(const FileBuffer &buf) const;
#endif

  void handle_input(int ch,
                    bool is_ctrl = false,
                    bool is_shift = false,
                    bool is_alt = false,
                    int original_ch = 0);
  void handle_mouse_input(int x,
                          int y,
                          bool is_click,
                          bool is_scroll_up,
                          bool is_scroll_down,
                          bool is_scroll_left = false,
                          bool is_scroll_right = false);

  void handle_modeless_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  void handle_command_palette(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  // Mouse for the command palette / quick pick / search panel: hover selects
  // a row (follow-window keeps the list put), click activates it, wheel
  // moves the selection. Returns true when the event was consumed.
  bool handle_palette_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
  bool handle_quick_pick_mouse(int x, int y, bool is_click, bool is_scroll_up, bool is_scroll_down);
  bool handle_search_mouse(int x, int y, bool is_click);
  bool execute_ex_command(const std::string &line);
  bool
  execute_ex_command_tail(const std::string &lcmd, const std::string &arg, const std::string &line);
  void show_command_help(const std::string &topic);
  void submit_command_palette();
  void
  handle_search_panel(int ch, bool is_ctrl = false, bool is_shift = false, bool is_alt = false);
  void handle_telescope(int ch);
  void handle_save_prompt(int ch);
  void handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_telescope_mouse(
      int x, int y, bool is_click, bool is_double_click, bool is_scroll_up, bool is_scroll_down);
  bool handle_home_menu_input(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool handle_home_menu_mouse(int x, int y, bool is_click);
  // Applies one settings-menu change through the normal config pipeline:
  // config.set + apply_config_live() + save, plus GUI font-size handling.
  void apply_settings_value(const std::string &key, const std::string &value);
  bool handle_menu_bar_input(int ch);
  bool handle_menu_bar_mouse(int x, int y, bool is_click, bool is_motion);
  bool handle_integrated_terminal_mouse(
      int x, int y, bool is_click, bool is_motion, bool is_click_release);
  bool handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down);
  // Mouse selection in the integrated terminal: anchors live in full-space
  // row/col coordinates (see IntegratedTerminal::get_total_rows) so they
  // survive scrolls and redraws.
  void begin_terminal_selection(int x, int y);
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
  void handle_mouse(void *event);

  void move_cursor(int dx, int dy, bool extend_selection = false);
  bool insert_char(char c);
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

public:
  // Hint cells inserted before `byte_col` on `line` (text shift amount), for
  // caret placement, popup anchoring, and mouse mapping. Pure query helpers
  // shared with the renderer and input layers (and the test suite).
  int lsp_inlay_hint_cells_before(const std::string &filepath,
                                  int line,
                                  int byte_col,
                                  const std::string &line_text);
  // (shifted screen column, cell width) pairs for every hint on `line`,
  // mirroring the renderer's layout (used to un-shift mouse clicks).
  std::vector<std::pair<int, int>> lsp_inlay_hints_visual(const std::string &filepath,
                                                          int line,
                                                          const std::string &line_text,
                                                          int tab_size);
  // The modeless Tab / Shift+Tab actions. Exposed so keymaps that shadow
  // those chords (the bundled snippet engine) can fall back to exactly the
  // editor's own behavior instead of re-deriving it in Lua.
  void apply_default_tab();
  void apply_default_shift_tab();
  void toggle_sidebar();
  // Drops the integrated-terminal mouse selection (panel close, terminal
  // close, and the test suite).
  void clear_terminal_selection();
  bool zen_active() const
  {
    return zen_mode;
  }
  bool right_panel_visible() const
  {
    return show_right_panel;
  }
  // Right dock (VSCode-style tabbed sidebar): panels open as tabs
  // (git, git diff, symbols, debug, plugin). Ctrl+Shift+B toggles the dock;
  // the tab strip at the top switches panels and closes individual tabs.
  void toggle_right_panel();
  void open_right_panel_tab(RightPanelTab tab);
  void close_right_panel_tab(RightPanelTab tab);
  bool right_panel_tab_open(RightPanelTab tab) const;
  // Right-dock state for headless tests (private EditorState; tests read
  // the tab list and active tab through these).
  const std::vector<RightPanelTab> &right_panel_tabs_for_test() const
  {
    return right_panel_tabs;
  }
  RightPanelTab active_right_panel_tab_for_test() const
  {
    return active_right_panel_tab;
  }
  // Headless tests: enables the workspace session subsystem for a fake root
  // so save_workspace_session / restore_workspace_session can round-trip
  // through a JOT_CONFIG_HOME-backed session file.
  void enable_workspace_session_for_test(const std::string &root)
  {
    workspace_session_enabled = true;
    workspace_session_root = root;
  }
  void save_workspace_session_for_test()
  {
    save_workspace_session();
  }
  bool restore_workspace_session_for_test()
  {
    return restore_workspace_session();
  }
  int status_line_height() const
  {
    return status_height;
  }
  // Zen focus mode: hides the sidebar and right panel, suppresses the status
  // line and centers the pane area at zen_content_width. Returns the new
  // state (true = zen on). Layout-affecting; safe with no panes / no ui.
  bool toggle_zen_mode();
  // Left/right margin that centers the pane area at zen_content_width while
  // zen mode is active (0 otherwise or when the area is narrower).
  int zen_content_margin(int available_w);
  void load_file_tree(const std::string &path);
  void open_workspace(const std::string &path, bool restore_session = true);
  // Test seam: the computed explorer/git sidebar rows (rebuilding the cache
  // on demand so callers never see a stale tree).
  const SidebarRenderCache &sidebar_render_cache()
  {
    ensure_sidebar_render_cache();
    return sidebar_render_cache_;
  }
  bool resume_last_workspace_session();
  void set_home_menu_visible(bool visible);

private:
  void handle_sidebar_input(int ch);
  void handle_sidebar_mouse(int x, int y, bool is_click, bool is_double_click = false);
  void render_sidebar();
  void render_collapsed_sidebar_handle();
  // The cell grid the editor is drawing into. Always use these for layout and
  // hit-testing, never the Terminal's size: the terminal object only exists in
  // terminal mode and keeps its 80x24 constructor default under --gui, so
  // reading it there silently clamps geometry (and clicks) to 24 rows.
  int grid_height() const
  {
    return ui ? ui->get_height() : 0;
  }
  int grid_width() const
  {
    return ui ? ui->get_render_width() : 0;
  }
  int sidebar_activity_rail_width() const
  {
    return 5;
  }
  int min_sidebar_width() const
  {
    return sidebar_activity_rail_width() + 18;
  }
  int sidebar_close_threshold() const
  {
    return 12;
  }
  int max_sidebar_width() const;
  int effective_sidebar_width() const;
  bool collapsed_sidebar_handle_hit_test(int x, int y) const;
  bool sidebar_resize_hit_test(int x, int y) const;
  bool begin_sidebar_resize_drag(int x, int y);
  bool update_sidebar_resize_drag(int x);
  void end_sidebar_resize_drag();
  int min_right_panel_width() const
  {
    return 28;
  }
  int max_right_panel_width() const;
  bool right_panel_resize_hit_test(int x, int y) const;
  bool begin_right_panel_resize_drag(int x, int y);
  bool update_right_panel_resize_drag(int x);
  void end_right_panel_resize_drag();
  void build_tree(const std::string &path, std::vector<FileNode> &nodes, int depth);
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

  void toggle_minimap();
  void toggle_integrated_terminal();
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
  bool begin_terminal_resize_drag(int x, int y);
  bool update_terminal_resize_drag(int y);
  void end_terminal_resize_drag();
  void close_integrated_terminal(int index);
  void activate_integrated_terminal(int index, bool focus = true);
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
  void toggle_search();
  void open_search();
  bool open_scoped_replace_from_selection();
  void toggle_command_palette();
  void open_command_palette(const std::string &query);
  void open_theme_chooser();
  void execute_command(const std::string &cmd);

  void find_next();
  void find_prev();
  void perform_search();
  void clear_search_scope();
  bool replace_current_search_match();
  bool replace_all_search_matches();
  // Root used when launching the telescope file finder: the workspace root
  // when one is open, otherwise the git/project root detected from the
  // current file's directory (falling back to the process cwd).
  std::string telescope_launch_root() const;
  void refresh_folds(FileBuffer &buf);
  bool toggle_fold_at_line(FileBuffer &buf, int line);
  bool fold_at_cursor();
  bool unfold_at_cursor();
  bool toggle_fold_at_cursor();
  void fold_all();
  void unfold_all();
  bool is_line_hidden_by_fold(const FileBuffer &buf, int line) const;
  int buffer_line_for_visible_row(const FileBuffer &buf, int first_line, int row) const;

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
  void equalize_panes();
  void toggle_pane_zoom();
  void swap_panes();
  bool pane_zoomed() const
  {
    return pane_zoom_active;
  }
  // Per-pane views: each pane keeps its own cursor/scroll/selection for the
  // buffer it shows, so two panes can display one buffer independently.
  void capture_pane_view(int pane_index);
  void restore_pane_view(int pane_index);
  void activate_pane(int pane_index);
  void pane_show_buffer(int buffer_index);
  bool resize_current_pane(int delta);
  bool resize_current_pane_direction(char dir, int delta);
  int pane_split_at_position(int x, int y) const;
  bool begin_pane_resize_drag(int x, int y);
  bool update_pane_resize_drag(int x, int y);
  void end_pane_resize_drag();
  bool is_pane_resize_dragging() const
  {
    return pane_resize_dragging;
  }
  bool pane_split_is_resizing(int node_index) const
  {
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
  void replace_all_text(const std::string &needle,
                        const std::string &replacement,
                        bool case_sensitive = true,
                        bool whole_word = false);
  void replace_all_regex(const std::string &pattern, const std::string &replacement);
  bool surround_selection_or_word(const std::string &left, const std::string &right);
  bool unsurround_selection_or_cursor();
  bool change_inside_quote(char quote);
  void increment_number_at_cursor(int delta);
  void toggle_auto_indent_setting();
  void change_tab_size(int delta);
  std::vector<std::string> list_available_themes();
  // Applies a theme; when persist is set, also saves it to the config file so
  // it survives the next session. Returns true on success.
  bool apply_theme(const std::string &name, bool persist = true, bool announce = true);
  int detect_indent_width(const std::vector<std::string> &lines) const;

  FileBuffer &get_buffer(int id = -1);
  SplitPane &get_pane(int id = -1);
  std::string get_file_extension(const std::string &path);
  std::string get_filename(const std::string &path);
  Theme &get_theme()
  {
    return theme;
  }
  IntegratedTerminal *get_integrated_terminal(int index = -1);

  void load_runtime_config();
  void initialize_state_defaults();
  void initialize_lua_runtime();
  void initialize_terminal_ui();
  // --- Discord presence helpers (jot/app/discord_session.cpp) ---
  // Config::get*() are non-const, so these read config and stay non-const too.
  bool discord_workspace_excluded();
  const std::string &discord_repository_remote();
  long long discord_buffer_size(const FileBuffer &buf);
  long long discord_error_count(const std::string &filepath);
  jot_discord::TemplateContext discord_template_context();
  jot_discord::PresenceState discord_presence_state();
  jot_discord::PresenceOptions discord_presence_options();
  // Sets the status the status-line chip reports and repaints when it actually
  // changed: the poll runs on a timer, so without this the chip would only
  // appear on the next unrelated repaint (which, in an idle editor, may never
  // come).
  void discord_set_status(const std::string &status);
  // GUI frontend (jot --gui): builds the SDL3/OpenGL UIGui and the initial
  // pane. Throws std::runtime_error when the display or font is missing.
  void initialize_gui_ui();
  void initialize_placeholder_buffer();
  // Re-reads every config key that maps to live editor state and applies it
  // immediately (no restart). Idempotent; called after Lua config/plugin load
  // and by reload_config().
  void apply_config_live();

  int create_pane(int x, int y, int w, int h, int buffer_id);
  void update_pane_layout();
  void split_pane_direction(int dx, int dy);
  void refresh_command_palette();
  // Lists every installed fixed-width family and applies the chosen one.
  void open_font_picker();
  // Switches the GUI to `family` (empty = the built-in font), persists it and
  // reports the result. False means nothing changed: either this frontend has
  // no font to change, or the name matched no installed family. The reason is
  // left in the statusline either way, so callers need not explain it again.
  //
  // Kept here rather than letting callers reach into UIGui: the GUI header
  // needs the GL loader, which the input layer does not link.
  bool apply_gui_font_family(const std::string &family);
  // The family in use, empty when the built-in font is loaded.
  std::string gui_font_family_name() const;
  // Installed fixed-width families, sorted. Reads the font directories, so it
  // is a picker-time call, not a per-frame one.
  std::vector<std::string> gui_font_families() const;
  // DEPRECATED: statusline message channel. Kept for compatibility; toasts
  // (runtime/lua/features/ui/toast.lua) are the message surface now — these still
  // feed them via the native bridge. `toast=false` shows the statusline
  // message without surfacing a toast (used for noisy open/close news).
  void set_message(const std::string &msg, bool toast = true);
  void set_transient_message(const std::string &msg, int duration_ms = 5000, bool toast = true);
  bool close_active_floating_ui();

public:
  // gui_mode selects the SDL3/OpenGL frontend (jot --gui) over the terminal
  // backend; the editor logic is identical either way.
  Editor(bool gui_mode = false);
  ~Editor();
  bool multicursor_active();
  void clear_extra_carets();
  bool add_caret_at(int line_y, int x);
  bool select_next_occurrence();
  void delete_selection_for_test();
  void delete_char_for_test(bool forward);
  void insert_string_for_test(const std::string &str);
  // Seeds the per-file inlay-hint cache directly (sorted on ingest like a
  // real server answer), so coordinate helpers can be unit-tested headless.
  void set_inlay_hints_for_test(const std::string &filepath, std::vector<LSPInlayHint> hints);
  // Headless mouse driver for tests: feeds a synthetic mouse event through
  // the real handle_mouse path (pane hit-test, selection, edge-panning).
  void mouse_event_for_test(int x, int y, int bstate);
  void mouse_event_for_test(int x, int y, int bstate, bool ctrl);
  void create_new_buffer_for_test();
  void move_to_line_start_for_test();
  void render_for_test();
  // One full frame-loop step (the per-frame blink/scheduling logic plus a
  // render), for tests that need to observe behaviour over time.
  void render_frame_for_test();
  FileBuffer &buffer_for_test(int id = -1);
  SplitPane &pane_for_test(int id = -1);
  // Settings-menu state accessors for headless tests (the menu surface is
  // private EditorState; tests drive it through these + handle_settings_input).
  bool settings_menu_open_for_test() const
  {
    return show_settings_menu;
  }
  const std::vector<SettingsEntry> &settings_entries_for_test() const
  {
    return settings_entries;
  }
  void settings_select_for_test(int index)
  {
    settings_selected = index;
  }
  std::string config_value_for_test(const std::string &key)
  {
    return config.get(key, "");
  }
  int config_int_for_test(const std::string &key)
  {
    return config.get_int(key, 0);
  }
  // Feeds one key through the settings menu's real input handler.
  bool settings_input_for_test(int ch)
  {
    return handle_settings_input(ch);
  }
  // Terminal panel state for headless tests: the integrated-terminal
  // fields are private EditorState, so tests configure them directly to
  // exercise the panel geometry / resize-drag math without spawning a
  // shell.
  void set_terminal_state_for_test(bool show, bool zoom, int height)
  {
    show_integrated_terminal = show;
    terminal_zoom_active = zoom;
    integrated_terminal_height = std::max(5, height);
    update_pane_layout();
  }
  bool terminal_zoom_active_for_test() const
  {
    return terminal_zoom_active;
  }
  int terminal_panel_h_for_test() const
  {
    return integrated_terminal_panel_h();
  }
  int terminal_panel_y_for_test() const
  {
    return integrated_terminal_panel_y();
  }
  int terminal_panel_w_for_test() const
  {
    return integrated_terminal_panel_w();
  }
  // Sidebar panel height as render_sidebar() computes it: the pane area
  // minus the terminal's real reserved footprint.
  int sidebar_panel_h_for_test() const
  {
    if (!ui)
    {
      return 0;
    }
    return std::max(
        0, ui->get_height() - status_height - topbar_height() - integrated_terminal_reserved_h());
  }
  bool terminal_resize_dragging_for_test() const
  {
    return terminal_resize_dragging;
  }
  // Feeds the terminal top-border drag through the real private handlers.
  bool terminal_resize_begin_for_test(int x, int y)
  {
    return begin_terminal_resize_drag(x, y);
  }
  bool terminal_resize_update_for_test(int y)
  {
    return update_terminal_resize_drag(y);
  }
  void terminal_resize_end_for_test()
  {
    end_terminal_resize_drag();
  }
  int ui_width_for_test() const
  {
    return grid_width();
  }
  int ui_height_for_test() const
  {
    return grid_height();
  }
  // The active theme, for tests that assert on painted colors.
  const Theme &theme_for_test() const
  {
    return theme;
  }
  // Absolute bracket depth at the start of `line` (the same value the renderer
  // seeds a scrolled-to line with), so tests can assert that painted rainbow
  // colors match file position.
  int bracket_depth_for_test(int line)
  {
    return bracket_depth_at_line_start(get_buffer(), line);
  }
  // Places the cursor and lets the viewport follow it through the same
  // ensure_cursor_visible the editing paths use, so tests can drive vertical and
  // horizontal scrolling without faking scroll offsets the editor would clamp.
  void scroll_cursor_to_for_test(int line, int col)
  {
    FileBuffer &buf = get_buffer();
    buf.cursor = {col, line};
    buf.preferred_x = col;
    ensure_cursor_visible();
  }
  // The hosting terminal's size. Meaningless under --gui (it keeps the
  // constructor default because the terminal is never initialised there) --
  // exposed so tests can pin that trap.
  int terminal_height_for_test() const
  {
    return terminal.get_height();
  }
  // Ctrl+hover goto-definition underline (see the mouse dispatcher): true while
  // Ctrl is held over a token, whatever the frontend.
  bool ctrl_hover_active_for_test() const
  {
    return ctrl_hover_active;
  }
  // Terminal mouse-selection hooks for headless tests: feeds clicks,
  // motions and releases through the real private handlers.
  bool terminal_mouse_for_test(int x, int y, bool click, bool motion, bool release)
  {
    return handle_integrated_terminal_mouse(x, y, click, motion, release);
  }
  bool terminal_sel_active_for_test() const
  {
    return terminal_sel_active;
  }
  bool terminal_sel_dragging_for_test() const
  {
    return terminal_sel_dragging;
  }
  int terminal_sel_anchor_row_for_test() const
  {
    return terminal_sel_anchor_row;
  }
  int terminal_sel_anchor_col_for_test() const
  {
    return terminal_sel_anchor_col;
  }
  int terminal_sel_cur_row_for_test() const
  {
    return terminal_sel_cur_row;
  }
  int terminal_sel_cur_col_for_test() const
  {
    return terminal_sel_cur_col;
  }
  // Registers a shell-less terminal so mouse/key handlers have a live
  // vterm-backed target without spawning a process.
  void add_terminal_for_test()
  {
    auto term = std::make_unique<IntegratedTerminal>();
    term->mark_active_for_test();
    integrated_terminals.push_back(std::move(term));
    current_integrated_terminal = (int)integrated_terminals.size() - 1;
    show_integrated_terminal = true;
  }
  // Closes the terminal at the given index through the real handler.
  void close_terminal_for_test(int index)
  {
    close_integrated_terminal(index);
  }
  // Opens/closes the settings menu (the :settings command path).
  void toggle_settings_menu_for_test()
  {
    toggle_settings_menu();
  }
  // Forces the menu closed so each test case starts from a known state.
  void close_settings_menu_for_test()
  {
    close_settings_menu();
  }
  bool mouse_selecting_for_test() const;
  // Grid resize plumbing (jot/app/resize.cpp) and the sidebar's width-driven
  // auto-hide: the same paths the terminal/GUI resize handlers drive.
  void apply_resize_for_test(int cols, int rows)
  {
    apply_resize(cols, rows);
  }
  bool sidebar_visible_for_test() const
  {
    return show_sidebar;
  }
  // Feeds one raw key code through the frontend's key path: the same
  // decode_key_event -> handle_input sequence the terminal and GUI backends use,
  // so a binding's routing can be tested without a terminal.
  void raw_key_for_test(int raw_ch);
  bool sidebar_auto_hidden_for_test() const
  {
    return sidebar_hidden_for_width_;
  }
  void toggle_sidebar_for_test()
  {
    toggle_sidebar();
  }
  // Discord presence session (jot/app/discord_session.cpp) for headless tests:
  // the same entry points the 1s timer, focus reporting and :discord use.
  void discord_poll_for_test(long long now_ms)
  {
    poll_discord_rpc(now_ms);
  }
  void discord_focus_for_test(bool focused, long long now_ms)
  {
    discord_note_focus(focused, now_ms);
  }
  std::string discord_status_for_test() const
  {
    return discord_status;
  }
  bool discord_idle_cleared_for_test() const
  {
    return discord_idle_cleared;
  }
  bool discord_connected_for_test() const
  {
    return discord_rpc.is_connected();
  }
  bool discord_pending_activity_for_test() const
  {
    return discord_rpc.has_pending_activity();
  }
  std::string discord_last_error_for_test() const
  {
    return discord_rpc.last_error();
  }
  std::string discord_command_for_test(const std::string &argument)
  {
    return discord_command(argument);
  }
  // A pending repaint: proves a status change reaches the screen instead of
  // waiting for the next unrelated redraw.
  bool needs_redraw_for_test() const
  {
    return needs_redraw;
  }
  void clear_needs_redraw_for_test()
  {
    needs_redraw = false;
  }
  // Emulates what a settings change does (apply_config_live marks the frame
  // dirty), so a test can render a config change that has no other trigger.
  void request_redraw_for_test()
  {
    needs_redraw = true;
  }
  // Overrides a setting the way :settings does, without writing it to the real
  // user config (tests run against a scratch JOT_CONFIG_HOME).
  void config_set_for_test(const std::string &key, const std::string &value)
  {
    config.set(key, value);
  }
  // Headless surface tests: the recording cell grid, and how many visible
  // floats a Lua surface (jot.ui.handler name, e.g. "sidebar", "home_screen")
  // currently owns. The editor constructor already boots the UI kit through
  // initialize_lua_runtime(), so the handlers are live in tests.
  UI *ui_for_test();
  int lua_float_count_for_test(const std::string &surface) const;
  void load_file(const std::string &fname);
  void run();
  // Reloads configuration from disk (settings.conf overlay + config.lua) and
  // live-applies every setting. Backing of the `:reload` command.
  void reload_config();
  EditorHostAPI &host();
  const EditorHostAPI &host() const;
};

#endif
