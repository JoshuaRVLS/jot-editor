#ifndef LUA_API_H
#define LUA_API_H

#include "text_features.h"
#include "tools/debugger/client.h"
#include "tools/lsp/client.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct LuaScratchBuffer
{
  int handle = 0;
  bool listed = false;
  bool scratch = true;
  bool valid = true;
  std::vector<std::string> lines = {""};
};

struct FloatSpan
{
  int start = 0; // byte offset into the line
  int len = 0;   // byte length
  int fg = 7;    // foreground for the span
  int bg = -1;   // background for the span (-1 = the float's bg). A span that
                 // covers the whole line (start 0, len >= line length) sets
                 // the background for the whole line, like a selected row.
};

// Views handed to Lua UI surface handlers (jot.ui.handler). Native code fills
// these with state; api_core turns them into a payload table and calls the
// registered handler. Returning true from the handler suppresses the native
// render for that surface.
struct PaletteItemView
{
  std::string label;
  std::string category;
  std::string detail;
  std::vector<int> match; // 0-based byte offsets into label matched by query
};

struct PaletteView
{
  std::string query;
  int selected = 0;
  std::vector<PaletteItemView> results;
  int x = 0, y = 0, w = 0, h = 0;
  int screen_w = 0, screen_h = 0;
};

struct QuickPickItemView
{
  std::string label;
  std::string detail;
  std::string preview;
  int severity = 0;
};

struct QuickPickView
{
  std::string title;
  std::string query;
  int selected = 0;
  int all_count = 0;
  std::vector<QuickPickItemView> items;
  int x = 0, y = 0, w = 0, h = 0;
};

struct PopupView
{
  std::string title;
  std::vector<std::string> lines;
  int scroll = 0;
  int x = 0, y = 0, w = 0, h = 0;
};

struct PromptView
{
  std::string input;
  int x = 0, y = 0, w = 0, h = 0;
};

struct TsStatusRowView
{
  bool section = false;
  std::string label; // section title or language name
  std::string detail;
  int color = 0; // theme color slot for language names (0 = default fg)
};

struct TsStatusView
{
  std::vector<TsStatusRowView> rows;
  int scroll = 0;
  int x = 0, y = 0, w = 0, h = 0;
};

struct LspActionView
{
  std::string action;
  std::string label;
  std::string variant; // primary | secondary | danger | muted
  bool enabled = true;
  bool focused = false;
  int x = 0, y = 0, w = 0; // screen rect of the button
};

struct LspManagerRowView
{
  std::string server;
  std::string label;
  std::string state;
  int state_color = 0;
  std::vector<LspActionView> actions;
};

struct LspManagerView
{
  std::vector<LspManagerRowView> rows;
  int selected = 0;
  int scroll = 0;
  int x = 0, y = 0, w = 0, h = 0;
  int label_w = 0;
  int state_x = 0;
  int action_x = 0;
};

struct TelescopeResultView
{
  std::string name;
  std::string parent_path;
  bool is_directory = false;
};

struct TelescopePreviewView
{
  std::string title;
  std::string detail;
  std::vector<std::string> lines; // windowed to the visible region
  int start_line = 0;             // 0-based line number of lines[0]
  std::string extension;          // ".cpp" or empty
  bool is_directory = false;
  bool skipped = false;
};

struct TelescopeView
{
  // Native layout geometry is passed through unchanged so mouse hit-testing
  // (row clicks, wheel regions, query focus) keeps working on the Lua render.
  int x = 0, y = 0, w = 0, h = 0;
  int inner_x = 0, inner_y = 0, inner_w = 0, inner_h = 0;
  int query_x = 0, query_y = 0, query_w = 0;
  int body_y = 0, body_h = 0;
  int list_x = 0, list_y = 0, list_w = 0, list_h = 0;
  int preview_x = 0, preview_y = 0, preview_w = 0, preview_h = 0;
  int footer_y = 0;
  bool show_preview = false;
  std::string query;
  std::string root;
  std::string title;
  int selected = 0;
  int list_scroll = 0;
  int result_count = 0;
  bool scan_pending = false;
  std::string focus;                        // query | results | preview
  std::vector<TelescopeResultView> results; // windowed to visible rows
  TelescopePreviewView preview;
};

