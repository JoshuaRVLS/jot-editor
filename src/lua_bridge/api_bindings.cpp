// Lua host bindings: the l_* C functions registered into the jot.* namespaces
// and the single init() pass that assembles them. Split out of api_core.cpp so
// the API-surface registration lives apart from the engine-facing
// implementations (api_float.cpp, api_core.cpp, ...).
#include "editor.h"
#include "features/syntax_highlighter.h"
#include "features/tree_sitter/manager.h"
#include "host_api.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "lua_bridge/embedded_lua.h"
#include "lua_bridge/lua_loader.h"
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

  // Runs `command` through the shell on the calling thread, merging stderr into
  // stdout. Returns {output, exit_code}. Used by worker threads only.

  int l_show_message(lua_State *L)
  {
    api(L).show_message(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_register_command(lua_State *L)
  {
    auto &a = api(L);
    const char *name = luaL_optstring(L, 1, "");
    const char *detail = luaL_optstring(L, 3, "Runtime command");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string id = "lua." + std::to_string(ref);
    a.lua_callbacks[id] = ref;
    a.register_command(name, id, detail);
    return 0;
  }
  int l_register_keymap(lua_State *L)
  {
    auto &a = api(L);
    const char *key = luaL_optstring(L, 1, "");
    std::string cb;
    std::string cmd;
    if (lua_isfunction(L, 2))
    {
      lua_pushvalue(L, 2);
      int ref = luaL_ref(L, LUA_REGISTRYINDEX);
      cb = "lua." + std::to_string(ref);
      a.lua_callbacks[cb] = ref;
    }
    else
      cmd = luaL_optstring(L, 2, "");
    a.register_keymap(key, cb, cmd, luaL_optstring(L, 3, ""), luaL_optstring(L, 4, "global"));
    return 0;
  }
  int l_register_autocmd(lua_State *L)
  {
    auto &a = api(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string id = "lua." + std::to_string(ref);
    a.lua_callbacks[id] = ref;
    a.register_autocmd(luaL_optstring(L, 1, ""), id);
    return 0;
  }
  int l_register_panel(lua_State *L)
  {
    auto &a = api(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string id = "lua." + std::to_string(ref);
    a.lua_callbacks[id] = ref;
    a.register_panel(luaL_optstring(L, 1, ""), id, luaL_optstring(L, 3, ""));
    return 0;
  }
  int l_get_buffer(lua_State *L)
  {
    lua_pushstring(L, api(L).get_current_buffer().c_str());
    return 1;
  }
  int l_set_buffer(lua_State *L)
  {
    api(L).set_current_buffer(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_get_selection(lua_State *L)
  {
    lua_pushstring(L, api(L).get_selection().c_str());
    return 1;
  }
  int l_replace(lua_State *L)
  {
    api(L).replace_selection(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_insert(lua_State *L)
  {
    api(L).insert_text(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_cursor(lua_State *L)
  {
    auto p = api(L).get_cursor();
    lua_pushinteger(L, p.first + 1);
    lua_pushinteger(L, p.second + 1);
    return 2;
  }
  int l_set_cursor(lua_State *L)
  {
    api(L).set_cursor((int)luaL_checkinteger(L, 1) - 1, (int)luaL_checkinteger(L, 2) - 1);
    return 0;
  }
  int l_current_file(lua_State *L)
  {
    lua_pushstring(L, api(L).current_file().c_str());
    return 1;
  }
  int l_open(lua_State *L)
  {
    api(L).open_file(luaL_checkstring(L, 1));
    return 0;
  }
  int l_save(lua_State *L)
  {
    api(L).save_current_file();
    return 0;
  }
  int l_execute(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_job(lua_State *L)
  {
    api(L).run_job(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), luaL_optstring(L, 3, ""));
    return 0;
  }
  int l_close_buffer(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().core.close_buffer((int)luaL_checkinteger(L, 1) - 1));
    return 1;
  }
  int l_new_buffer(lua_State *L)
  {
    api(L).host().core.new_buffer();
    return 0;
  }
  int l_save_buffer(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().io.save_buffer((int)luaL_checkinteger(L, 1) - 1));
    return 1;
  }
  int l_open_workspace(lua_State *L)
  {
    api(L).host().io.open_workspace(luaL_checkstring(L, 1));
    return 0;
  }
  int l_toggle_sidebar(lua_State *L)
  {
    api(L).host().io.toggle_sidebar();
    return 0;
  }
  int l_toggle_terminal(lua_State *L)
  {
    api(L).host().io.toggle_terminal();
    return 0;
  }
  int l_editor_execute(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_editor_redraw(lua_State *L)
  {
    api(L).host().render.request_redraw();
    return 0;
  }
  int l_capabilities(lua_State *L)
  {
    lua_newtable(L);
    const char *names[] = {
        "buffer",      "cursor", "selection", "clipboard", "picker",     "events",  "viewport",
        "filetree",    "pane",   "file",      "ui",        "keymap",     "job",     "edit",
        "search",      "folds",  "bookmarks", "workspace", "terminal",   "tasks",   "theme",
        "config",      "lsp",    "debugger",  "git",       "treesitter", "symbols", "image",
        "diagnostics", "marks",  "status",    "timer",     "motion",     "sidebar"};
    for (const char *name : names)
    {
      lua_pushboolean(L, 1);
      lua_setfield(L, -2, name);
    }
    return 1;
  }
  int l_theme_list(lua_State *L)
  {
    lua_newtable(L);
    int n = 1;
    for (const auto &name : api(L).list_themes())
    {
      lua_pushstring(L, name.c_str());
      lua_rawseti(L, -2, n++);
    }
    return 1;
  }
  int l_theme_apply(lua_State *L)
  {
    lua_pushboolean(L, api(L).apply_theme_and_persist(luaL_checkstring(L, 1)));
    return 1;
  }
  int l_command(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_picker(lua_State *L)
  {
    auto &a = api(L);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    int r = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string select = "lua." + std::to_string(r);
    a.lua_callbacks[select] = r;
    std::string items;
    if (lua_isfunction(L, 2))
    {
      lua_pushvalue(L, 2);
      r = luaL_ref(L, LUA_REGISTRYINDEX);
      items = "lua." + std::to_string(r);
      a.lua_callbacks[items] = r;
    }
    else
    {
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_pushvalue(L, 2);
      r = luaL_ref(L, LUA_REGISTRYINDEX);
      items = "lua." + std::to_string(r);
    }
    a.show_picker(luaL_optstring(L, 1, "Runtime Picker"), items, select);
    return 0;
  }
  int l_panel_show(lua_State *L)
  {
    api(L).show_panel(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_buffer_list(lua_State *L)
  {
    auto &a = api(L);
    lua_newtable(L);
    int n = 1;
    for (const auto &b : a.host().core.list_buffers())
    {
      lua_newtable(L);
      lua_pushinteger(L, b.index + 1);
      lua_setfield(L, -2, "index");
      lua_pushstring(L, b.filepath.c_str());
      lua_setfield(L, -2, "path");
      lua_pushboolean(L, b.modified);
      lua_setfield(L, -2, "modified");
      lua_pushboolean(L, b.active);
      lua_setfield(L, -2, "active");
      lua_pushboolean(L, b.preview);
      lua_setfield(L, -2, "preview");
      lua_rawseti(L, -2, n++);
    }
    return 1;
  }
  int l_layout(lua_State *L)
  {
    auto x = api(L).host().render.layout();
    lua_newtable(L);
    lua_pushinteger(L, x.width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, x.height);
    lua_setfield(L, -2, "height");
    lua_pushboolean(L, x.sidebar_visible);
    lua_setfield(L, -2, "sidebar_visible");
    lua_pushinteger(L, x.sidebar_width);
    lua_setfield(L, -2, "sidebar_width");
    lua_pushboolean(L, x.minimap_visible);
    lua_setfield(L, -2, "minimap_visible");
    lua_pushboolean(L, x.terminal_visible);
    lua_setfield(L, -2, "terminal_visible");
    lua_pushinteger(L, x.terminal_height);
    lua_setfield(L, -2, "terminal_height");
    return 1;
  }
  int l_panes(lua_State *L)
  {
    auto &a = api(L);
    lua_newtable(L);
    int n = 1;
    for (const auto &p : a.host().render.list_panes())
    {
      lua_newtable(L);
      lua_pushinteger(L, p.index + 1);
      lua_setfield(L, -2, "index");
      lua_pushinteger(L, p.buffer_id + 1);
      lua_setfield(L, -2, "buffer");
      lua_pushinteger(L, p.x);
      lua_setfield(L, -2, "x");
      lua_pushinteger(L, p.y);
      lua_setfield(L, -2, "y");
      lua_pushinteger(L, p.w);
      lua_setfield(L, -2, "width");
      lua_pushinteger(L, p.h);
      lua_setfield(L, -2, "height");
      lua_pushboolean(L, p.focused);
      lua_setfield(L, -2, "focused");
      lua_rawseti(L, -2, n++);
    }
    return 1;
  }
  int l_switch_buffer(lua_State *L)
  {
    const bool ok = api(L).host().core.switch_buffer((int)luaL_checkinteger(L, 1) - 1);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
  }
  int l_split_h(lua_State *L)
  {
    api(L).host().render.split_horizontal();
    return 0;
  }
  int l_split_v(lua_State *L)
  {
    api(L).host().render.split_vertical();
    return 0;
  }
  int l_focus_next(lua_State *L)
  {
    api(L).host().render.focus_next_pane();
    return 0;
  }
  int l_focus_prev(lua_State *L)
  {
    api(L).host().render.focus_prev_pane();
    return 0;
  }
  int l_resize(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().render.resize_focused_pane((int)luaL_checkinteger(L, 1)));
    return 1;
  }
  int l_redraw(lua_State *L)
  {
    api(L).host().render.request_redraw();
    return 0;
  }
  int l_ui_buffer_create(lua_State *L)
  {
    lua_pushinteger(L, api(L).create_scratch_buffer(lua_toboolean(L, 1), lua_toboolean(L, 2)));
    return 1;
  }
  int l_ui_buffer_set_lines(lua_State *L)
  {
    luaL_checktype(L, 5, LUA_TTABLE);
    std::vector<std::string> v;
    for (int i = 1;; i++)
    {
      lua_rawgeti(L, 5, i);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      v.emplace_back(luaL_checkstring(L, -1));
      lua_pop(L, 1);
    }
    const bool ok = api(L).set_scratch_lines((int)luaL_checkinteger(L, 1),
                                             (int)luaL_checkinteger(L, 2),
                                             (int)luaL_checkinteger(L, 3),
                                             lua_toboolean(L, 4),
                                             v);
    lua_pushboolean(L, ok);
    return 1;
  }
  int l_ui_buffer_get_lines(lua_State *L)
  {
    auto v = api(L).get_scratch_lines((int)luaL_checkinteger(L, 1),
                                      (int)luaL_checkinteger(L, 2),
                                      (int)luaL_checkinteger(L, 3),
                                      lua_toboolean(L, 4));
    lua_newtable(L);
    int n = 1;
    for (auto &s : v)
    {
      lua_pushlstring(L, s.data(), s.size());
      lua_rawseti(L, -2, n++);
    }
    return 1;
  }
  int l_ui_buffer_delete(lua_State *L)
  {
    lua_pushboolean(L, api(L).delete_scratch_buffer((int)luaL_checkinteger(L, 1)));
    return 1;
  }
  int l_ui_handler(lua_State *L)
  {
    // jot.ui.handler(name, fn) registers a Lua renderer for a native UI
    // surface; handler(name, nil) unregisters it. fn(state) is called with a
    // payload table while the surface is visible (returning true suppresses
    // the native render) and with nil when the surface closes.
    const std::string name = luaL_checkstring(L, 1);
    if (lua_isnoneornil(L, 2))
    {
      api(L).clear_lua_ui_handler(name);
    }
    else
    {
      luaL_checktype(L, 2, LUA_TFUNCTION);
      api(L).register_lua_ui_handler(name, L, 2);
    }
    return 0;
  }

  int l_ui_float_open(lua_State *L)
  {
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_pushinteger(L, api(L).open_float((int)luaL_checkinteger(L, 1), lua_toboolean(L, 2), L, 3));
    return 1;
  }
  int l_ui_float_configure(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushboolean(L, api(L).configure_float((int)luaL_checkinteger(L, 1), L, 2));
    return 1;
  }
  int l_ui_float_get_config(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    lua_newtable(L);
    auto it = api(L).float_windows.find(w);
    if (it == api(L).float_windows.end())
      return 1;
    auto &f = it->second;
    lua_pushinteger(L, f.col);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, f.row);
    lua_setfield(L, -2, "row");
    lua_pushinteger(L, f.w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, f.h);
    lua_setfield(L, -2, "height");
    lua_pushstring(L, f.relative.c_str());
    lua_setfield(L, -2, "relative");
    lua_pushstring(L, f.anchor.c_str());
    lua_setfield(L, -2, "anchor");
    lua_pushstring(L, f.border.c_str());
    lua_setfield(L, -2, "border");
    return 1;
  }
  int l_ui_float_close(lua_State *L)
  {
    lua_pushboolean(L, api(L).close_float((int)luaL_checkinteger(L, 1), lua_toboolean(L, 2)));
    return 1;
  }
  int l_ui_float_is_valid(lua_State *L)
  {
    lua_pushboolean(L, api(L).is_float_valid((int)luaL_checkinteger(L, 1)));
    return 1;
  }
  int l_ui_float_buffer(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    auto it = api(L).float_windows.find(w);
    lua_pushinteger(L, it == api(L).float_windows.end() ? 0 : it->second.buffer);
    return 1;
  }
  int l_ui_float_focus(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, api(L).is_float_valid(w));
    if (lua_toboolean(L, -1))
      api(L).current_float_window = w;
    return 1;
  }
  int l_ui_float_current(lua_State *L)
  {
    lua_pushinteger(L, api(L).current_float_window);
    return 1;
  }
  int l_float_open(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushinteger(L, api(L).open_float((int)luaL_checkinteger(L, 1), true, L, 2));
    return 1;
  }
  int l_float_set_lines(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    auto it = api(L).float_windows.find(w);
    if (it == api(L).float_windows.end())
      return (lua_pushboolean(L, 0), 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_replace(L, 5);
    lua_pushinteger(L, it->second.buffer);
    lua_replace(L, 1);
    lua_pushinteger(L, 0);
    lua_replace(L, 2);
    lua_pushinteger(L, -1);
    lua_replace(L, 3);
    lua_pushboolean(L, 0);
    lua_replace(L, 4);
    return l_ui_buffer_set_lines(L);
  }
  int l_float_get_lines(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    auto it = api(L).float_windows.find(w);
    if (it == api(L).float_windows.end())
      return (lua_newtable(L), 1);
    lua_pushinteger(L, it->second.buffer);
    lua_replace(L, 1);
    lua_pushinteger(L, 0);
    lua_replace(L, 2);
    lua_pushinteger(L, -1);
    lua_replace(L, 3);
    lua_pushboolean(L, 0);
    lua_replace(L, 4);
    return l_ui_buffer_get_lines(L);
  }
  int l_float_configure(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushboolean(L, api(L).configure_float((int)luaL_checkinteger(L, 1), L, 2));
    return 1;
  }
  const char *syntax_token_kind_name(int token)
  {
    switch (token)
    {
    case TS_TOKEN_KEYWORD:
      return "keyword";
    case TS_TOKEN_STRING:
      return "string";
    case TS_TOKEN_COMMENT:
      return "comment";
    case TS_TOKEN_NUMBER:
      return "number";
    case TS_TOKEN_TYPE:
      return "type";
    case TS_TOKEN_FUNCTION:
      return "function";
    case TS_TOKEN_VARIABLE:
      return "variable";
    case TS_TOKEN_CONSTANT:
      return "constant";
    case TS_TOKEN_BUILTIN:
      return "builtin";
    case TS_TOKEN_OPERATOR:
      return "operator";
    case TS_TOKEN_PUNCTUATION:
      return "punctuation";
    case TS_TOKEN_TAG:
      return "tag";
    case TS_TOKEN_ATTRIBUTE:
      return "attribute";
    case TS_TOKEN_NAMESPACE:
      return "namespace";
    case TS_TOKEN_MODULE:
      return "module";
    case TS_TOKEN_PARAMETER:
      return "parameter";
    case TS_TOKEN_FIELD:
      return "field";
    case TS_TOKEN_KEYWORD_CONTROL:
      return "keyword_control";
    case TS_TOKEN_KEYWORD_STORAGE:
      return "keyword_storage";
    case TS_TOKEN_KEYWORD_PREPROC:
      return "keyword_preproc";
    case TS_TOKEN_FUNCTION_METHOD:
      return "function_method";
    case TS_TOKEN_FUNCTION_CONSTRUCTOR:
      return "function_constructor";
    case TS_TOKEN_TYPE_BUILTIN:
      return "type_builtin";
    case TS_TOKEN_CONSTANT_MACRO:
      return "constant_macro";
    case TS_TOKEN_STRING_ESCAPE:
      return "string_escape";
    case TS_TOKEN_PUNCTUATION_BRACKET:
      return "punctuation_bracket";
    case TS_TOKEN_PUNCTUATION_DELIMITER:
      return "punctuation_delimiter";
    default:
      return "";
    }
  }

  int l_syntax_highlight(lua_State *L)
  {
    const std::string ext = luaL_optstring(L, 1, "");
    const std::string text = luaL_optstring(L, 2, "");
    SyntaxHighlighter highlighter;
    highlighter.set_language(ext);
    const auto colors = highlighter.get_colors(text);
    lua_newtable(L);
    if (!highlighter.has_rules())
    {
      return 1;
    }
    int out = 1;
    int chunk_start = 0;
    int chunk_token = 0;
    for (int i = 0; i <= (int)colors.size(); i++)
    {
      int token = 0;
      if (i < (int)colors.size() && colors[i].first == 1)
      {
        token = colors[i].second;
      }
      if (i == 0)
      {
        chunk_token = token;
      }
      if (i == (int)colors.size() || token != chunk_token)
      {
        if (i > chunk_start && chunk_token != TS_TOKEN_NONE)
        {
          lua_newtable(L);
          lua_pushinteger(L, chunk_start);
          lua_setfield(L, -2, "start");
          lua_pushinteger(L, i - chunk_start);
          lua_setfield(L, -2, "len");
          lua_pushstring(L, syntax_token_kind_name(chunk_token));
          lua_setfield(L, -2, "kind");
          lua_rawseti(L, -2, out++);
        }
        chunk_start = i;
        chunk_token = token;
      }
    }
    return 1;
  }

  int l_decoration_set(lua_State *L)
  {
    auto &a = api(L);
    const int idx = a.resolve_buffer_arg(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const std::uint64_t id = idx >= 0 ? a.decoration_set(idx, L, 2) : 0;
    if (id != 0)
    {
      a.host().render.request_redraw();
    }
    if (id != 0)
    {
      lua_pushinteger(L, (lua_Integer)id);
    }
    else
    {
      lua_pushboolean(L, 0);
    }
    return 1;
  }
  int l_decoration_delete(lua_State *L)
  {
    auto &a = api(L);
    const int idx = a.resolve_buffer_arg(L, 1);
    const std::uint64_t id = (std::uint64_t)luaL_checkinteger(L, 2);
    const bool ok = idx >= 0 && a.decoration_delete(idx, id);
    if (ok)
    {
      a.host().render.request_redraw();
    }
    lua_pushboolean(L, ok);
    return 1;
  }
  int l_decoration_clear(lua_State *L)
  {
    auto &a = api(L);
    const int idx = a.resolve_buffer_arg(L, 1);
    if (idx >= 0)
    {
      a.decoration_clear(idx);
      a.host().render.request_redraw();
    }
    lua_pushboolean(L, idx >= 0);
    return 1;
  }
  int l_decoration_list(lua_State *L)
  {
    auto &a = api(L);
    const int idx = a.resolve_buffer_arg(L, 1);
    a.decoration_list(idx, L);
    return 1;
  }

  int l_float_set_spans(lua_State *L)
  {
    // (window, line, spans) - spans is an array of {start, len, fg} tables
    // with byte offsets into the line; replaces any previous spans for the line.
    const int win = (int)luaL_checkinteger(L, 1);
    const int line = (int)luaL_checkinteger(L, 2);
    if (line < 1)
      return (lua_pushboolean(L, 0), 1);
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_pushboolean(L, api(L).set_float_spans(win, line, L, 3));
    return 1;
  }
  int l_float_close(lua_State *L)
  {
    lua_pushboolean(L, api(L).close_float((int)luaL_checkinteger(L, 1), true));
    return 1;
  }
  int l_float_on_key(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto it = api(L).float_windows.find(w);
    if (it == api(L).float_windows.end())
      return (lua_pushboolean(L, 0), 1);
    if (it->second.key_callback >= 0)
      luaL_unref(L, LUA_REGISTRYINDEX, it->second.key_callback);
    lua_pushvalue(L, 2);
    it->second.key_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    return (lua_pushboolean(L, 1), 1);
  }
  int l_float_on_mouse(lua_State *L)
  {
    int w = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto it = api(L).float_windows.find(w);
    if (it == api(L).float_windows.end())
      return (lua_pushboolean(L, 0), 1);
    if (it->second.mouse_callback >= 0)
      luaL_unref(L, LUA_REGISTRYINDEX, it->second.mouse_callback);
    lua_pushvalue(L, 2);
    it->second.mouse_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    it->second.mouse = true;
    return (lua_pushboolean(L, 1), 1);
  }
  int l_job_capture(lua_State *L)
  {
    auto &a = api(L);
    const char *cmd = luaL_checkstring(L, 1);
    std::string cwd;
    int cb = 2;
    if (!lua_isfunction(L, 2))
    {
      cwd = luaL_optstring(L, 2, "");
      cb = 3;
    }
    luaL_checktype(L, cb, LUA_TFUNCTION);
    lua_pushvalue(L, cb);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string id = "job." + std::to_string(ref);
    a.lua_callbacks[id] = ref;
    if (!a.run_job_capture(cmd, cwd, id))
    {
      a.lua_callbacks.erase(id);
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
      lua_pushboolean(L, 0);
      return 1;
    }
    return 0;
  }
  int l_diagnostics_get(lua_State *L)
  {
    api(L).push_diagnostics(L);
    return 1;
  }
  int l_buf_set_var(lua_State *L)
  {
    api(L).set_buffer_var(L);
    return 0;
  }
  int l_buf_get_var(lua_State *L)
  {
    api(L).push_buffer_var(L);
    return 1;
  }
  int l_buf_del_var(lua_State *L)
  {
    api(L).delete_buffer_var(L);
    return 0;
  }
  int l_mark_set(lua_State *L)
  {
    api(L).set_mark(L);
    return 1;
  }
  int l_mark_get(lua_State *L)
  {
    api(L).push_mark(L);
    return 1;
  }
  int l_mark_jump(lua_State *L)
  {
    api(L).jump_mark(L);
    return 1;
  }
  int l_mark_del(lua_State *L)
  {
    api(L).delete_mark(L);
    return 1;
  }
  int l_mark_list(lua_State *L)
  {
    api(L).push_mark_list(L);
    return 1;
  }
  int l_status_register(lua_State *L)
  {
    api(L).register_status_segment(L);
    return 0;
  }
  int l_status_unregister(lua_State *L)
  {
    api(L).unregister_status_segment(L);
    return 0;
  }
  int l_config_get(lua_State *L)
  {
    api(L).lua_config_get(L, 0);
    return 1;
  }
  int l_config_get_number(lua_State *L)
  {
    api(L).lua_config_get(L, 1);
    return 1;
  }
  int l_config_get_bool(lua_State *L)
  {
    api(L).lua_config_get(L, 2);
    return 1;
  }
  int l_config_set(lua_State *L)
  {
    api(L).config_set_from_lua(L);
    return 0;
  }
  int l_config_unset(lua_State *L)
  {
    api(L).config_unset_from_lua(L);
    return 0;
  }
  int l_config_has(lua_State *L)
  {
    api(L).config_has_from_lua(L);
    return 1;
  }
  int l_config_keys(lua_State *L)
  {
    api(L).push_config_keys(L);
    return 1;
  }
  int l_config_path(lua_State *L)
  {
    api(L).push_config_path(L);
    return 1;
  }
  int l_editor_info(lua_State *L)
  {
    api(L).push_editor_info(L);
    return 1;
  }
  int l_theme_current(lua_State *L)
  {
    api(L).push_theme_current(L);
    return 1;
  }
  int l_git_info(lua_State *L)
  {
    api(L).push_git_info(L);
    return 1;
  }
  int l_git_status(lua_State *L)
  {
    api(L).push_git_status(L);
    return 1;
  }
  int l_git_stage(lua_State *L)
  {
    api(L).git_stage_from_lua(L);
    return 1;
  }
  int l_git_unstage(lua_State *L)
  {
    api(L).git_unstage_from_lua(L);
    return 1;
  }
  int l_git_stage_all(lua_State *L)
  {
    api(L).git_stage_all_from_lua(L);
    return 1;
  }
  int l_git_unstage_all(lua_State *L)
  {
    api(L).git_unstage_all_from_lua(L);
    return 1;
  }
  int l_git_commit(lua_State *L)
  {
    api(L).git_commit_from_lua(L);
    return 1;
  }
  int l_git_refresh(lua_State *L)
  {
    api(L).git_refresh_from_lua(L);
    return 0;
  }
  int l_task_list(lua_State *L)
  {
    api(L).push_task_list(L);
    return 1;
  }
  int l_task_run(lua_State *L)
  {
    api(L).run_task_from_lua(L);
    return 1;
  }
  int l_task_rerun(lua_State *L)
  {
    api(L).rerun_task_from_lua(L);
    return 1;
  }
  int l_symbols_list(lua_State *L)
  {
    api(L).push_symbols(L);
    return 1;
  }
  int l_dbg_configs(lua_State *L)
  {
    api(L).push_debugger_configs(L);
    return 1;
  }
  int l_dbg_run_config(lua_State *L)
  {
    api(L).run_debugger_config_from_lua(L);
    return 1;
  }
  int l_buf_current(lua_State *L)
  {
    api(L).push_buffer_current(L);
    return 1;
  }
  int l_buf_count(lua_State *L)
  {
    api(L).push_buffer_count(L);
    return 1;
  }
  int l_buf_text(lua_State *L)
  {
    api(L).push_buffer_text(L);
    return 1;
  }
  int l_buf_meta(lua_State *L)
  {
    api(L).push_buffer_meta(L);
    return 1;
  }
  int l_buf_selection(lua_State *L)
  {
    api(L).push_buffer_selection(L);
    return 1;
  }
  int l_buf_bookmarks(lua_State *L)
  {
    api(L).push_buffer_bookmarks(L);
    return 1;
  }
  int l_buf_folds(lua_State *L)
  {
    api(L).push_buffer_folds(L);
    return 1;
  }
  int l_clip_copy(lua_State *L)
  {
    api(L).clipboard_copy_from_lua(L);
    return 0;
  }
  int l_clip_cut(lua_State *L)
  {
    api(L).clipboard_cut_from_lua(L);
    return 0;
  }
  int l_clip_paste(lua_State *L)
  {
    api(L).clipboard_paste_from_lua(L);
    return 0;
  }
  int l_terminal_list(lua_State *L)
  {
    api(L).push_terminal_list(L);
    return 1;
  }
  int l_terminal_write(lua_State *L)
  {
    api(L).terminal_write_from_lua(L);
    return 1;
  }
  int l_terminal_close(lua_State *L)
  {
    api(L).terminal_close_from_lua(L);
    return 1;
  }
  int l_terminal_activate(lua_State *L)
  {
    api(L).terminal_activate_from_lua(L);
    return 1;
  }
  int l_terminal_spawn(lua_State *L)
  {
    api(L).terminal_spawn_from_lua(L);
    return 1;
  }
  int l_workspace_path(lua_State *L)
  {
    api(L).push_workspace_path(L);
    return 1;
  }
  int l_recent_files(lua_State *L)
  {
    api(L).push_recent_files(L);
    return 1;
  }
  int l_recent_workspaces(lua_State *L)
  {
    api(L).push_recent_workspaces(L);
    return 1;
  }
  int l_lsp_clients(lua_State *L)
  {
    api(L).push_lsp_clients(L);
    return 1;
  }
  int l_popup(lua_State *L)
  {
    api(L).popup_from_lua(L);
    return 0;
  }
  int l_ui_command_palette(lua_State *L)
  {
    api(L).host().io.open_command_palette(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_search_info(lua_State *L)
  {
    api(L).push_search_info(L);
    return 1;
  }
  int l_search_matches(lua_State *L)
  {
    api(L).push_search_matches(L);
    return 1;
  }
  int l_picker_active(lua_State *L)
  {
    api(L).push_picker_active(L);
    return 1;
  }
  int l_picker_info(lua_State *L)
  {
    api(L).push_picker_info(L);
    return 1;
  }
  int l_picker_items(lua_State *L)
  {
    api(L).push_picker_items(L);
    return 1;
  }
  int l_picker_accept(lua_State *L)
  {
    api(L).picker_accept_from_lua(L);
    return 1;
  }
  int l_picker_close(lua_State *L)
  {
    api(L).picker_close_from_lua(L);
    return 1;
  }
  int l_buf_tokens(lua_State *L)
  {
    api(L).push_buffer_tokens(L);
    return 1;
  }
  int l_lsp_request_hover(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 0);
    return 0;
  }
  int l_lsp_request_definition(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 1);
    return 0;
  }
  int l_lsp_request_symbols(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 2);
    return 0;
  }
  int l_lsp_diagnostics(lua_State *L)
  {
    api(L).push_lsp_diagnostics(L);
    return 1;
  }
  int l_lsp_results(lua_State *L)
  {
    api(L).push_lsp_last_results(L);
    return 1;
  }
  int l_lsp_completions(lua_State *L)
  {
    api(L).push_lsp_completions(L);
    return 1;
  }
  int l_lsp_request_completion(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 3);
    return 0;
  }
  int l_events_subscribe(lua_State *L)
  {
    api(L).events_subscribe_from_lua(L);
    return 1;
  }
  int l_events_unsubscribe(lua_State *L)
  {
    api(L).events_unsubscribe_from_lua(L);
    return 0;
  }
  int l_viewport_info(lua_State *L)
  {
    api(L).push_viewport_info(L);
    return 1;
  }
  int l_viewport_line_at(lua_State *L)
  {
    api(L).push_viewport_line_at(L);
    return 1;
  }
  int l_viewport_scroll_top(lua_State *L)
  {
    api(L).viewport_scroll_top_from_lua(L);
    return 0;
  }
  int l_viewport_scroll_lines(lua_State *L)
  {
    api(L).viewport_scroll_lines_from_lua(L);
    return 0;
  }
  int l_viewport_scroll_col(lua_State *L)
  {
    api(L).viewport_scroll_col_from_lua(L);
    return 0;
  }
  int l_viewport_reveal(lua_State *L)
  {
    api(L).viewport_reveal_from_lua(L);
    return 0;
  }
  int l_filetree_root(lua_State *L)
  {
    api(L).push_filetree_root(L);
    return 1;
  }
  int l_filetree_tree(lua_State *L)
  {
    api(L).push_filetree_tree(L);
    return 1;
  }
  int l_filetree_children(lua_State *L)
  {
    api(L).push_filetree_children(L);
    return 1;
  }
  int l_buf_select(lua_State *L)
  {
    api(L).buffer_select_from_lua(L);
    return 0;
  }
  int l_buf_clear_selection(lua_State *L)
  {
    api(L).buffer_clear_selection_from_lua(L);
    return 0;
  }
  int l_buf_lines(lua_State *L)
  {
    api(L).push_buffer_lines(L);
    return 1;
  }
  int l_timer_set_timeout(lua_State *L)
  {
    api(L).timer_set_from_lua(L, false);
    return 1;
  }
  int l_timer_set_interval(lua_State *L)
  {
    api(L).timer_set_from_lua(L, true);
    return 1;
  }
  int l_timer_clear(lua_State *L)
  {
    api(L).timer_clear_from_lua(L);
    return 0;
  }
  int l_dbg_state(lua_State *L)
  {
    api(L).push_debugger_state(L);
    return 1;
  }
  int l_dbg_breakpoints(lua_State *L)
  {
    api(L).push_debugger_breakpoints(L);
    return 1;
  }
  int l_dbg_toggle_bp(lua_State *L)
  {
    api(L).debugger_toggle_breakpoint_from_lua(L);
    return 1;
  }
  int l_dbg_has_bp(lua_State *L)
  {
    api(L).debugger_has_breakpoint_from_lua(L);
    return 1;
  }
  int l_dbg_request_stack(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 0);
    return 0;
  }
  int l_dbg_request_variables(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 1);
    return 0;
  }
  int l_dbg_request_threads(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 2);
    return 0;
  }
  int l_theme_palette(lua_State *L)
  {
    api(L).push_theme_palette(L);
    return 1;
  }
  int l_clip_get(lua_State *L)
  {
    api(L).push_clipboard_text(L);
    return 1;
  }
  int l_motion_word_next(lua_State *L)
  {
    api(L).motion_from_lua(L, 0);
    return 0;
  }
  int l_motion_word_prev(lua_State *L)
  {
    api(L).motion_from_lua(L, 1);
    return 0;
  }
  int l_motion_line_start(lua_State *L)
  {
    api(L).motion_from_lua(L, 2);
    return 0;
  }
  int l_motion_line_end(lua_State *L)
  {
    api(L).motion_from_lua(L, 3);
    return 0;
  }
  int l_motion_file_start(lua_State *L)
  {
    api(L).motion_from_lua(L, 4);
    return 0;
  }
  int l_motion_file_end(lua_State *L)
  {
    api(L).motion_from_lua(L, 5);
    return 0;
  }
  int l_motion_matching_bracket(lua_State *L)
  {
    api(L).motion_from_lua(L, 6);
    return 0;
  }
  int l_motion_select_function(lua_State *L)
  {
    api(L).motion_from_lua(L, 7);
    return 0;
  }
  int l_sidebar_info(lua_State *L)
  {
    api(L).push_sidebar_info(L);
    return 1;
  }
  int l_sidebar_set_view(lua_State *L)
  {
    api(L).sidebar_set_view_from_lua(L);
    return 0;
  }
  int l_lsp_disabled(lua_State *L)
  {
    api(L).push_lsp_disabled(L);
    return 1;
  }
  int l_lsp_set_enabled(lua_State *L)
  {
    api(L).lsp_set_enabled_from_lua(L);
    return 0;
  }
  int l_lsp_install(lua_State *L)
  {
    api(L).lsp_install_from_lua(L);
    return 1;
  }
  int l_lsp_remove(lua_State *L)
  {
    api(L).lsp_remove_from_lua(L);
    return 1;
  }
  int l_lsp_restart_all(lua_State *L)
  {
    api(L).lsp_restart_all_from_lua(L);
    return 0;
  }
  int l_lsp_hover_ui(lua_State *L)
  {
    // jot.lsp.hover_ui(fn) registers a Lua hover renderer; nil clears it.
    if (lua_isnoneornil(L, 1))
    {
      api(L).set_lsp_hover_ui_handler(L, 0);
    }
    else
    {
      luaL_checktype(L, 1, LUA_TFUNCTION);
      api(L).set_lsp_hover_ui_handler(L, 1);
    }
    return 0;
  }
  int l_buf_filetype(lua_State *L)
  {
    api(L).push_buffer_filetype(L);
    return 1;
  }
  int l_buf_get_line(lua_State *L)
  {
    api(L).push_buffer_get_line(L);
    return 1;
  }
  int l_git_diff(lua_State *L)
  {
    api(L).git_diff_from_lua(L);
    return 1;
  }
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
                    "jot.notify=show_message; jot.command=command; jot.autocmd=autocmd; "
                    "jot.execute=execute; jot.open_file=open; jot.save=save; "
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
  command_field(L, this, "close", "Close Pane");
  lua_setfield(L, -2, "pane");
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
  // Bundled feature: inline diagnostics as anchored decorations (see
  // lua/features/decorations.lua). Loaded after the API tables and before
  // user plugins so the autocmd is registered before any diagnostic arrives.
  jot_lua::load_bundled_lua_file(L, "features/decorations.lua", "Decorations");
  load_plugins();
  return true;
}
