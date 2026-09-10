// Lua host bindings assembly: the LuaAPI lifecycle (constructor,
// destructor, active-instance tracking), the inject/field helpers, and
// the single init() pass that registers every l_* binding into the
// jot.* namespaces. The bindings themselves live in bindings_*.cpp.
#include "editor.h"
#include "features/syntax_highlighter.h"
#include "features/tree_sitter/manager.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"
#include "jot/lua/api_internal.h"
#include "jot/lua/embedded_lua.h"
#include "jot/lua/lua_loader.h"
#include "tools/symbols/index.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace fs = std::filesystem;

using namespace lua_bind;

namespace lua_bind
{
  LuaAPI &api(lua_State *L)
  {
    void *ud = lua_touserdata(L, lua_upvalueindex(1));
    if (ud)
      return *static_cast<LuaAPI *>(ud);
    // Namespaced fields are plain C functions (no upvalue). Jot runs a single
    // LuaAPI instance, so fall back to the tracked active instance.
    if (g_lua_active_api)
      return *g_lua_active_api;
    static LuaAPI *const kNullFallback = nullptr;
    (void)kNullFallback;
    luaL_error(L, "Lua API not available");
    return *reinterpret_cast<LuaAPI *>(uintptr_t(1)); // unreachable
  }
} // namespace lua_bind

// Single active bridge instance (fallback for namespaced bindings that carry
// no upvalue); assigned by the LuaAPI constructor / destructor below.
LuaAPI *g_lua_active_api = nullptr;

LuaAPI::LuaAPI(Editor *ed) : editor(ed), lua_state(nullptr), lua_initialized(false)
{
  g_lua_active_api = this;
}
EditorHostAPI &LuaAPI::host()
{
  return *editor->host_api;
}
LuaAPI::~LuaAPI()
{
  cleanup();
  if (g_lua_active_api == this)
    g_lua_active_api = nullptr;
}

namespace
{
  void inject(lua_State *L, LuaAPI *a, const char *name, lua_CFunction fn)
  {
    lua_pushlightuserdata(L, a);
    lua_pushcclosure(L, fn, 1);
    lua_setglobal(L, name);
  }
  void field(lua_State *L, const char *name, lua_CFunction fn)
  {
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
  }
  void command_field(lua_State *L, LuaAPI *a, const char *name, const char *command)
  {
    lua_pushlightuserdata(L, a);
    lua_pushstring(L, command);
    lua_pushcclosure(
        L,
        [](lua_State *s)
        {
          api(s).execute_command(lua_tostring(s, lua_upvalueindex(2)));
          return 0;
        },
        2);
    lua_setfield(L, -2, name);
  }
} // namespace