struct CompletionItemView
{
  std::string label;
  int kind = 0;
  std::string kind_name; // e.g. "Function", "Keyword", ...
  std::string kind_icon; // nerd-font glyph as rendered natively
  bool deprecated = false;
  std::string detail;
  std::string documentation;
};

struct CompletionView
{
  // Content-box geometry (rows + footer); the Lua render wraps it in a
  // bordered float one cell larger on each side, matching the native popup.
  int x = 0, y = 0, w = 0, h = 0;
  int max_items = 0; // visible row window
  int start = 0;     // absolute index of items[0]
  int selected = 0;  // index into the full filtered list
  int total = 0;     // filtered item count
  int all_total = 0; // unfiltered count (footer "filtered" hint)
  bool filtered = false;
  std::string prefix;
  std::vector<CompletionItemView> items; // windowed to max_items rows
};

struct ContextMenuItemView
{
  std::string label;
  bool enabled = true;
};

struct ContextMenuView
{
  int x = 0, y = 0, w = 0, h = 0;
  int selected = 0;
  std::vector<ContextMenuItemView> items;
};

struct MenuItemView
{
  std::string label;
  bool enabled = true;
};

struct MenuDropdownView
{
  std::string menu_label;
  int x = 0, y = 0, w = 0, h = 0;
  int selected = 0;
  std::vector<MenuItemView> items;
};

struct SearchView
{
  int x = 0, y = 0, w = 0, h = 0;
  std::string query;
  std::string replace_text;
  bool replace_visible = false;
  bool focus_replace = false; // false = find field focused
  bool case_sensitive = false;
  bool whole_word = false;
  bool regex = false;
  bool scoped_to_selection = false;
  std::string count; // "3/12" or "0/0"
};

struct LuaFloatWindow
{
  int handle = 0;
  int buffer = 0;
  int x = 0, y = 0, w = 1, h = 1;
  int row = 0, col = 0;
  int zindex = 50;
  bool valid = true;
  bool enter = false;
  bool focusable = true;
  bool mouse = false;
  bool hide = false;
  bool style_minimal = false;
  int fg = 7, bg = 0;
  int border_fg = -1; // -1 = fall back to fg
  int title_fg = -1;  // -1 = fall back to fg
  int footer_fg = -1; // -1 = fall back to fg
  std::string relative = "editor";
  std::string anchor = "NW";
  std::string border = "none";
  std::array<std::string, 8> custom_border = {"", "", "", "", "", "", "", ""};
  std::string title;
  std::string footer;
  // Per-line inline spans (1-based line index). Used for syntax highlighting
  // inside float content; lines without spans render in fg/bg as before.
  std::map<int, std::vector<FloatSpan>> spans;
  int key_callback = -1;
  int mouse_callback = -1;
  int creation_order = 0;
};

struct PluginCommand
{
  std::string name;
  std::string callback;
  std::string detail;
};

struct PluginKeymap
{
  std::string key;
  std::string callback;
  std::string command;
  std::string detail;
  std::string mode;
};

struct PluginAutocmd
{
  std::string event;
  std::string callback;
};

struct PluginPanel
{
  std::string name;
  std::string callback;
  std::string title;
};

struct PluginStatusSegment
{
  std::string name;
  std::string callback; // registry key into lua_callbacks
  std::string side;     // "left" or "right"
  int priority = 50;
  int fg = -1; // optional xterm color override; -1 = theme status color
};

struct RenderedStatusSegment
{
  std::string text;
  std::string side;
  int priority = 50;
  int fg = -1;
};

struct PluginLoadStatus
{
  std::string name;
  std::string path;
  bool loaded;
  std::string error;
};

struct EventBusSubscriber
{
  std::string key;
  int ref;
};

// Forward declaration
class Editor;
class EditorHostAPI;
class TreeSitterManager;
struct lua_State;

