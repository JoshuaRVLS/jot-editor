#ifndef LUA_API_H
#define LUA_API_H

#include "dap/client.h"
#include "extras/leetcode_http.h"
#include "lsp/client.h"
#include "text_features.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class PreviewServer;

#include "lua/view/plugins.h"
#include "lua/view/shared.h"
#include "lua/view/surfaces.h"

// SnapshotLine, the edit snapshots below (see model/buffer.h). Kept in its own
// block so the view includes above keep the order they have to be read in.
#include "core/buffer.h"

// Forward declaration
class Editor;
class EditorHostAPI;
class TreeSitterManager;
struct lua_State;
struct EditorState;
class UI;

#include "lua/view/runtime.h"

// One entry of the recorded Lua API surface. Recorded by walking the table
// init() built, so the description cannot drift from the bindings.
struct LuaApiEntry
{
  std::string name;      // "buffer.apply_edit", "decoration", "command"
  bool callable = false; // a function rather than a namespace table
};

// The Lua API object itself. The view structs it hands to Lua live in
// jot/lua/view/ (see their maps there); this header keeps the class, its
// includes and the forward declarations the class needs.
class LuaAPI
{
private:
  Editor *editor;
  std::vector<PluginCommand> plugin_commands;
  std::vector<PluginKeymap> plugin_keymaps;
  std::vector<PluginAutocmd> plugin_autocmds;
  std::vector<PluginPanel> plugin_panels;
  // What this build registered, as dotted names.
  std::vector<LuaApiEntry> api_entries_;
  std::vector<PluginStatusSegment> status_segments_;
  std::vector<PluginLoadStatus> plugin_load_status;
  // Registry ref (LUA_NOREF when unset) of the jot.lsp.hover_ui handler.
  int lsp_hover_ui_ref_ = -2;
  // Registry ref (LUA_NOREF when unset) of the jot.lsp snippet-completion
  // handler (registered by the bundled snippet engine).
  int lsp_snippet_handler_ref_ = -2;
  // Registry refs (LUA_NOREF when unset) of the raw layer's callbacks: the
  // jot.screen painter and the jot.input key / mouse hooks (api_screen.cpp).
  // Dropped on plugin reload and on shutdown with the rest of the runtime.
  int paint_hook_ref_ = -2;
  int raw_key_ref_ = -2;
  int raw_mouse_ref_ = -2;
  // One entry of the jot.input mode stack (api_screen.cpp): the handle
  // pop_mode takes, a name for introspection, and a registry ref per event kind
  // it handles (-1 when it handles none of that kind).
  struct InputMode
  {
    int handle = 0;
    std::string name;
    int key_ref = -1;
    int mouse_ref = -1;
  };
  // The stack, back() is the top: the owner that sees an event first.
  std::vector<InputMode> input_modes_;
  int next_input_mode_handle_ = 1;
  // jot.policy providers, by name, holding a registry ref to each table
  // (api_provider.cpp). Dropped with the rest of the runtime on reload.
  std::map<std::string, int> lua_policies_;
  // Pushes the provider's `field` function for `name`, or pushes nothing and
  // answers 0 when there is no such provider or function.
  int push_lua_policy_field(const char *name, const char *field);
  // Pushes the provider table for `name`, or nothing and 0 when there is none.
  int push_lua_policy_table(const char *name);
  // Surface name -> registered handler (jot.ui.handler). Values are registry
  // refs; -1 entries are removed lazily on next emit.
  std::map<std::string, int> lua_ui_handlers_;
  // Surface whose Lua handler is currently running (set around the handler
  // call in emit_lua_ui). Floats opened during the call are tagged with it,
  // so render_floats() can tell modal-content floats (e.g. a command palette
  // panel) from background floats (sidebar / status line) that must stay
  // under the modal scrim.
  std::string current_emit_surface_;
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
  // Per-buffer last-known lines (only tracked while a BufChange listener is
  // registered) so edit deltas can be computed without touching call sites.
  // A snapshot (SnapshotLine, model/buffer.h) holds each unchanged line by
  // reference, so it costs the lines an edit touched rather than a copy of the
  // buffer joined into one string and split apart again on every keystroke.
  // stamp is the clock reading of the edit that last refreshed the entry, so
  // the line budget in on_buffer_change can drop the file a plugin has left
  // alone the longest instead of whichever one the map happens to visit first.
  struct EditSnapshot
  {
    std::vector<SnapshotLine> lines;
    std::uint64_t stamp = 0;
  };
  std::unordered_map<std::string, EditSnapshot> edit_snapshots_;
  std::uint64_t edit_snapshot_clock_ = 0;
  LuaEditDelta last_edit_;
  // Rapid events (BufChange, CursorMoved) fired several times inside one
  // event-loop drain are coalesced into a single Lua dispatch per event
  // name at the next flush (right before a frame renders). Fast typists and
  // multi-key paste would otherwise pay a full Lua round-trip per key.
  // last_edit_ is captured per entry so a deferred BufChange still carries
  // the delta of the edit that produced it.
  struct PendingAutocmd
  {
    std::string event;
    std::string filepath;
    int buffer = -1;
    LuaEditDelta delta;
  };
  std::vector<PendingAutocmd> pending_autocmds_;
  bool flushing_autocmds_ = false;
  static bool is_rapid_autocmd(const std::string &event);
  // Native timers with Lua callbacks (jot.timer).
  std::unordered_map<uint64_t, LuaTimerEntry> timer_entries_;
  // Last observed debugger state signature (dedupes debugger.state_changed).
  std::string last_debugger_sig_;
  // Process memory cache: resident set size is re-read at most once per
  // second (the status line polls it every frame).
  long long cached_process_memory_bytes_ = -1;
  std::chrono::steady_clock::time_point last_process_memory_read_{};