bool LuaAPI::init()
{
  if (lua_initialized)
    return true;
  lua_State *L = luaL_newstate();
  if (!L)
    return false;
  luaL_openlibs(L);
  lua_state = L;
  lua_initialized = true;
  inject(L, this, "show_message", l_show_message);
  inject(L, this, "show_transient_message", l_show_transient_message);
  inject(L, this, "command", l_register_command);
  inject(L, this, "autocmd", l_register_autocmd);
  inject(L,
         this,
         "set_hl",
         [](lua_State *s)
         {
           auto &a = api(s);
           luaL_checktype(s, 2, LUA_TTABLE);
           lua_getfield(s, 2, "fg");
           int fg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
           lua_pop(s, 1);
           lua_getfield(s, 2, "bg");
           int bg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
           lua_pop(s, 1);
           a.set_theme_color(luaL_optstring(s, 1, ""), fg, bg);
           return 0;
         });
  inject(L, this, "get_current_buffer", l_get_buffer);
  inject(L, this, "set_current_buffer", l_set_buffer);
  inject(L, this, "get_selection", l_get_selection);
  inject(L, this, "replace_selection", l_replace);
  inject(L, this, "insert_text", l_insert);
  inject(L, this, "cursor", l_cursor);
  inject(L, this, "set_cursor", l_set_cursor);
  inject(L, this, "current_file", l_current_file);
  inject(L, this, "open_file", l_open);
  inject(L, this, "save", l_save);
  inject(L, this, "execute", l_execute);
  inject(L, this, "run_job", l_job);
  inject(L, this, "show_picker", l_picker);
  inject(L, this, "show_panel", l_panel_show);
  inject(L, this, "register_keymap", l_register_keymap);
  inject(L, this, "register_panel", l_register_panel);
  lua_newtable(L);
  lua_pushstring(L, "3.0.0");
  lua_setfield(L, -2, "api_version");
  lua_pushstring(L, "Lua coordinates are 1-based line/column");
  lua_setfield(L, -2, "coordinate_convention");
  lua_newtable(L);
  field(L, "get_text", l_get_buffer);
  field(L, "set_text", l_set_buffer);
  field(L, "get_selection", l_get_selection);
  field(L, "replace_selection", l_replace);
  field(L, "insert", l_insert);
  field(L, "cursor", l_cursor);
  field(L, "set_cursor", l_set_cursor);
  field(L, "list", l_buffer_list);
  field(L, "current", l_buf_current);
  field(L, "count", l_buf_count);
  field(L, "text", l_buf_text);
  field(L, "meta", l_buf_meta);
  field(L, "selection", l_buf_selection);
  field(L, "bookmarks", l_buf_bookmarks);
  field(L, "folds", l_buf_folds);
  field(L, "tokens", l_buf_tokens);
  field(L, "select", l_buf_select);
  field(L, "clear_selection", l_buf_clear_selection);
  field(L, "lines", l_buf_lines);
  field(L, "filetype", l_buf_filetype);
  field(L, "get_line", l_buf_get_line);
  field(L, "switch", l_switch_buffer);
  field(L, "set_var", l_buf_set_var);
  field(L, "get_var", l_buf_get_var);
  field(L, "del_var", l_buf_del_var);
  lua_setfield(L, -2, "buffer");
  lua_newtable(L);
  field(L, "get", l_cursor);
  field(L, "set", l_set_cursor);
  lua_setfield(L, -2, "cursor");
  lua_newtable(L);
  field(L, "layout", l_layout);
  field(L, "panes", l_panes);
  field(L, "split_horizontal", l_split_h);
  field(L, "split_vertical", l_split_v);
  field(L, "focus_next", l_focus_next);
  field(L, "focus_previous", l_focus_prev);
  field(L, "resize", l_resize);
  field(L, "resize_direction", l_resize_direction);
  field(L, "equalize", l_equalize);
  field(L, "zoom", l_zoom);
  field(L, "swap", l_swap);
  field(L, "redraw", l_redraw);
  lua_setfield(L, -2, "pane");
  lua_newtable(L);
  field(L, "open", l_open);
  field(L, "save", l_save);
  field(L, "execute", l_execute);
  field(L, "run", l_job);
  lua_setfield(L, -2, "file");
  lua_newtable(L);
  field(L, "show_message", l_show_message);
  field(L, "picker", l_picker);
  field(L, "panel", l_panel_show);
  lua_setfield(L, -2, "ui");
  lua_newtable(L);
  field(L, "register", l_register_keymap);
  lua_setfield(L, -2, "keymap");
  lua_newtable(L);
  field(L, "run", l_job);
  field(L, "capture", l_job_capture);
  lua_setfield(L, -2, "job");
  lua_newtable(L);
  field(L,
        "set",
        [](lua_State *s)
        {
          auto &a = api(s);
          luaL_checktype(s, 2, LUA_TTABLE);
          lua_getfield(s, 2, "fg");
          int fg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
          lua_pop(s, 1);
          lua_getfield(s, 2, "bg");
          int bg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
          lua_pop(s, 1);
          a.set_theme_color(luaL_optstring(s, 1, ""), fg, bg);
          return 0;
        });
  lua_setfield(L, -2, "theme");
  lua_newtable(L);
  field(L, "highlight", l_syntax_highlight);
  lua_setfield(L, -2, "syntax");
  lua_newtable(L);
  field(L, "set", l_decoration_set);
  field(L, "delete", l_decoration_delete);
  field(L, "clear", l_decoration_clear);
  field(L, "list", l_decoration_list);
  lua_setfield(L, -2, "decoration");
  lua_getglobal(L, "show_message");
  lua_setfield(L, -2, "notify");
  lua_getglobal(L, "show_transient_message");
  lua_setfield(L, -2, "notify_transient");
  lua_getglobal(L, "command");
  lua_setfield(L, -2, "command");
  lua_getglobal(L, "autocmd");
  lua_setfield(L, -2, "autocmd");
  lua_getglobal(L, "register_keymap");
  lua_setfield(L, -2, "register_keymap");
  lua_getglobal(L, "register_panel");
  lua_setfield(L, -2, "register_panel");
  lua_getglobal(L, "show_picker");
  lua_setfield(L, -2, "show_picker");
  lua_getglobal(L, "show_panel");
  lua_setfield(L, -2, "show_panel");
  lua_pushvalue(L, -1);
  lua_setglobal(L, "vim");
  lua_setglobal(L, "jot");
  if (luaL_dostring(L,
                    "jot.notify=show_message; jot.notify_transient=show_transient_message; jot.command=command; jot.autocmd=autocmd; "
                    "jot.execute=execute; jot.open_file=open_file; jot.save=save; "
                    "jot.buffer={get_text=get_current_buffer,set_text=set_current_buffer,"
                    "get_selection=get_selection,replace_selection=replace_selection,"
                    "insert_text=insert_text,cursor=cursor,set_cursor=set_cursor,"
                    "current_file=current_file}; "
                    "jot.ui={show_picker=show_picker,register_panel=register_panel,"
                    "show_panel=show_panel}; "
                    "jot.keymap={set=register_keymap}; jot.job={run=run_job}; "
                    "jot.api={set_theme_color=set_hl}; vim=jot"))
  {
    std::cerr << "Lua API setup failed: " << lua_tostring(L, -1) << "\n";
    lua_pop(L, 1);
  }
  // Re-attach fields the compatibility aliases above overwrote: the minimal
  // `jot.buffer={...}` map and `jot.job={run=...}` map replace the richer
  // tables built earlier, so re-apply the full documented surface here.
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "buffer");
  field(L, "list", l_buffer_list);
  field(L, "switch", l_switch_buffer);
  field(L, "current", l_buf_current);
  field(L, "count", l_buf_count);
  field(L, "text", l_buf_text);
  field(L, "meta", l_buf_meta);
  field(L, "selection", l_buf_selection);
  field(L, "bookmarks", l_buf_bookmarks);
  field(L, "folds", l_buf_folds);
  field(L, "tokens", l_buf_tokens);
  field(L, "select", l_buf_select);
  field(L, "clear_selection", l_buf_clear_selection);
  field(L, "lines", l_buf_lines);
  field(L, "filetype", l_buf_filetype);
  field(L, "get_line", l_buf_get_line);
  field(L, "set_var", l_buf_set_var);
  field(L, "get_var", l_buf_get_var);
  field(L, "del_var", l_buf_del_var);
  lua_pop(L, 2);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "job");
  field(L, "capture", l_job_capture);
  lua_pop(L, 2);
  // Same for jot.keymap: the compatibility alias replaced the richer table,
  // so re-attach the full documented surface (register/remove).
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "keymap");
  field(L, "register", l_register_keymap);
  field(L, "remove", l_keymap_remove);
  lua_pop(L, 2);

  // Complete stable runtime surface after compatibility aliases are installed.
  lua_getglobal(L, "jot");
  lua_pushcfunction(L, l_capabilities);
  lua_setfield(L, -2, "capabilities");
  lua_newtable(L);
  field(L, "execute", l_editor_execute);
  field(L, "request_redraw", l_editor_redraw);
  command_field(L, this, "undo", ":undo");
  command_field(L, this, "redo", ":redo");
  command_field(L, this, "insert_newline", ":newline");
  command_field(L, this, "insert_line_below", ":newlinebelow");
  command_field(L, this, "insert_line_above", ":newlineabove");
  command_field(L, this, "delete", ":delete");
  command_field(L, this, "indent", ":indent");
  command_field(L, this, "outdent", ":outdent");
  command_field(L, this, "comment", ":comment");
  command_field(L, this, "duplicate", ":duplicate");
  command_field(L, this, "move_up", ":moveup");
  command_field(L, this, "move_down", ":movedown");
  command_field(L, this, "join", ":join");
  command_field(L, this, "uppercase", ":upper");
  command_field(L, this, "lowercase", ":lower");
  command_field(L, this, "replace", ":replace");
  command_field(L, this, "surround", ":surround");
  command_field(L, this, "increment", ":incnum");
  command_field(L, this, "select_all", ":selectall");
  command_field(L, this, "select_line", ":selectline");
  command_field(L, this, "search", "Toggle Search");
  command_field(L, this, "format", "Format Document");
  lua_setfield(L, -2, "edit");
  lua_newtable(L);
  field(L, "get", l_cursor);
  field(L, "set", l_set_cursor);
  command_field(L, this, "select_all", ":selectall");
  command_field(L, this, "select_line", ":selectline");
  lua_setfield(L, -2, "cursor");
  lua_newtable(L);
  field(L, "current_file", l_current_file);
  field(L, "open", l_open);
  field(L, "save", l_save);
  field(L, "execute", l_editor_execute);
  field(L, "save_buffer", l_save_buffer);
  field(L, "close", l_close_buffer);
  field(L, "new", l_new_buffer);
  field(L, "open_workspace", l_open_workspace);
  field(L, "recent", l_recent_files);
  lua_setfield(L, -2, "file");
  lua_newtable(L);
  field(L, "toggle_sidebar", l_toggle_sidebar);
  field(L, "toggle_zen", l_toggle_zen);
  field(L, "toggle_terminal", l_toggle_terminal);
  field(L, "request_redraw", l_editor_redraw);
  lua_setfield(L, -2, "ui");
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "theme");
  field(L, "list", l_theme_list);
  field(L, "apply", l_theme_apply);
  field(L, "current", l_theme_current);
  lua_pop(L, 2);
  lua_newtable(L);
  field(L, "layout", l_layout);
  field(L, "list", l_panes);
  field(L, "split_horizontal", l_split_h);
  field(L, "split_vertical", l_split_v);
  field(L, "focus_next", l_focus_next);
  field(L, "focus_previous", l_focus_prev);
  field(L, "resize", l_resize);
  field(L, "resize_direction", l_resize_direction);
  field(L, "equalize", l_equalize);
  field(L, "zoom", l_zoom);
  field(L, "swap", l_swap);
  command_field(L, this, "close", "Close Pane");
  lua_setfield(L, -2, "pane");
  lua_newtable(L);
  field(L, "memory", l_process_memory);
  lua_setfield(L, -2, "process");
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "ui");
  field(L, "show_message", l_show_message);
  field(L, "picker", l_picker);
  field(L, "panel", l_panel_show);
  field(L, "popup", l_popup);
  field(L, "command_palette", l_ui_command_palette);
  lua_pop(L, 2);
  lua_newtable(L);
  field(L, "execute", l_editor_execute);
  field(L, "request_redraw", l_editor_redraw);
  field(L, "info", l_editor_info);
  lua_setfield(L, -2, "editor");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "info", l_search_info);
  field(L, "matches", l_search_matches);
  command_field(L, this, "next", ":next");
  command_field(L, this, "previous", ":prev");
  lua_setfield(L, -2, "search");
  // jot.symbols: native SymbolIndex extraction over the live buffer.
  lua_newtable(L);
  field(L, "list", l_symbols_list);
  lua_setfield(L, -2, "symbols");
  lua_newtable(L);
  field(L, "execute", l_command);
  command_field(L, this, "toggle", ":togglefold");
  command_field(L, this, "fold", ":fold");
  command_field(L, this, "unfold", ":unfold");
  command_field(L, this, "all", ":foldall");
  lua_setfield(L, -2, "folds");
  lua_newtable(L);
  field(L, "execute", l_command);
  command_field(L, this, "toggle", "Toggle Bookmark");
  command_field(L, this, "next", "Next Bookmark");
  command_field(L, this, "previous", "Previous Bookmark");
  lua_setfield(L, -2, "bookmarks");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "open", l_open_workspace);
  field(L, "path", l_workspace_path);
  field(L, "recent", l_recent_workspaces);
  lua_setfield(L, -2, "workspace");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "list", l_terminal_list);
  field(L, "spawn", l_terminal_spawn);
  field(L, "close", l_terminal_close);
  field(L, "activate", l_terminal_activate);
  field(L, "write", l_terminal_write);
  command_field(L, this, "toggle", "Toggle Terminal");
  command_field(L, this, "new", "New Terminal");
  lua_setfield(L, -2, "terminal");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "list", l_task_list);
  field(L, "run", l_task_run);
  field(L, "rerun", l_task_rerun);
  command_field(L, this, "show", "Tasks");
  lua_setfield(L, -2, "tasks");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "clients", l_lsp_clients);
  field(L, "diagnostics", l_lsp_diagnostics);
  field(L, "results", l_lsp_results);
  field(L, "completions", l_lsp_completions);
  field(L, "request_hover", l_lsp_request_hover);
  field(L, "request_definition", l_lsp_request_definition);
  field(L, "request_symbols", l_lsp_request_symbols);
  field(L, "request_completion", l_lsp_request_completion);
  field(L, "hover_ui", l_lsp_hover_ui);
  field(L, "disabled", l_lsp_disabled);
  field(L, "set_enabled", l_lsp_set_enabled);
  field(L, "install", l_lsp_install);
  field(L, "remove", l_lsp_remove);
  field(L, "restart_all", l_lsp_restart_all);
  command_field(L, this, "definition", "LSP Definition");
  command_field(L, this, "back", "LSP Back");
  command_field(L, this, "completion", "LSP Completion");
  lua_setfield(L, -2, "lsp");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "configs", l_dbg_configs);
  field(L, "run_config", l_dbg_run_config);
  field(L, "state", l_dbg_state);
  field(L, "breakpoints", l_dbg_breakpoints);
  field(L, "toggle_breakpoint", l_dbg_toggle_bp);
  field(L, "has_breakpoint", l_dbg_has_bp);
  field(L, "request_stack", l_dbg_request_stack);
  field(L, "request_variables", l_dbg_request_variables);
  field(L, "request_threads", l_dbg_request_threads);
  field(L, "scroll_output", l_dbg_scroll_output);
  field(L, "cycle_thread", l_dbg_cycle_thread);
  field(L, "cycle_frame", l_dbg_cycle_frame);
  command_field(L, this, "continue", "Debug Continue");
  command_field(L, this, "pause", "Debug Pause");
  command_field(L, this, "step_in", "Debug Step In");
  command_field(L, this, "step_over", "Debug Step Over");
  command_field(L, this, "step_out", "Debug Step Out");
  command_field(L, this, "stop", "Debug Stop");
  lua_setfield(L, -2, "debugger");
  lua_newtable(L);
  field(L, "execute", l_command);
  field(L, "info", l_git_info);
  field(L, "status", l_git_status);
  field(L, "stage", l_git_stage);
  field(L, "unstage", l_git_unstage);
  field(L, "stage_all", l_git_stage_all);
  field(L, "unstage_all", l_git_unstage_all);
  field(L, "commit", l_git_commit);
  field(L, "refresh", l_git_refresh);
  field(L, "diff", l_git_diff);
  lua_setfield(L, -2, "git");
  lua_newtable(L);
  field(L, "execute", l_command);
  command_field(L, this, "status", "Tree-sitter Status");
  command_field(L, this, "reload", "Reload Tree-sitter");
  lua_setfield(L, -2, "treesitter");
  lua_newtable(L);
  field(L, "execute", l_command);
  command_field(L, this, "open", "Open Image");
  lua_setfield(L, -2, "image");
  lua_newtable(L);
  field(L, "get", l_config_get);
  field(L, "get_number", l_config_get_number);
  field(L, "get_bool", l_config_get_bool);
  field(L, "set", l_config_set);
  field(L, "unset", l_config_unset);
  field(L, "has", l_config_has);
  field(L, "keys", l_config_keys);
  field(L, "path", l_config_path);
  lua_setfield(L, -2, "config");
  lua_newtable(L);
  field(L, "get", l_diagnostics_get);
  lua_setfield(L, -2, "diagnostics");
  lua_newtable(L);
  field(L, "set", l_mark_set);
  field(L, "get", l_mark_get);
  field(L, "jump", l_mark_jump);
  field(L, "del", l_mark_del);
  field(L, "list", l_mark_list);
  lua_setfield(L, -2, "marks");
  lua_newtable(L);
  field(L, "copy", l_clip_copy);
  field(L, "cut", l_clip_cut);
  field(L, "paste", l_clip_paste);
  field(L, "get", l_clip_get);
  field(L, "set", l_clip_set);
  lua_setfield(L, -2, "clipboard");
  lua_newtable(L);
  field(L, "active", l_picker_active);
  field(L, "info", l_picker_info);
  field(L, "items", l_picker_items);
  field(L, "accept", l_picker_accept);
  field(L, "close", l_picker_close);
  lua_setfield(L, -2, "picker");
  lua_newtable(L);
  field(L, "subscribe", l_events_subscribe);
  field(L, "unsubscribe", l_events_unsubscribe);
  lua_setfield(L, -2, "events");
  lua_newtable(L);
  field(L, "set_timeout", l_timer_set_timeout);
  field(L, "set_interval", l_timer_set_interval);
  field(L, "clear", l_timer_clear);
  lua_setfield(L, -2, "timer");
  lua_newtable(L);
  field(L, "word_next", l_motion_word_next);
  field(L, "word_prev", l_motion_word_prev);
  field(L, "line_start", l_motion_line_start);
  field(L, "line_end", l_motion_line_end);
  field(L, "file_start", l_motion_file_start);
  field(L, "file_end", l_motion_file_end);
  field(L, "matching_bracket", l_motion_matching_bracket);
  field(L, "select_function", l_motion_select_function);
  lua_setfield(L, -2, "motion");
  lua_newtable(L);
  field(L, "info", l_sidebar_info);
  field(L, "set_view", l_sidebar_set_view);
  lua_setfield(L, -2, "sidebar");
  lua_newtable(L);
  field(L, "register", l_toast_register);
  field(L, "show", l_toast_show);
  field(L, "dismiss", l_toast_dismiss);
  field(L, "clear", l_toast_clear);
  field(L, "info", l_toast_info);
  lua_setfield(L, -2, "toast");
  lua_newtable(L);
  field(L, "info", l_viewport_info);
  field(L, "line_at", l_viewport_line_at);
  field(L, "scroll_top", l_viewport_scroll_top);
  field(L, "scroll_lines", l_viewport_scroll_lines);
  field(L, "scroll_col", l_viewport_scroll_col);
  field(L, "reveal", l_viewport_reveal);
  lua_setfield(L, -2, "viewport");
  lua_newtable(L);
  field(L, "root", l_filetree_root);
  field(L, "tree", l_filetree_tree);
  field(L, "children", l_filetree_children);
  lua_setfield(L, -2, "filetree");
  lua_newtable(L);
  field(L, "register", l_status_register);
  field(L, "unregister", l_status_unregister);
  lua_setfield(L, -2, "status");
  lua_setglobal(L, "jot");
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "ui");
  lua_newtable(L);
  field(L, "create", l_ui_buffer_create);
  field(L, "set_lines", l_ui_buffer_set_lines);
  field(L, "get_lines", l_ui_buffer_get_lines);
  field(L, "delete", l_ui_buffer_delete);
  lua_setfield(L, -2, "buffer");
  field(L, "handler", l_ui_handler);
  field(L,
        "set_cursor",
        [](lua_State *s)
        {
          api(s).ui_set_cursor((int)luaL_checkinteger(s, 1), (int)luaL_checkinteger(s, 2));
          return 0;
        });
  field(L,
        "hide_cursor",
        [](lua_State *s)
        {
          api(s).ui_hide_cursor();
          return 0;
        });
  lua_newtable(L);
  field(L, "open", l_float_open);
  field(L, "set_lines", l_float_set_lines);
  field(L, "get_lines", l_float_get_lines);
  field(L, "configure", l_float_configure);
  field(L, "get_config", l_ui_float_get_config);
  field(L, "close", l_float_close);
  field(L, "is_valid", l_ui_float_is_valid);
  field(L, "buffer", l_ui_float_buffer);
  field(L, "focus", l_ui_float_focus);
  field(L, "current", l_ui_float_current);
  field(L, "on_key", l_float_on_key);
  field(L, "on_mouse", l_float_on_mouse);
  field(L, "set_spans", l_float_set_spans);
  lua_setfield(L, -2, "float");
  lua_pop(L, 2);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "theme");
  field(L,
        "set_color",
        [](lua_State *s)
        {
          auto &a = api(s);
          luaL_checktype(s, 2, LUA_TTABLE);
          lua_getfield(s, 2, "fg");
          int fg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
          lua_pop(s, 1);
          lua_getfield(s, 2, "bg");
          int bg = lua_isnumber(s, -1) ? (int)lua_tointeger(s, -1) : -1;
          lua_pop(s, 1);
          a.set_theme_color(luaL_optstring(s, 1, ""), fg, bg);
          return 0;
        });
  field(L, "palette", l_theme_palette);
  lua_pop(L, 2);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "ui");
  field(L, "register_panel", l_register_panel);
  lua_pop(L, 2);
  load_treesitter_runtime(L);
  load_hover_ui_runtime(L);
  load_ui_kit_runtime(L);
  // LSP installer: mason-style registry + per-manager install scripts (see
  // runtime/lua/lsp/install.lua). Pure data + pure functions; loading it only
  // registers the plan/list callbacks the native host uses.
  register_lsp_install_api(L);
  load_lsp_installer(L);
  // Web attach policy + toolkit presets (lsp/policy.lua). Runs after the
  // installer so jot.lsp.installed() is bound for presence checks.
  load_lsp_policy(L);
  // Update feature metadata: expose the git checkout this binary was built
  // from (empty for plain installed binaries) so features/update.lua can
  // fetch/compare/rebuild against the source clone without hardcoding paths.
  lua_getglobal(L, "jot");
  lua_pushstring(L, jot_lua_repo_root().string().c_str());
  lua_setfield(L, -2, "source_dir");
  // Native :update dispatches here (features/update.lua registers the fn).
  lua_pushcfunction(L, l_register_update_handler);
  lua_setfield(L, -2, "register_update_handler");
  // Self-restart: replays the original launch args so the fresh binary boots
  // the same file/workspace. Returns false when unsaved buffers block it.
  lua_pushcfunction(L, l_editor_restart);
  lua_setfield(L, -2, "restart");
  lua_pop(L, 1);
  // Bundled feature: inline diagnostics as anchored decorations (see
  // lua/features/decorations.lua). Loaded after user plugins: load_plugins()
  // resets the autocmd table for plugin reloads, so registering before it
  // would be wiped before any diagnostic arrives. The handler re-checks
  // decorations_inline_diagnostics on every event, so config.lua can still
  // disable it.
  load_plugins();
  jot_lua::load_bundled_lua_file(L, "features/decorations.lua", "Decorations");
  // Built-in editor keybinds (features/keymaps.lua). Loaded after plugins so
  // user keymaps registered first take precedence; the Lua registrations
  // shadow the matching hardcoded fallbacks in the modeless input path.
  jot_lua::load_bundled_lua_file(L, "features/keymaps.lua", "Built-in keymaps");
  // Zen focus mode (features/zen.lua): F12 toggles the centered, chrome-free
  // layout. Loaded after keymaps so it can reuse the jot.keymap API.
  jot_lua::load_bundled_lua_file(L, "features/zen.lua", "Zen mode");
  // Self-update (:update + silent startup check, features/update.lua). Loaded
  // last so user config can tune update.* settings before the module boots.
  jot_lua::load_bundled_lua_file(L, "features/update.lua", "Update");
  return true;
}