// Timer API (jot.timer): native event-loop timers with Lua callbacks. Entries
// are keyed by the native EventLoop::TimerId (uint64_t); the callback ref is
// also registered under "timer.<ref>" in lua_callbacks so cleanup paths unref
// everything through the existing callback sweep.
struct LuaTimerEntry
{
  int ref = -1;
  bool repeat = false;
};

// Rich per-edit description attached to BufChange events (see
// LuaAPI::on_buffer_change). All line/col values are 1-based; end_line /
// end_col describe the exclusive end of the inserted region in the NEW text.
struct LuaEditDelta
{
  bool valid = false;
  int start_line = 1;
  int start_col = 1;
  int end_line = 1;
  int end_col = 1;
  std::string inserted; // text added by the edit
  std::string removed;  // text replaced/removed by the edit
  bool multiline = false;
};

class LuaAPI
{
private:
  Editor *editor;
  std::vector<PluginCommand> plugin_commands;
  std::vector<PluginKeymap> plugin_keymaps;
  std::vector<PluginAutocmd> plugin_autocmds;
  std::vector<PluginPanel> plugin_panels;
  std::vector<PluginStatusSegment> status_segments_;
  std::vector<PluginLoadStatus> plugin_load_status;
  // Registry ref (LUA_NOREF when unset) of the jot.lsp.hover_ui handler.
  int lsp_hover_ui_ref_ = -2;
  // Surface name -> registered handler (jot.ui.handler). Values are registry
  // refs; -1 entries are removed lazily on next emit.
  std::map<std::string, int> lua_ui_handlers_;
  // One-shot LSP result sinks: when non-empty (a "lsp.*" key into
  // lua_callbacks), the next matching native result is delivered to Lua and
  // skipped by the native handler instead.
  std::string pending_lsp_hover;
  std::string pending_lsp_definition;
  std::string pending_lsp_symbols;
  std::string pending_lsp_completion;
  // One-shot debugger result sinks (jot.debugger.request_*): when non-empty,
  // the next matching debugger event (for pending_debugger_session) is
  // delivered to Lua instead of the native side effects where applicable.
  std::string pending_debugger_stack;
  std::string pending_debugger_variables;
  std::string pending_debugger_threads;
  int pending_debugger_session = -1;
  // Ambient event bus (jot.events): native events broadcast to every
  // subscriber. Refs are unregistered on plugin reload/cleanup.
  std::map<std::string, std::vector<EventBusSubscriber>> event_subscribers_;
  int next_event_sub_id_ = 1;
  // Per-buffer last-known text (only tracked while a BufChange listener is
  // registered) so edit deltas can be computed without touching call sites.
  std::unordered_map<std::string, std::string> edit_snapshots_;
  LuaEditDelta last_edit_;
  // Native timers with Lua callbacks (jot.timer).
  std::unordered_map<uint64_t, LuaTimerEntry> timer_entries_;
  // Last observed debugger state signature (dedupes debugger.state_changed).
  std::string last_debugger_sig_;

  void push_ui_colors(lua_State *L, int payload_index);

  void *lua_state; // lua_State* (opaque in public header)
  bool lua_initialized;

public:
  int next_scratch_buffer = 1;
  int next_float_window = 1;
  int next_float_order = 1;
  int current_float_window = 0;
  std::map<int, LuaScratchBuffer> scratch_buffers;
  std::map<int, LuaFloatWindow> float_windows;

public:
  std::unordered_map<std::string, int> lua_callbacks;

private:
  void clear_runtime_state();
  bool apply_theme_file(const std::string &name, std::vector<std::string> &extends_stack);
  bool load_script_path(const std::string &module_name, const std::string &path);
  bool call_callback_string(const std::string &callback, const std::string &arg);
  bool call_callback_event(const std::string &callback,
                           const std::string &event,
                           const std::string &filepath,
                           int buffer);
  // Resolves the optional buffer argument (1-based index or filepath, or the
  // current buffer when omitted) to a 0-based buffer index, or -1.
  int resolve_buffer_arg(lua_State *L, int arg_index);

public:
  LuaAPI(Editor *ed);
  ~LuaAPI();
  EditorHostAPI &host();
  TreeSitterManager &tree_sitter_manager();
  bool is_main_thread() const;
  void register_treesitter_api(lua_State *L);
  bool load_treesitter_runtime(lua_State *L);
  bool reload_treesitter_runtime();
  // Bundled feature modules (lua/features/*), loaded after the API tables so
  // they can register handlers against them (see load_treesitter_runtime).
  bool load_hover_ui_runtime(lua_State *L);
  bool load_ui_kit_runtime(lua_State *L);