  void push_ui_colors(lua_State *L, int payload_index);

  void *lua_state; // lua_State* (opaque in public header)
  bool lua_initialized;
  // True once the boot-time treesitter query load recorded its sources (see
  // flush_deferred_treesitter_queries); makes the load idempotent so the
  // event-loop poller can call it without re-queueing compile jobs.
  bool treesitter_queries_flushed_ = false;

public:
  int next_scratch_buffer = 1;
  int next_float_window = 1;
  int next_float_order = 1;
  int current_float_window = 0;
  // Registry reference (LUA_NOREF == -1) to the Lua toast module table;
  // set by jot.toast.register and cleared on shutdown.
  int toast_module_ref_ = -1;
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
                           int buffer,
                           const LuaEditDelta *edit = nullptr);

public:
  // Resolves the optional buffer argument (1-based index or filepath, or the
  // current buffer when omitted) to a 0-based buffer index, or -1.
  int resolve_buffer_arg(lua_State *L, int arg_index);
  // Anchored decorations (extmark-style, see core/app/decorations.cpp).
  // buffer_idx is the 0-based buffer index from resolve_buffer_arg; set
  // parses the opts table at opts_index (1-based row/col, byte width, hl
  // group, explicit fg/bg, priority, right_gravity, optional id to update)
  // and creates or updates the decoration, returning the decoration's id
  // (0 on failure).
  std::uint64_t decoration_set(int buffer_idx, lua_State *L, int opts_index);
  bool decoration_delete(int buffer_idx, std::uint64_t id);
  void decoration_clear(int buffer_idx);
  // Pushes the buffer's decorations as a Lua array of option tables.
  void decoration_list(int buffer_idx, lua_State *L);

  LuaAPI(Editor *ed);
  ~LuaAPI();
  EditorHostAPI &host();
  TreeSitterManager &tree_sitter_manager();
  bool is_main_thread() const;
  void register_treesitter_api(lua_State *L);
  bool load_treesitter_runtime(lua_State *L);
  bool reload_treesitter_runtime();
  // Runs the bundled tree-sitter highlight-query load that boot deferred to
  // after the first frame (see treesitter/init.lua load_queries). The load
  // itself only records sources; the parser dlopen and query compiles happen
  // on a background thread started here. Idempotent; safe to call any time on
  // the main thread. Returns false when the runtime is missing or the load
  // raised.
  bool flush_deferred_treesitter_queries();
  // Advances the background query compile: starts it on first call, installs
  // finished results and reports failures through the Lua policy when the
  // worker is done. Returns false once there is nothing left to do, so the
  // caller can stop polling.
  bool tick_deferred_treesitter_compile();
  // Installs background whole-file parses that finished since the last tick
  // (see TreeSitterManager::queue_async_parse).
  void tick_deferred_treesitter_parses();
  // Bundled feature modules (lua/features/*), loaded after the API tables so
  // they can register handlers against them (see load_treesitter_runtime).
  bool load_hover_ui_runtime(lua_State *L);
  bool load_ui_kit_runtime(lua_State *L);
  // Bundled markdown preview feature (features/markdown/init.lua and its
  // modules). Loaded after user plugins so its commands/autocmds survive.
  bool load_markdown_runtime(lua_State *L);
  bool load_html_runtime(lua_State *L);
  bool load_snippet_runtime(lua_State *L);
  bool load_bible_runtime(lua_State *L);
  bool load_settings_runtime(lua_State *L);
  bool load_comments_runtime(lua_State *L);
  bool load_leetcode_runtime(lua_State *L);
  // Bundled AI assistant (features/ai/*.lua): the CodeCompanion-style chat,
  // inline rewrite and provider adapters. Loaded after the snippet runtime
  // (a chat reply can carry snippets) and before features/update.lua.
  bool load_ai_runtime(lua_State *L);

  // LSP installer host half (see api_lsp_install.cpp): loads the Lua
  // installer module (runtime/lua/lsp/install.lua, mason-style registry +
  // per-manager scripts) and bridges install/remove plans and the server
  // list between Lua and the native install machinery.
  bool load_lsp_installer(lua_State *L);
  void register_lsp_install_api(lua_State *L);
  // Lua plan lookups. Returns false for unknown servers. On success fills
  // the canonical registry id, the shell script body ("" when the server is
  // not supported on this platform; message then explains why) and a human
  // message.
  bool lsp_install_plan(const std::string &name,
                        std::string *id,
                        std::string *script,
                        std::string *message);
  bool lsp_remove_plan(const std::string &name,
                       std::string *id,
                       std::string *script,
                       std::string *message);
  bool lsp_install_list(std::vector<LspServerSpec> *out);
  bool load_lsp_policy(lua_State *L);
  // Extra servers the Lua attach policy wants for `filepath` on top of its
  // primary `language` server (web stacks: tailwind/eslint next to
  // typescript). Each entry names the registry server id plus the binary to
  // launch and its stdio args. Returns false when no policy is registered.
  bool lsp_policy_extras(const std::string &language,
                         const std::string &filepath,
                         std::vector<LspPolicyExtra> *out);
  // One-shot toolkit presets ("web") as {kind="lsp"|"parser", name=...} items.
  bool lsp_policy_preset(const std::string &name, std::vector<LspPolicyTool> *out);

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
  bool emit_settings(const SettingsView &view);
  bool emit_popup(const PopupView &view);
  bool emit_prompt(const std::string &name, const PromptView &view);
  bool emit_tree_sitter_status(const TsStatusView &view);
  bool emit_lsp_status(const TsStatusView &view);
  bool emit_telescope(const TelescopeView &view);
  bool emit_lsp_completion(const CompletionView &view);
  bool emit_lsp_signature(const SignatureView &view);
  bool emit_context_menu(const ContextMenuView &view);
  bool emit_menu_dropdown(const MenuDropdownView &view);
  // The winbar row and the drop-down a crumb opens (render/winbar.cpp).
  bool emit_winbar(const WinbarView &view);
  bool emit_winbar_menu(const WinbarMenuView &view);
  bool emit_search(const SearchView &view);
  bool emit_home(const HomeView &view);
  bool emit_status(const StatusView &view);
  bool emit_sidebar(const SidebarPanelView &view);
  bool emit_side_panel(const SidePanelView &view);

  // Terminal-cursor control from Lua (friend access to editor->ui).
  void ui_set_cursor(int x, int y);
  void ui_hide_cursor();
  // Test hook: point the Lua float renderer at a headless UI grid so
  // render_floats() can be exercised without a live terminal.
  void attach_test_ui(UI *ui);
  // Test hook: total lines the per-file edit snapshots hold right now, so a
  // case can pin the budget that keeps many open files from adding up.
  std::size_t edit_snapshot_lines_for_test() const;
  // Test hook: whether one file still holds a snapshot, so a case can pin which
  // entries a budget sweep kept and which it dropped.
  bool edit_snapshot_has_for_test(const std::string &filepath) const;

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
  void show_transient_message(const std::string &msg, int duration_ms = 5000);
  // Takes the group name by value: nvim-style names ("StatusLineInfo") are
  // translated to jot slot names inside.
  void set_theme_color(std::string name, int fg, int bg);
  bool apply_colorscheme(const std::string &name);
  // Applies a theme and persists it to the config file (jot.theme.apply).
  bool apply_theme_and_persist(const std::string &name);
  std::vector<std::string> list_themes();
  // True when a theme file of this name lives in a directory the user owns, so
  // a scheme the user wrote wins over the editor's legacy-name alias.
  bool theme_file_is_user_owned(const std::string &name) const;

  // Plugin system
  void load_plugins();
  void reload_plugins();
  // Re-runs config.lua (the Lua-first configuration file) only. Idempotent:
  // config.lua should only call jot.config.set / other pure config calls.
  // Backing of the `:reload` command together with Editor::reload_config().
  void load_config_file();
  bool run_plugin_command(const std::string &name, const std::string &arg);
  bool run_plugin_keymap(const std::string &key, const std::string &mode = "global");
  // Update feature bridge: the native :update ex command dispatches into
  // features/update.lua through a handler registered with
  // jot.register_update_handler (stored under the "update.cmd" callback id).
  void set_update_handler_ref(int lua_ref);
  bool run_update_command(const std::string &arg);
  // Replaces the running process with a fresh jot (POSIX exec in place /
  // Windows spawn) so a rebuilt binary loads immediately. False when unsaved
  // buffers block it (unless force) or the relaunch failed.
  bool restart_editor(bool force = false);
  // Removes every Lua keymap whose canonical key matches (any mode when
  // `mode` is empty). Frees the stored callback refs. Used by features that
  // register a keymap only while some condition holds (e.g. the :update
  // Ctrl+U shortcut while updates are available).
  void remove_keymap(const std::string &key, const std::string &mode = "");
  // Whether any keymap sequence ("Ctrl+T N") starts with the given chord
  // ("Ctrl+T") - i.e. pressing the chord should reveal the which-key helper.
  bool plugin_keymap_is_prefix(const std::string &chord, const std::string &mode);
  // Rows shown by the helper under a prefix path (chords joined with a space,
  // e.g. "Ctrl+T" or "Ctrl+T N").
  std::vector<PluginKeymapChild> plugin_keymap_children(const std::string &chord_path,
                                                        const std::string &mode);
  // Optional title for a group: the detail registered on the bare prefix key.
  std::string plugin_keymap_group_title(const std::string &chord, const std::string &mode);
  void fire_autocmd(const std::string &event, const std::string &filepath = "", int buffer = -1);
  // Delivers coalesced rapid autocmds (BufChange, CursorMoved) to Lua; the
  // event loop calls this once per drain, just before a frame renders.
  void flush_pending_autocmds();
  void dispatch_autocmd(const std::string &event,
                        const std::string &filepath,
                        int buffer,
                        const LuaEditDelta *edit);
  std::vector<std::string> plugin_panel_lines(const std::string &name);
  // Structured rows of a registered panel. `action` empty means "render":
  // the callback returns the rows to draw. A non-empty action is a click on
  // the row at `index`; the callback performs it and its return value is
  // ignored (the next frame renders the result).
  std::vector<PluginPanelRow>
  plugin_panel_rows(const std::string &name, const std::string &action = "", int index = -1);
  // Hands one key to a registered panel, as a callback call with
  // `event.action = "key"` plus `event.key` (the canonical chord name) and the
  // modifier flags. The panel's truthy return says it used the key, and the
  // caller only offers keys while the dock's plugin tab holds focus and no
  // modal prompt owns the keyboard.
  bool plugin_panel_key(const std::string &name,
                        const std::string &key,
                        bool ctrl,
                        bool shift,
                        bool alt);
  // Records the surface init() just registered, so `jot.api.*`,
  // `jot.capabilities()` and `jot --api-info` answer from one place.
  void record_api_entries(lua_State *L);
  const std::vector<LuaApiEntry> &api_entries() const { return api_entries_; }
  // One line per callable, sorted, behind a header with the counts.
  std::vector<std::string> api_info_lines() const;
  void push_api_namespaces(lua_State *L);
  void push_api_functions(lua_State *L);
  void push_api_info(lua_State *L);
  void push_api_has(lua_State *L);
  void push_capabilities(lua_State *L);
  // Picker rows: a plain string item carries an empty value, a table item may
  // carry `value` (what the select callback receives instead of the display
  // text), `detail` (right-aligned secondary text) and `fg` (the row's own
  // colour, a "#rrggbb" string or xterm index).
  std::vector<PluginPickerRow> plugin_picker_items(const std::string &callback);
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
  // Expands the Emmet abbreviation at the cursor through the snippet engine
  // (extras/emmet/wiring.cpp). False when there was nothing to expand.
  bool expand_emmet();
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
  // Resolved colours of a decoration highlight group as 24-bit hex (the
  // read-side twin of set_theme_color; unknown names push nil).
  void push_theme_color(lua_State *L, const std::string &name);
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
  void clipboard_set_from_lua(lua_State *L);
  void push_terminal_list(lua_State *L);
  void terminal_write_from_lua(lua_State *L);
  void terminal_close_from_lua(lua_State *L);
  void terminal_activate_from_lua(lua_State *L);
  void terminal_spawn_from_lua(lua_State *L);
  void push_workspace_path(lua_State *L);
  // Literal case-insensitive text search across the workspace root, the same
  // walk :grep uses. Results carry 1-based line/column like every other Lua
  // coordinate.
  void push_workspace_search(lua_State *L);
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
  // Reads a file from disk (jot.file.read), independent of the buffer list:
  // snippet packs and other config data live outside the open buffers.
  void push_file_read(lua_State *L);
  // Directory listing (jot.file.list): the entries of an arbitrary path, so
  // Lua features can discover snippet/config packs outside the workspace
  // file tree (which only covers the workspace root).
  void push_file_list(lua_State *L);
  // Cursor motions (jot.motion): native movement primitives for macros.
  // kind: 0 word_next, 1 word_prev, 2 line_start, 3 line_end, 4 file_start,
  // 5 file_end, 6 matching_bracket, 7 select_function.
  void motion_from_lua(lua_State *L, int kind);
  // Sidebar (jot.sidebar): mirror/control the explorer/git activity views.
  void push_sidebar_info(lua_State *L);
  void sidebar_set_view_from_lua(lua_State *L);
  // Undo tree (jot.undo): the states the buffer in front has been in, the text
  // of any one of them, and the jump onto one. A panel draws the tree from
  // this; the walking and the captures stay in buffers/undo.cpp.
  void push_undo_tree(lua_State *L);
  void push_undo_state_lines(lua_State *L);
  void undo_tree_goto_from_lua(lua_State *L);
  void undo_tree_clear_from_lua(lua_State *L);
  // Precise buffer edit (jot.buffer.apply_edit): replace a half-open range
  // with text, splitting on '\n'. 1-based lines and columns, end-exclusive.
  void apply_buffer_edit_from_lua(lua_State *L);
  // The editor's own Tab / Shift+Tab actions (jot.edit.tab / shift_tab), so
  // Lua keymaps that shadow those chords can fall back to them exactly.
  void default_tab_from_lua(lua_State *L);
  void default_shift_tab_from_lua(lua_State *L);
  // Applies the currently selected LSP completion item (jot.lsp
  // .accept_completion), so Lua keymaps that shadow Tab can hand the key back
  // to the completion popup instead of breaking accept-on-Tab.
  void lsp_accept_completion_from_lua(lua_State *L);
  // Snippet completion bridge: the bundled snippet engine registers a handler
  // that claims LSP items carrying `insert_text_format = 2` so they expand
  // through the real snippet engine instead of the plain-text fallback.
  void set_lsp_snippet_handler_ref(int lua_ref);
  // Calls the registered handler with the snippet body and the exact 1-based
  // replace range. Returns true when Lua consumed (expanded) it.
  bool run_lsp_snippet_handler(
      const std::string &text, int start_line, int start_col, int end_line, int end_col);
  // LSP manager actions (jot.lsp): disabled set + server enable/install/remove.
  void push_lsp_disabled(lua_State *L);
  // Current resident memory of the editor process in bytes (jot.process
  // .memory), cached ~1 s. -1 when the platform could not be queried.
  long long process_memory_bytes();
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
  // Debugger panel navigation (jot.debugger.scroll_output / cycle_thread /
  // cycle_frame): scroll the output history, switch the active thread, or
  // walk the active thread's stack frames. Each returns true when the action
  // moved something.
  void debugger_scroll_output_from_lua(lua_State *L);
  void debugger_cycle_thread_from_lua(lua_State *L);
  void debugger_cycle_frame_from_lua(lua_State *L);
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
  // Toasts (runtime/lua/features/ui/toast.lua): fired by the deprecated statusline
  // message channel so every set_message / set_transient_message also surfaces
  // as a toast. duration_ms <= 0 means "use the toast default".
  void emit_toast_event(const std::string &message, int duration_ms);
  // Direct delivery to the registered toast module (same LuaAPI instance that
  // holds the module reference - the same path jot.toast.show uses). Returns
  // the toast id, or 0 when no module is registered.
  int emit_toast_direct(lua_State *L, const std::string &message, int duration_ms);
  // jot.toast: store a reference to the Lua toast module table and forward
  // show/dismiss/clear/info calls to it (the module keeps all the logic).
  void register_toast_module(lua_State *L);
  void toast_show_from_lua(lua_State *L);
  void toast_dismiss_from_lua(lua_State *L);
  void toast_clear_from_lua(lua_State *L);
  void toast_info_from_lua(lua_State *L);
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
  bool leetcode_http_request(const LeetCodeHttp::Request &request, const std::string &callback);
  bool deliver_leetcode_http_result(const std::string &callback,
                                    const LeetCodeHttp::Response &response);
  void show_picker(const std::string &title,
                   const std::string &items_callback,
                   const std::string &select_callback);
  // jot.ui.panel(name[, focus]): show a registered panel's dock tab. `focus`
  // hands the dock the keyboard, which a panel that reads keys needs.
  void show_panel(const std::string &name, bool focus);
  // Closes the dock's plugin tab, the way the focused panel's own q key does.
  void close_plugin_panel();

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
  bool float_paste(const std::string &text);
  bool float_mouse(int x,
                   int y,
                   int button,
                   bool pressed,
                   bool released,
                   bool motion,
                   bool ctrl,
                   bool shift,
                   bool alt);
  // Float z-order layers: floats open at 0 (editor chrome), a modal surface's own
  // float opens on kModalFloatZindex so it always paints above that chrome, and
  // toasts sit above both (see runtime/lua/features/ui/toast.lua).
  static constexpr int kModalFloatZindex = 50000;

  // The raw layer (jot.screen / jot.input / jot.term), see api_screen.cpp.
  // Everything above it -- floats, the UI kit, the editor's own chrome -- is
  // built on cells and events; this is that layer exposed to Lua, so a script
  // can draw anything the cell model can express and see any event before the
  // editor does.
  //
  // One cell's ink. Unset fields are -1 for colours ("the theme's default") and
  // false/0 for the attributes.
  struct ScreenStyle
  {
    int fg = -1;
    int bg = -1;
    int underline = 0;
    int underline_fg = -1;
    bool bold = false;
    bool italic = false;
    bool dim = false;
    bool reverse = false;
  };
  // jot.screen: writes a run of grapheme clusters starting at (x, y), one cell
  // each (two for a wide cluster), clipped to the grid. Returns the number of
  // cells painted.
  int screen_write(int x, int y, const std::string &text, const ScreenStyle &style);
  // jot.screen.fill: the same ink over a rect. Returns cells painted.
  int screen_fill(int x, int y, int w, int h, const std::string &text, const ScreenStyle &style);
  // jot.screen.cell / text / size: reads back what is in the grid right now.
  void screen_cell_to_lua(lua_State *L, int x, int y);
  void screen_text_to_lua(lua_State *L, int x, int y, int w);
  void screen_size_to_lua(lua_State *L);
  // jot.screen.on_paint: the per-frame painter, called after the editor has
  // drawn everything it knows about and before the frame is diffed out. A
  // registered painter that returns true asks for another frame immediately
  // (the animation loop); returning nothing lets the editor idle again.
  void set_paint_hook(lua_State *L, int fn_index);
  bool run_paint_hook();
  bool has_paint_hook() const
  {
    return paint_hook_ref_ >= 0;
  }
  // jot.screen.redraw: queue a repaint (the same call a timer uses to drive an
  // animation).
  void screen_redraw();
  // jot.input.on_key / on_mouse: every key and mouse event, before any mode
  // sees it. True consumes the event. See terminal_events.cpp for where they
  // sit in the dispatch order. These two are the base of the stack below.
  void set_raw_key_hook(lua_State *L, int fn_index);
  void set_raw_mouse_hook(lua_State *L, int fn_index);
  // jot.input.push_mode / pop_mode / modes: a stack of owners above the base
  // hooks, so an overlay that takes the keyboard (a menu inside a menu, a text
  // prompt inside an overlay) composes instead of every surface hand-consuming
  // the keys it does not want. An event is offered from the top down and the
  // first handler that returns true consumes it; when nothing consumes, the
  // base hook runs and then the editor does.
  //
  // One entry per push: the handle is what pop_mode takes and `name` is for
  // introspection (jot.input.modes); either callback may be absent.
  // Reads a Lua function (key handler) or a table {name, on_key, on_mouse} from
  // `arg_index` and pushes it. Returns the handle (> 0), or 0 when the argument
  // was neither.
  int push_input_mode_from_lua(lua_State *L, int arg_index);
  // Removes the mode `handle` names, or the top one when the argument is absent.
  // False when the stack is empty or the handle is not in it.
  bool pop_input_mode_from_lua(lua_State *L);
  // The stack top-first, as a table of {handle=, name=} entries.
  void input_modes_to_lua(lua_State *L);
  int input_mode_depth() const
  {
    return (int)input_modes_.size();
  }
  bool raw_key_input(int ch, bool ctrl, bool shift, bool alt, int original_ch);
  bool raw_mouse_input(int x,
                       int y,
                       int button,
                       bool pressed,
                       bool released,
                       bool motion,
                       bool ctrl,
                       bool shift,
                       bool alt);
  // jot.term: the terminal itself. write() appends bytes to this frame's
  // output, ahead of the caret, which is the only way Lua can emit a protocol
  // sequence the cell model cannot express (kitty graphics, OSC links).
  bool term_write(const std::string &bytes);
  void term_info_to_lua(lua_State *L);

  // Whether a modal surface (quick pick, a popup, the status modals, the
  // telescope, the settings menu) is up. A modal paints above the background
  // floats and is the only surface that takes the mouse.
  bool modal_surface_open() const;

  // Drops the per-frame overlay list and snapshots the float-free grid the GUI
  // backend diffs against. Call once per frame, before the first float layer.
  void begin_float_pass();
  // Paints the visible floats inside the z-index window (inclusive), in paint
  // order. A modal frame splits its pass across kModalFloatZindex so the chrome
  // lands under the modal and the modal's own float above it; render_floats()
  // is the whole-window pass and the common case.
  void render_float_layer(int min_zindex, int max_zindex);
  void render_floats();
  void clear_floats();

  // Markdown preview transport (markdown_preview_server.h): a libuv HTTP/SSE
  // server the Lua markdown feature drives. Every entry point is main-thread
  // and maps 1:1 onto one Lua function in the jot.preview namespace.
  void preview_start_from_lua(lua_State *L);
  void preview_stop_from_lua(lua_State *L);
  void preview_status_from_lua(lua_State *L);
  void preview_set_page_from_lua(lua_State *L);
  void preview_page_from_lua(lua_State *L);
  void preview_set_content_from_lua(lua_State *L);
  // jot.preview.set_document(rel_path, text): the file served in place of the
  // one on disk under the file root, for the buffer being edited. An empty path
  // clears it.
  void preview_set_document_from_lua(lua_State *L);
  void preview_notify_from_lua(lua_State *L);
  void preview_sync_from_lua(lua_State *L);
  void preview_take_scroll_from_lua(lua_State *L);

  // jot.policy (see api_provider.cpp): a named table owned by a bundled Lua
  // module. C++ keeps the mechanism and asks the provider first; with no
  // provider registered its own implementation answers, so a ported policy
  // degrades to the old behavior instead of failing.
  //
  // A provider carries either functions (a decision) or plain fields (a table
  // of entries, the settings menu's catalog being the first). Functions are
  // named calls, one per entry point; fields are read with the lua_policy_*
  // readers below, entry by entry, which is what keeps a table of a few
  // hundred rows off the per-frame path: the native side reads what it needs
  // when it builds the list, not while it paints.
  //
  // The exchange carries plain fields only. A book travels as the id its URLs
  // use ("JHN"), never as a pointer or an index into the native table, so the
  // canon the reader can actually fetch stays the authority on which books
  // exist.
  struct BibleRefView
  {
    std::string book_id;
    int chapter = 0;
    bool has_verse = false;
    int verse_first = 0;
    int verse_last = 0;
  };
  // `index` is a Lua table (or nil to unregister).
  void set_lua_policy(lua_State *L, const char *name, int index);
  bool has_lua_policy(const char *name) const;
  void lua_policy_names_to_lua(lua_State *L);
  // The field readers: the entry named `key` in the provider `name`, its
  // `field` read as the asked-for type. Each answers `fallback` (or an empty
  // list) when there is no provider, no such entry, or the field is missing or
  // of another type, so a caller never branches on whether Lua is there.
  bool lua_policy_has_entry(const char *name, const std::string &key);
  bool lua_policy_has_field(const char *name, const std::string &key, const char *field);
  std::string lua_policy_str(const char *name,
                             const std::string &key,
                             const char *field,
                             const std::string &fallback);
  int lua_policy_int(const char *name, const std::string &key, const char *field, int fallback);
  std::vector<std::string> lua_policy_str_list(const char *name,
                                               const std::string &key,
                                               const char *field);
  // The bible.reference provider's two calls: the one `:bible` reads its
  // argument with, and the one the tab strip and the toasts print. Each
  // answers false when there is no provider, when its field is missing, or
  // when what came back cannot be used, which is the caller's signal to use
  // the native implementation.
  bool bible_policy_parse(const std::string &text, BibleRefView &out);
  bool bible_policy_label(const BibleRefView &ref, std::string &out);

  // jot.lua: the module surface (see api_runtime.cpp). A feature written in
  // Lua is a directory of files under the config dir, resolved through
  // package.path, and reloaded in place -- no rebuild, no restart.
  //
  // path()/dir() answer where `require` looks and where the user's Lua lives.
  void lua_path_to_lua(lua_State *L);
  void lua_dir_to_lua(lua_State *L);
  // reload(name) re-runs one module: it drops name and everything under it from
  // package.loaded and requires it again, returning (true, module) or
  // (false, error). With no name, every user file (init.lua and every plugin)
  // re-runs, the way :reloadplugins does.
  void lua_reload_from_lua(lua_State *L);
  // Installs the module search path above the built-in one. Called once, right
  // after the standard libraries are opened.
  static void setup_lua_module_path(lua_State *L);

private:
  PreviewServer *ensure_preview_server();
  std::unique_ptr<PreviewServer> preview_server_;
  // Unrefs the raw layer's three callbacks (see api_screen.cpp). Called from
  // the shutdown and plugin-reload paths, before the Lua state is closed.
  void clear_raw_hooks();
  // Drops every jot.policy provider (see api_provider.cpp), same two paths.
  void clear_lua_policies();
};

#endif