  // Lua UI surfaces: a handler registered through jot.ui.handler(name, fn)
  // takes over rendering of a native surface. Native state is pushed as a
  // payload table; returning true suppresses the native render. The handler is
  // called with nil when the surface closes so it can tear down its floats.
  bool register_lua_ui_handler(const std::string &name, lua_State *L, int fn_index);
  void clear_lua_ui_handler(const std::string &name);
  bool has_lua_ui_handler(const std::string &name) const;
  bool emit_lua_ui(const std::string &name,
                   const std::function<void(lua_State *, int)> &fill_payload);
  bool emit_lua_ui_close(const std::string &name);
  bool emit_command_palette(const PaletteView &view);
  bool emit_quick_pick(const QuickPickView &view);
  bool emit_popup(const PopupView &view);
  bool emit_prompt(const std::string &name, const PromptView &view);
  bool emit_tree_sitter_status(const TsStatusView &view);
  bool emit_lsp_manager(const LspManagerView &view);
  bool emit_telescope(const TelescopeView &view);
  bool emit_lsp_completion(const CompletionView &view);
  bool emit_context_menu(const ContextMenuView &view);
  bool emit_menu_dropdown(const MenuDropdownView &view);
  bool emit_search(const SearchView &view);

  // Terminal-cursor control from Lua (friend access to editor->ui).
  void ui_set_cursor(int x, int y);
  void ui_hide_cursor();

  // LSP hover UI: a Lua handler registered through jot.lsp.hover_ui renders
  // hover results with floats instead of the native popup. When the handler is
  // set, present_lsp_hover() is called right before the native popup would
  // show; returning true suppresses it. notify_lsp_hover_closed() is invoked
  // wherever the native popup would have been dismissed (any key, click, drag,
  // paste, hover replaced, context menu, ...).
  void set_lsp_hover_ui_handler(lua_State *L, int fn_index);
  bool has_lsp_hover_ui() const;
  bool present_lsp_hover(const std::string &contents,
                         const std::string &filepath,
                         int line,
                         int character,
                         const std::string &kind,
                         int anchor_x,
                         int anchor_y);
  void notify_lsp_hover_closed();

  bool init();
  void cleanup();
  void on_buffer_open(const std::string &filepath);
  void on_buffer_change(const std::string &filepath, const std::string &content);
  void on_buffer_save(const std::string &filepath);

  // Lua API functions used by the embedded runtime.
  void show_message(const std::string &msg);
  // Takes the group name by value: nvim-style names ("StatusLineInfo") are
  // translated to jot slot names inside.
  void set_theme_color(std::string name, int fg, int bg);
  bool apply_colorscheme(const std::string &name);
  // Applies a theme and persists it to the config file (jot.theme.apply).
  bool apply_theme_and_persist(const std::string &name);
  std::vector<std::string> list_themes();

  // Plugin system
  void load_plugins();
  void reload_plugins();
  // Re-runs config.lua (the Lua-first configuration file) only. Idempotent:
  // config.lua should only call jot.config.set / other pure config calls.
  // Backing of the `:reload` command together with Editor::reload_config().
  void load_config_file();
  bool run_plugin_command(const std::string &name, const std::string &arg);
  bool run_plugin_keymap(const std::string &key, const std::string &mode = "global");
  void fire_autocmd(const std::string &event, const std::string &filepath = "", int buffer = -1);
  std::vector<std::string> plugin_panel_lines(const std::string &name);
  std::vector<std::string> plugin_picker_items(const std::string &callback);
  bool run_plugin_callback(const std::string &callback, const std::string &arg = "");
  void
  register_command(const std::string &name, const std::string &callback, const std::string &detail);
  void register_keymap(const std::string &key,
                       const std::string &callback,
                       const std::string &command,
                       const std::string &detail,
                       const std::string &mode);
  void register_autocmd(const std::string &event, const std::string &callback);
  void
  register_panel(const std::string &name, const std::string &callback, const std::string &title);

  // Diagnostics / buffer vars / marks / status segments (push/act on the
  // active lua_State; callbacks run on the main thread only).
  void push_diagnostics(lua_State *L);
  void set_buffer_var(lua_State *L);
  void push_buffer_var(lua_State *L);
  void delete_buffer_var(lua_State *L);
  void set_mark(lua_State *L);
  void push_mark(lua_State *L);
  void jump_mark(lua_State *L);
  void delete_mark(lua_State *L);
  void push_mark_list(lua_State *L);
  void register_status_segment(lua_State *L);
  void unregister_status_segment(lua_State *L);
  std::vector<RenderedStatusSegment> render_status_segments();

  // Extended native surface (jot.config / jot.git / jot.tasks / jot.symbols /
  // jot.debugger / jot.editor / jot.theme). These read and act on live
  // Editor state directly so feature code can live in Lua without touching
  // C++: everything below is reachable without recompiling jot.
  void lua_config_get(lua_State *L, int kind); // kind: 0 string, 1 number, 2 bool
  void config_set_from_lua(lua_State *L);
  void config_unset_from_lua(lua_State *L);
  void config_has_from_lua(lua_State *L);
  void push_config_keys(lua_State *L);
  void push_config_path(lua_State *L);
  void push_editor_info(lua_State *L);
  void push_theme_current(lua_State *L);
  void push_git_info(lua_State *L);
  void push_git_status(lua_State *L);
  void git_stage_from_lua(lua_State *L);
  void git_unstage_from_lua(lua_State *L);
  void git_stage_all_from_lua(lua_State *L);
  void git_unstage_all_from_lua(lua_State *L);
  void git_commit_from_lua(lua_State *L);
  void git_refresh_from_lua(lua_State *L);
  void push_task_list(lua_State *L);
  void run_task_from_lua(lua_State *L);
  void rerun_task_from_lua(lua_State *L);
  void push_symbols(lua_State *L);
  void push_debugger_configs(lua_State *L);
  void run_debugger_config_from_lua(lua_State *L);
  void push_buffer_current(lua_State *L);
  void push_buffer_count(lua_State *L);
  void push_buffer_text(lua_State *L);
  void push_buffer_meta(lua_State *L);
  void push_buffer_selection(lua_State *L);
  void push_buffer_bookmarks(lua_State *L);
  void push_buffer_folds(lua_State *L);
  void clipboard_copy_from_lua(lua_State *L);
  void clipboard_cut_from_lua(lua_State *L);
  void clipboard_paste_from_lua(lua_State *L);
  void push_terminal_list(lua_State *L);
  void terminal_write_from_lua(lua_State *L);
  void terminal_close_from_lua(lua_State *L);
  void terminal_activate_from_lua(lua_State *L);
  void terminal_spawn_from_lua(lua_State *L);
  void push_workspace_path(lua_State *L);
  void push_recent_files(lua_State *L);
  void push_recent_workspaces(lua_State *L);
  void push_lsp_clients(lua_State *L);
  void popup_from_lua(lua_State *L);
  void push_search_info(lua_State *L);
  void push_search_matches(lua_State *L);
  void push_picker_active(lua_State *L);
  void push_picker_info(lua_State *L);
  void push_picker_items(lua_State *L);
  void picker_accept_from_lua(lua_State *L);
  void picker_close_from_lua(lua_State *L);
  void push_buffer_tokens(lua_State *L);
  void lsp_request_from_lua(lua_State *L,
                            int kind); // 0 hover, 1 definition, 2 document symbols, 3 completion
  void push_lsp_diagnostics(lua_State *L);
  void push_lsp_last_results(lua_State *L);
  void push_lsp_completions(lua_State *L);
  bool try_deliver_lsp_completion(const std::string &filepath,
                                  const std::vector<LSPCompletionItem> &items);
  // Event bus: subscribe/unsubscribe from Lua, native broadcast entry point.
  void events_subscribe_from_lua(lua_State *L);
  void events_unsubscribe_from_lua(lua_State *L);
  bool has_event_subscribers(const std::string &name) const
  {
    auto it = event_subscribers_.find(name);
    return it != event_subscribers_.end() && !it->second.empty();
  }
  void emit_event_bus(const std::string &name, const std::function<void(lua_State *)> &build);
  void emit_lsp_hover(const LSPHoverResult &hover);
  void emit_lsp_definition(const LSPDefinitionResult &definition);
  void emit_lsp_symbols(const LSPDocumentSymbolResult &symbols);
  void emit_lsp_completion(const std::string &filepath,
                           const std::vector<LSPCompletionItem> &items);
  void emit_git_refreshed();
  // Viewport: mirror the focused pane / editor geometry and scroll state.
  void push_viewport_info(lua_State *L);
  void push_viewport_line_at(lua_State *L);
  void viewport_scroll_top_from_lua(lua_State *L);
  void viewport_scroll_lines_from_lua(lua_State *L);
  void viewport_scroll_col_from_lua(lua_State *L);
  void viewport_reveal_from_lua(lua_State *L);
  // File tree: mirror the native explorer tree (same FileNode data the
  // sidebar renders) without re-walking the disk.
  void push_filetree_root(lua_State *L);
  // Cursor motions (jot.motion): native movement primitives for macros.
  // kind: 0 word_next, 1 word_prev, 2 line_start, 3 line_end, 4 file_start,
  // 5 file_end, 6 matching_bracket, 7 select_function.
  void motion_from_lua(lua_State *L, int kind);
  // Sidebar (jot.sidebar): mirror/control the explorer/git activity views.
  void push_sidebar_info(lua_State *L);
  void sidebar_set_view_from_lua(lua_State *L);
  // LSP manager actions (jot.lsp): disabled set + server enable/install/remove.
  void push_lsp_disabled(lua_State *L);
  void lsp_set_enabled_from_lua(lua_State *L);
  void lsp_install_from_lua(lua_State *L);
  void lsp_remove_from_lua(lua_State *L);
  void lsp_restart_all_from_lua(lua_State *L);
  // Buffer extras: extension-based filetype and single-line read.
  void push_buffer_filetype(lua_State *L);
  void push_buffer_get_line(lua_State *L);
  // Git diff panel (jot.git.diff).
  void git_diff_from_lua(lua_State *L);
  // Timers (jot.timer): schedule native event-loop timers that invoke Lua
  // callbacks on the main thread; cancelled on plugin reload and shutdown.
  void timer_set_from_lua(lua_State *L, bool repeat);
  void timer_clear_from_lua(lua_State *L);
  void cancel_all_timers();
  void fire_timer(int ref);
  // Debugger live state (jot.debugger.state / breakpoints / toggling).
  void push_debugger_state(lua_State *L);
  void push_debugger_breakpoints(lua_State *L);
  void debugger_toggle_breakpoint_from_lua(lua_State *L);
  void debugger_has_breakpoint_from_lua(lua_State *L);
  // One-shot debugger requests (jot.debugger.request_stack / request_variables
  // / request_threads): register a sink, issue the native DAP request, deliver
  // the fresh answer to Lua; stack/variables yield native side effects for
  // that one response.
  void debugger_request_from_lua(lua_State *L, int kind); // 0 stack, 1 variables, 2 threads
  bool try_deliver_debugger_stack(int session, const std::vector<DebuggerFrame> &frames);
  bool try_deliver_debugger_variables(int session, const std::vector<DebuggerVariable> &variables);
  bool try_deliver_debugger_threads(int session, const std::vector<DebuggerThread> &threads);
  // request_variables chain: after the scopes answer, ask the adapter for the
  // first expandable scope's variables (sink stays pending for the Variables
  // event). Returns true when the chain advanced.
  bool debugger_chain_variables(int session, const std::vector<DebuggerVariable> &scopes);
  // Theme palette readback (jot.theme.palette): full current slot table.
  void push_theme_palette(lua_State *L);
  // Buffer lines (jot.buffer.lines) and clipboard text (jot.clipboard.get).
  void push_buffer_lines(lua_State *L);
  void push_clipboard_text(lua_State *L);
  void push_filetree_tree(lua_State *L);
  void push_filetree_children(lua_State *L);
  // Selection control on a real buffer.
  void buffer_select_from_lua(lua_State *L);
  void buffer_clear_selection_from_lua(lua_State *L);
  // Additional bus emitters.
  void emit_buffer_event(const std::string &event, const std::string &path);
  void emit_diagnostics_changed(const std::string &path, const std::vector<Diagnostic> &items);
  void emit_theme_switched(const std::string &name);
  // Emits "debugger.state_changed" when any session's observable state
  // changed since the last poll (deduped by an internal signature).
  void emit_debugger_state_changed();
  // Deliver a native LSP result to a pending Lua one-shot sink. Returns true
  // when the result was consumed by Lua (the caller must skip native UI).
  bool try_deliver_lsp_hover(const LSPHoverResult &hover);
  bool try_deliver_lsp_definition(const LSPDefinitionResult &definition);
  bool try_deliver_lsp_symbols(const LSPDocumentSymbolResult &symbols);

  const std::vector<PluginCommand> &commands() const
  {
    return plugin_commands;
  }
  const std::vector<PluginKeymap> &keymaps() const
  {
    return plugin_keymaps;
  }
  const std::vector<PluginAutocmd> &autocmds() const
  {
    return plugin_autocmds;
  }
  const std::vector<PluginPanel> &panels() const
  {
    return plugin_panels;
  }
  const std::vector<PluginLoadStatus> &load_status() const
  {
    return plugin_load_status;
  }
  std::string get_current_buffer();
  void set_current_buffer(const std::string &text);
  std::string get_selection();
  void replace_selection(const std::string &text);
  void insert_text(const std::string &text);
  std::pair<int, int> get_cursor();
  void set_cursor(int line, int col);
  std::string current_file();
  void open_file(const std::string &path);
  void save_current_file();
  void execute_command(const std::string &command);
  void run_job(const std::string &command, const std::string &cwd, const std::string &label);
  // Async shell capture: runs the command on a worker thread and delivers a
  // {output=..., exit_code=..., ok=...} table to the Lua callback registered
  // under `callback` (a key into lua_callbacks) on the main thread.
  bool
  run_job_capture(const std::string &command, const std::string &cwd, const std::string &callback);
  bool deliver_job_result(const std::string &callback, const std::string &output, int exit_code);
  void show_picker(const std::string &title,
                   const std::string &items_callback,
                   const std::string &select_callback);
  void show_panel(const std::string &name);

  int create_scratch_buffer(bool listed, bool scratch);
  bool set_scratch_lines(
      int buffer, int start, int end, bool strict, const std::vector<std::string> &lines);
  std::vector<std::string> get_scratch_lines(int buffer, int start, int end, bool strict) const;
  bool delete_scratch_buffer(int buffer);
  int open_float(int buffer, bool enter, lua_State *L, int config_index);
  bool configure_float(int window, lua_State *L, int config_index);
  bool set_float_spans(int window, int line, lua_State *L, int spans_index);
  bool close_float(int window, bool force);
  bool is_float_valid(int window) const;
  bool float_input(int ch, bool ctrl, bool shift, bool alt);
  bool float_mouse(int x,
                   int y,
                   int button,
                   bool pressed,
                   bool released,
                   bool motion,
                   bool ctrl,
                   bool shift,
                   bool alt);
  void render_floats();
  void clear_floats();
};

#endif
