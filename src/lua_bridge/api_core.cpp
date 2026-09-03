#include "editor.h"
#include "features/tree_sitter/manager.h"
#include "host_api.h"
#include "lua_bridge/api.h"
#include "tools/symbols/index.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <sys/wait.h>
#endif

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace fs = std::filesystem;
static LuaAPI *active_api = nullptr;

namespace
{
  std::string lower(std::string s)
  {
    for (char &c : s)
      c = (char)std::tolower((unsigned char)c);
    return s;
  }
  std::string key_name(const std::string &s)
  {
    std::string out;
    for (char c : s)
      if (!std::isspace((unsigned char)c))
        out += c;
    return out;
  }
  LuaAPI &api(lua_State *L)
  {
    void *ud = lua_touserdata(L, lua_upvalueindex(1));
    if (ud)
      return *static_cast<LuaAPI *>(ud);
    // Namespaced fields are plain C functions (no upvalue). Jot runs a single
    // LuaAPI instance, so fall back to the tracked active instance.
    if (active_api)
      return *active_api;
    static LuaAPI *const kNullFallback = nullptr;
    (void)kNullFallback;
    luaL_error(L, "Lua API not available");
    return *reinterpret_cast<LuaAPI *>(uintptr_t(1)); // unreachable
  }

  std::string lua_shell_quote(const std::string &value)
  {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value)
    {
      if (c == '"')
        out += "\"\"";
      else
        out.push_back(c);
    }
    return out + '"';
#else
    std::string out = "'";
    for (char c : value)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out.push_back(c);
    }
    return out + "'";
#endif
  }

  // Runs `command` through the shell on the calling thread, merging stderr into
  // stdout. Returns {output, exit_code}. Used by worker threads only.
  std::pair<std::string, int> lua_capture_shell(const std::string &command)
  {
    std::array<char, 512> buf{};
    std::string out;
    FILE *pipe = nullptr;
#ifdef _WIN32
    pipe = _popen((command + " 2>&1").c_str(), "r");
#else
    pipe = popen((command + " 2>&1").c_str(), "r");
#endif
    if (!pipe)
      return {std::string(), 1};
    while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr)
      out += buf.data();
    int status = 0;
#ifdef _WIN32
    status = _pclose(pipe);
#else
    status = pclose(pipe);
#endif
    int code = 1;
#ifndef _WIN32
    if (status != -1 && WIFEXITED(status))
      code = WEXITSTATUS(status);
#else
    code = status;
#endif
    return {std::move(out), code};
  }

  const char *lua_diag_severity_name(int severity)
  {
    switch (severity)
    {
    case 1:
      return "Error";
    case 2:
      return "Warning";
    case 3:
      return "Info";
    case 4:
      return "Hint";
    default:
      return "Diagnostic";
    }
  }
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

static int table_int(lua_State *L, int i, const char *k, int d)
{
  lua_getfield(L, i, k);
  int v = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : d;
  lua_pop(L, 1);
  return v;
}
static bool table_bool(lua_State *L, int i, const char *k, bool d)
{
  lua_getfield(L, i, k);
  bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : d;
  lua_pop(L, 1);
  return v;
}
static std::string table_string(lua_State *L, int i, const char *k, const std::string &d)
{
  lua_getfield(L, i, k);
  std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : d;
  lua_pop(L, 1);
  return v;
}

LuaAPI::LuaAPI(Editor *ed) : editor(ed), lua_state(nullptr), lua_initialized(false)
{
  active_api = this;
}
EditorHostAPI &LuaAPI::host()
{
  return *editor->host_api;
}
LuaAPI::~LuaAPI()
{
  cleanup();
  if (active_api == this)
    active_api = nullptr;
}
int LuaAPI::create_scratch_buffer(bool listed, bool scratch)
{
  int id = next_scratch_buffer++;
  scratch_buffers.emplace(id, LuaScratchBuffer{id, listed, scratch, true, {""}});
  return id;
}
bool LuaAPI::set_scratch_lines(
    int id, int start, int end, bool strict, const std::vector<std::string> &v)
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end() || !it->second.valid)
    return false;
  auto &l = it->second.lines;
  int n = (int)l.size();
  if (start < 0)
    start = n + start;
  if (end < 0)
    end = n + end;
  if (strict && (start < 0 || end < start || start > n || end > n))
    return false;
  start = std::clamp(start, 0, n);
  end = std::clamp(end, start, n);
  l.erase(l.begin() + start, l.begin() + end);
  l.insert(l.begin() + start, v.begin(), v.end());
  if (l.empty())
    l.push_back("");
  return true;
}
std::vector<std::string> LuaAPI::get_scratch_lines(int id, int start, int end, bool strict) const
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end() || !it->second.valid)
    return {};
  auto l = it->second.lines;
  int n = (int)l.size();
  if (start < 0)
    start = n + start;
  if (end < 0)
    end = n + end;
  if (strict && (start < 0 || end < start || start > n || end > n))
    return {};
  start = std::clamp(start, 0, n);
  end = std::clamp(end, start, n);
  return {l.begin() + start, l.begin() + end};
}
bool LuaAPI::delete_scratch_buffer(int id)
{
  auto it = scratch_buffers.find(id);
  if (it == scratch_buffers.end())
    return false;
  for (auto wi = float_windows.begin(); wi != float_windows.end();)
    if (wi->second.buffer == id)
    {
      if (lua_state)
      {
        if (wi->second.key_callback >= 0)
          luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, wi->second.key_callback);
        if (wi->second.mouse_callback >= 0)
          luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, wi->second.mouse_callback);
      }
      wi = float_windows.erase(wi);
    }
    else
      ++wi;
  it->second.valid = false;
  scratch_buffers.erase(it);
  return true;
}
bool LuaAPI::configure_float(int id, lua_State *L, int ti)
{
  auto it = float_windows.find(id);
  if (it == float_windows.end())
    return false;
  auto &f = it->second;
  f.x = table_int(L, ti, "col", f.x);
  f.y = table_int(L, ti, "row", f.y);
  f.col = f.x;
  f.row = f.y;
  f.w = table_int(L, ti, "width", f.w);
  f.h = table_int(L, ti, "height", f.h);
  f.relative = table_string(L, ti, "relative", f.relative);
  f.anchor = table_string(L, ti, "anchor", f.anchor);
  f.border = table_string(L, ti, "border", f.border);
  if (f.w < 1 || f.h < 1
      || (f.relative != "editor" && f.relative != "cursor" && f.relative != "win"
          && f.relative != "mouse")
      || (f.border != "none" && f.border != "single" && f.border != "double"
          && f.border != "rounded" && f.border != "custom"))
    return false;
  f.zindex = table_int(L, ti, "zindex", f.zindex);
  f.focusable = table_bool(L, ti, "focusable", f.focusable);
  f.mouse = table_bool(L, ti, "mouse", f.mouse);
  f.hide = table_bool(L, ti, "hide", f.hide);
  f.style_minimal = table_bool(L, ti, "style_minimal", f.style_minimal);
  f.title = table_string(L, ti, "title", f.title);
  f.footer = table_string(L, ti, "footer", f.footer);
  f.fg = table_int(L, ti, "fg", f.fg);
  f.bg = table_int(L, ti, "bg", f.bg);
  lua_getfield(L, ti, "style");
  if (lua_istable(L, -1))
  {
    f.style_minimal = table_bool(L, -1, "minimal", f.style_minimal);
    f.fg = table_int(L, -1, "fg", f.fg);
    f.bg = table_int(L, -1, "bg", f.bg);
  }
  lua_pop(L, 1);
  lua_getfield(L, ti, "border_chars");
  if (lua_istable(L, -1))
    for (int i = 0; i < 8; i++)
    {
      lua_rawgeti(L, -1, i + 1);
      if (lua_isstring(L, -1))
        f.custom_border[i] = lua_tostring(L, -1);
      lua_pop(L, 1);
    }
  lua_pop(L, 1);
  for (const char *key : {"on_key", "key_callback"})
  {
    lua_getfield(L, ti, key);
    if (lua_isfunction(L, -1))
    {
      if (f.key_callback >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, f.key_callback);
      lua_pushvalue(L, -1);
      f.key_callback = luaL_ref(L, LUA_REGISTRYINDEX);
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
  }
  for (const char *key : {"on_mouse", "mouse_callback"})
  {
    lua_getfield(L, ti, key);
    if (lua_isfunction(L, -1))
    {
      if (f.mouse_callback >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, f.mouse_callback);
      lua_pushvalue(L, -1);
      f.mouse_callback = luaL_ref(L, LUA_REGISTRYINDEX);
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
  }
  return true;
}
int LuaAPI::open_float(int buffer, bool enter, lua_State *L, int ti)
{
  if (scratch_buffers.find(buffer) == scratch_buffers.end())
    return 0;
  LuaFloatWindow f;
  f.handle = next_float_window++;
  f.buffer = buffer;
  f.enter = enter;
  f.creation_order = next_float_order++;
  float_windows.emplace(f.handle, f);
  if (!configure_float(f.handle, L, ti))
  {
    float_windows.erase(f.handle);
    return 0;
  }
  if (enter)
    current_float_window = f.handle;
  return f.handle;
}
bool LuaAPI::close_float(int id, bool)
{
  auto it = float_windows.find(id);
  if (it == float_windows.end())
    return false;
  if (lua_state)
  {
    if (it->second.key_callback >= 0)
      luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, it->second.key_callback);
    if (it->second.mouse_callback >= 0)
      luaL_unref((lua_State *)lua_state, LUA_REGISTRYINDEX, it->second.mouse_callback);
  }
  float_windows.erase(it);
  if (current_float_window == id)
    current_float_window = 0;
  return true;
}
bool LuaAPI::is_float_valid(int id) const
{
  return float_windows.find(id) != float_windows.end();
}
void LuaAPI::clear_floats()
{
  while (!float_windows.empty())
    close_float(float_windows.begin()->first, true);
  scratch_buffers.clear();
  current_float_window = 0;
}
bool LuaAPI::float_input(int ch, bool ctrl, bool shift, bool alt)
{
  if (!lua_state || !editor || !editor->event_loop_.is_main_thread())
    return false;
  std::vector<LuaFloatWindow *> fs;
  for (auto &x : float_windows)
    fs.push_back(&x.second);
  std::sort(fs.begin(),
            fs.end(),
            [](auto *a, auto *b)
            {
              return a->zindex != b->zindex ? a->zindex > b->zindex
                                            : a->creation_order > b->creation_order;
            });
  for (auto *f : fs)
  {
    if (f->hide || !f->focusable || f->key_callback < 0)
      continue;
    int handle = f->handle;
    lua_State *L = (lua_State *)lua_state;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, f->key_callback);
    lua_newtable(L);
    lua_pushinteger(L, ch);
    lua_setfield(L, -2, "key");
    lua_pushboolean(L, ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, alt);
    lua_setfield(L, -2, "alt");
    lua_pushinteger(L, handle);
    lua_setfield(L, -2, "window");
    int ok = lua_pcall(L, 1, 1, 0) == LUA_OK;
    bool consume = ok && lua_toboolean(L, -1);
    if (!ok)
      std::cerr << "Lua float key callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    if (consume)
    {
      current_float_window = handle;
      return true;
    }
  }
  return false;
}
bool LuaAPI::float_mouse(int x,
                         int y,
                         int button,
                         bool pressed,
                         bool released,
                         bool motion,
                         bool ctrl,
                         bool shift,
                         bool alt)
{
  if (!lua_state || !editor || !editor->event_loop_.is_main_thread())
    return false;
  for (auto it = float_windows.rbegin(); it != float_windows.rend(); ++it)
  {
    auto &f = it->second;
    if (f.hide || f.mouse_callback < 0)
      continue;
    int handle = f.handle;
    int bx = f.x, by = f.y;
    if (x < bx || x >= bx + f.w || y < by || y >= by + f.h)
      continue;
    lua_State *L = (lua_State *)lua_state;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, f.mouse_callback);
    lua_newtable(L);
    lua_pushinteger(L, x - bx);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, y - by);
    lua_setfield(L, -2, "row");
    lua_pushinteger(L, button);
    lua_setfield(L, -2, "button");
    lua_pushboolean(L, pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushboolean(L, released);
    lua_setfield(L, -2, "released");
    lua_pushboolean(L, motion);
    lua_setfield(L, -2, "motion");
    lua_pushboolean(L, ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, alt);
    lua_setfield(L, -2, "alt");
    int ok = lua_pcall(L, 1, 1, 0) == LUA_OK;
    bool consume = ok && lua_toboolean(L, -1);
    if (!ok)
      std::cerr << "Lua float mouse callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    if (consume)
    {
      current_float_window = handle;
      return true;
    }
  }
  return false;
}
void LuaAPI::render_floats()
{
  if (!editor || !editor->ui)
    return;
  int rw = editor->ui->get_render_width(),
      rh = std::max(1, editor->ui->get_height() - editor->status_height);
  std::vector<LuaFloatWindow *> fs;
  for (auto &x : float_windows)
    if (!x.second.hide)
      fs.push_back(&x.second);
  std::sort(fs.begin(),
            fs.end(),
            [](auto *a, auto *b)
            {
              return a->zindex != b->zindex ? a->zindex < b->zindex
                                            : a->creation_order < b->creation_order;
            });
  for (auto *f : fs)
  {
    int x = f->col, y = f->row;
    if (f->relative == "cursor" && !editor->panes.empty())
    {
      auto &p = editor->get_pane();
      auto &b = editor->get_buffer(p.buffer_id);
      x = p.x + 9 + b.cursor.x;
      y = p.y + editor->tab_height + b.cursor.y - b.scroll_offset;
    }
    else if (f->relative == "win" && !editor->panes.empty())
    {
      auto &p = editor->get_pane();
      x = p.x + f->col;
      y = p.y + f->row;
    }
    if (f->anchor.find('S') != std::string::npos)
      y -= f->h;
    if (f->anchor.find('E') != std::string::npos)
      x -= f->w;
    x = std::clamp(x, 0, std::max(0, rw - f->w));
    y = std::clamp(y, 0, std::max(0, rh - f->h));
    f->x = x;
    f->y = y;
    UIRect r{x, y, std::min(f->w, rw - x), std::min(f->h, rh - y)};
    editor->ui->fill_rect(r, " ", f->fg, f->bg);
    if (f->border != "none")
    {
      std::array<std::string, 8> b = {"─", "│", "─", "│", "┌", "┐", "┘", "└"};
      if (f->border == "double")
        b = {"═", "║", "═", "║", "╔", "╗", "╝", "╚"};
      if (f->border == "rounded")
        b = {"─", "│", "─", "│", "╭", "╮", "╯", "╰"};
      if (f->border == "custom")
        b = f->custom_border;
      editor->ui->draw_text(x, y, b[4], f->fg, f->bg);
      editor->ui->draw_text(x + f->w - 1, y, b[5], f->fg, f->bg);
      editor->ui->draw_text(x, y + f->h - 1, b[7], f->fg, f->bg);
      editor->ui->draw_text(x + f->w - 1, y + f->h - 1, b[6], f->fg, f->bg);
      for (int i = 1; i < f->w - 1; i++)
      {
        editor->ui->draw_text(x + i, y, b[0], f->fg, f->bg);
        editor->ui->draw_text(x + i, y + f->h - 1, b[2], f->fg, f->bg);
      }
      for (int i = 1; i < f->h - 1; i++)
      {
        editor->ui->draw_text(x, y + i, b[3], f->fg, f->bg);
        editor->ui->draw_text(x + f->w - 1, y + i, b[1], f->fg, f->bg);
      }
    }
    auto bi = scratch_buffers.find(f->buffer);
    if (bi == scratch_buffers.end())
      continue;
    const int inset = (f->border == "none" ? 0 : 1);
    int ix = x + inset;
    int iy = y + inset;
    int iw = std::max(0, r.w - (f->border == "none" ? 0 : 2));
    int ih = std::max(0, r.h - (f->border == "none" ? 0 : 2));
    for (int i = 0; i < ih && i < (int)bi->second.lines.size(); i++)
      editor->ui->draw_text(ix, iy + i, ui_truncate_cells(bi->second.lines[i], iw), f->fg, f->bg);
    if (!f->title.empty())
    {
      const std::string title_text = ui_truncate_cells(" " + f->title + " ", std::max(0, r.w - 2));
      editor->ui->draw_text(x + 1, y, title_text, f->fg, f->bg, true);
    }
    if (!f->footer.empty())
    {
      const std::string footer_text =
          ui_truncate_cells(" " + f->footer + " ", std::max(0, r.w - 2));
      editor->ui->draw_text(x + 1, y + r.h - 1, footer_text, f->fg, f->bg);
    }
  }
}
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
  load_plugins();
  return true;
}
void LuaAPI::cleanup()
{
  if (!lua_state)
    return;
  cancel_all_timers();
  clear_floats();
  lua_close(static_cast<lua_State *>(lua_state));
  lua_state = nullptr;
  lua_callbacks.clear();
  lua_initialized = false;
  timer_entries_.clear();
}
void LuaAPI::clear_runtime_state()
{
  plugin_commands.clear();
  plugin_keymaps.clear();
  plugin_autocmds.clear();
  plugin_panels.clear();
  status_segments_.clear();
  plugin_load_status.clear();
  cancel_all_timers();
  clear_floats();
  if (lua_state)
  {
    for (auto &x : lua_callbacks)
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, x.second);
    for (auto &kv : event_subscribers_)
      for (auto &s : kv.second)
        luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, s.ref);
  }
  lua_callbacks.clear();
  event_subscribers_.clear();
  pending_lsp_hover.clear();
  pending_lsp_definition.clear();
  pending_lsp_symbols.clear();
  pending_lsp_completion.clear();
  pending_debugger_stack.clear();
  pending_debugger_variables.clear();
  pending_debugger_threads.clear();
  pending_debugger_session = -1;
  edit_snapshots_.clear();
  last_edit_ = LuaEditDelta{};
}
bool LuaAPI::load_script_path(const std::string &module, const std::string &path)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  if (luaL_loadfile(L, path.c_str()) || lua_pcall(L, 0, 0, 0))
  {
    std::string e = lua_tostring(L, -1);
    lua_pop(L, 1);
    plugin_load_status.push_back({module, path, false, e});
    if (editor)
      editor->set_message("Plugin failed: " + module);
    lua_settop(L, top);
    return false;
  }
  plugin_load_status.push_back({module, path, true, ""});
  return true;
}
void LuaAPI::load_config_file()
{
  if (!lua_initialized)
    return;
  fs::path root;
  const char *env = getenv("JOT_CONFIG_HOME");
  if (env && *env)
    root = env;
  else
  {
    const char *home = getenv("HOME");
    const char *app = getenv("APPDATA");
    if (app && *app)
      root = fs::path(app) / "jot";
    else if (home)
      root = fs::path(home) / ".config" / "jot";
  }
  if (root.empty())
    return;
  fs::path cfg = root / "config.lua";
  if (fs::exists(cfg))
    load_script_path("jot_config", cfg.string());
}
void LuaAPI::load_plugins()
{
  if (!lua_initialized)
    return;
  fs::path root;
  const char *env = getenv("JOT_CONFIG_HOME");
  if (env && *env)
    root = env;
  else
  {
    const char *home = getenv("HOME");
    const char *app = getenv("APPDATA");
    if (app && *app)
      root = fs::path(app) / "jot";
    else if (home)
      root = fs::path(home) / ".config" / "jot";
  }
  if (root.empty())
    return;
  fs::create_directories(root / "plugins");
  clear_runtime_state();
  load_config_file();
  fs::path init = root / "init.lua";
  if (fs::exists(init))
    load_script_path("jot_init", init.string());
  std::vector<fs::path> files;
  for (auto &e : fs::directory_iterator(root / "plugins"))
  {
    if (e.is_regular_file() && e.path().extension() == ".lua")
      files.push_back(e.path());
    else if (e.is_directory() && fs::exists(e.path() / "plugin.lua"))
      files.push_back(e.path() / "plugin.lua");
  }
  std::sort(files.begin(), files.end());
  for (auto &p : files)
    load_script_path("jot_plugin_" + p.stem().string(), p.string());
  if (editor)
    editor->apply_config_live();
  fire_autocmd("EditorEnter");
}
void LuaAPI::reload_plugins()
{
  load_plugins();
  fire_autocmd("PluginReload");
  if (editor)
    editor->set_message("Reloaded " + std::to_string(plugin_load_status.size())
                        + " plugin file(s)");
}
bool LuaAPI::call_callback_string(const std::string &id, const std::string &arg)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
    return false;
  auto it = lua_callbacks.find(id);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  lua_pushstring(L, arg.c_str());
  bool ok = lua_pcall(L, 1, 0, 0) == LUA_OK;
  if (!ok)
  {
    std::cerr << "Lua callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return ok;
}
bool LuaAPI::call_callback_event(const std::string &id,
                                 const std::string &event,
                                 const std::string &filepath,
                                 int buffer)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
    return false;
  auto it = lua_callbacks.find(id);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  lua_newtable(L);
  lua_pushstring(L, event.c_str());
  lua_setfield(L, -2, "event");
  lua_pushstring(L, filepath.c_str());
  lua_setfield(L, -2, "path");
  if (buffer < 0)
    lua_pushnil(L);
  else
    lua_pushinteger(L, buffer + 1);
  lua_setfield(L, -2, "buffer");
  if (buffer >= 0 && buffer < (int)editor->buffers.size())
  {
    const Cursor pos = editor->buffers[(size_t)buffer].cursor;
    lua_pushinteger(L, pos.y + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, pos.x + 1);
    lua_setfield(L, -2, "column");
  }
  if (event == "BufChange" && last_edit_.valid)
  {
    lua_pushinteger(L, last_edit_.start_line);
    lua_setfield(L, -2, "edit_start_line");
    lua_pushinteger(L, last_edit_.start_col);
    lua_setfield(L, -2, "edit_start_col");
    lua_pushinteger(L, last_edit_.end_line);
    lua_setfield(L, -2, "edit_end_line");
    lua_pushinteger(L, last_edit_.end_col);
    lua_setfield(L, -2, "edit_end_col");
    lua_pushstring(L, last_edit_.inserted.c_str());
    lua_setfield(L, -2, "edit_inserted");
    lua_pushstring(L, last_edit_.removed.c_str());
    lua_setfield(L, -2, "edit_removed");
    lua_pushboolean(L, last_edit_.multiline);
    lua_setfield(L, -2, "edit_multiline");
  }
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua event callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}
void LuaAPI::on_buffer_open(const std::string &f)
{
  if (editor)
    editor->notify_lsp_open(f);
  fire_autocmd("BufOpen", f, -1);
  emit_buffer_event("buffer.open", f);
}
static void lua_split_edit_lines(const std::string &text, std::vector<std::string> &out)
{
  size_t start = 0;
  for (size_t i = 0; i < text.size(); i++)
  {
    if (text[i] == '\n')
    {
      out.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  out.push_back(text.substr(start));
}

static size_t lua_common_prefix_bytes(const std::string &a, const std::string &b)
{
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n && a[i] == b[i])
    i++;
  return i;
}

static size_t lua_common_suffix_bytes(const std::string &a, const std::string &b)
{
  const size_t na = a.size(), nb = b.size();
  size_t i = 0;
  while (i < na && i < nb && a[na - 1 - i] == b[nb - 1 - i])
    i++;
  return i;
}

// Joins lines[first..last] (inclusive). The first line loses its leading
// cut_left bytes; the last line is truncated to its first end_col bytes.
static std::string lua_join_edit_range(const std::vector<std::string> &lines,
                                       size_t first,
                                       size_t last,
                                       size_t cut_left,
                                       size_t end_col)
{
  std::string out;
  if (first > last || first >= lines.size())
    return out;
  for (size_t i = first; i <= last && i < lines.size(); i++)
  {
    const std::string &line = lines[i];
    size_t from = 0;
    size_t to = line.size();
    if (i == first)
      from = std::min(cut_left, line.size());
    if (i == last)
    {
      to = std::min(end_col, line.size());
      if (i == first && to < from)
        to = from;
    }
    if (i > first)
      out += '\n';
    if (to > from)
      out.append(line, from, to - from);
  }
  return out;
}

// Computes a best-effort line/column delta between two versions of a buffer
// using common-prefix/common-suffix line matching. Exact for single-line
// typing/paste/delete edits and block inserts; approximations suffice for
// anything more exotic (undo spans, merges).
static LuaEditDelta compute_edit_delta(const std::string &prev, const std::string &now)
{
  LuaEditDelta d;
  if (prev == now)
    return d;
  std::vector<std::string> a, b;
  lua_split_edit_lines(prev, a);
  lua_split_edit_lines(now, b);
  size_t ai = 0, bi = 0;
  while (ai < a.size() && bi < b.size() && a[ai] == b[bi])
  {
    ai++;
    bi++;
  }
  size_t aend = a.size(), bend = b.size();
  while (aend > ai && bend > bi && a[aend - 1] == b[bend - 1])
  {
    aend--;
    bend--;
  }
  if (ai == aend && bi == bend)
    return d;

  const std::string &old_first = ai < a.size() ? a[ai] : std::string();
  const std::string &new_first = bi < b.size() ? b[bi] : std::string();
  const size_t p = lua_common_prefix_bytes(old_first, new_first);
  const bool old_has = aend > ai;
  const bool new_has = bend > bi;
  size_t suffix = 0;
  if (old_has && new_has)
  {
    suffix = lua_common_suffix_bytes(a[aend - 1], b[bend - 1]);
    // Single-line changes must not double-count the common middle.
    if (aend - ai == 1 && bend - bi == 1)
    {
      suffix = std::min(suffix, std::min(a[ai].size(), b[bi].size()) - p);
    }
  }
  const size_t old_end = old_has && a[aend - 1].size() >= suffix ? a[aend - 1].size() - suffix : 0;
  const size_t new_end = new_has && b[bend - 1].size() >= suffix ? b[bend - 1].size() - suffix : 0;

  d.start_line = (int)bi + 1;
  d.start_col = (int)p + 1;
  d.multiline = (aend - ai > 1) || (bend - bi > 1);
  if (new_has)
  {
    d.end_line = (int)bend;       // exclusive end line (1-based, one past last)
    d.end_col = (int)new_end + 1; // exclusive column on the last line
    d.inserted = lua_join_edit_range(b, bi, bend - 1, p, new_end);
  }
  else
  {
    d.end_line = d.start_line;
    d.end_col = d.start_col;
  }
  if (old_has)
  {
    d.removed = lua_join_edit_range(a, ai, aend - 1, p, old_end);
  }
  if (d.inserted.find('\n') != std::string::npos || d.removed.find('\n') != std::string::npos)
  {
    d.multiline = true;
  }
  d.valid = true;
  return d;
}

void LuaAPI::on_buffer_change(const std::string &f, const std::string &)
{
  last_edit_ = LuaEditDelta{};
  if (editor)
  {
    editor->notify_lsp_change(f);
    // The persistent Outline panel rebuilds lazily from this dirty flag.
    editor->note_outline_edit();
  }
  int index = -1;
  if (editor)
  {
    for (size_t i = 0; i < editor->buffers.size(); i++)
    {
      if (editor->buffers[i].filepath == f)
      {
        index = (int)i;
        break;
      }
    }
    if (index < 0 && !f.empty())
    {
      std::error_code ec;
      for (size_t i = 0; i < editor->buffers.size() && index < 0; i++)
      {
        const std::string &other = editor->buffers[i].filepath;
        if (!other.empty() && fs::exists(other, ec) && fs::exists(f, ec)
            && fs::equivalent(other, f, ec))
        {
          index = (int)i;
        }
      }
    }
    if (index < 0 && f.empty() && editor->current_buffer >= 0
        && editor->current_buffer < (int)editor->buffers.size())
    {
      index = editor->current_buffer;
    }
  }
  // Compute a rich edit delta only when a plugin actually listens to
  // BufChange, and skip huge/lazy buffers to keep the typing hot path cheap.
  if (index >= 0 && index < (int)editor->buffers.size())
  {
    bool has_listener = false;
    for (const auto &x : plugin_autocmds)
    {
      if (x.event == "BufChange")
      {
        has_listener = true;
        break;
      }
    }
    if (has_listener)
    {
      FileBuffer &buf = editor->buffers[(size_t)index];
      if (!buf.is_lazy() && buf.line_count() <= 100000)
      {
        std::string cur;
        for (size_t i = 0; i < buf.lines.size(); i++)
        {
          if (i)
            cur += '\n';
          cur += buf.lines[i];
        }
        // Unnamed buffers share an empty filepath; key them by index.
        const std::string key = f.empty() ? ("\x01" + std::to_string(index)) : f;
        auto it = edit_snapshots_.find(key);
        if (it != edit_snapshots_.end())
        {
          last_edit_ = compute_edit_delta(it->second, cur);
        }
        edit_snapshots_[key] = std::move(cur);
        while (edit_snapshots_.size() > 256)
        {
          edit_snapshots_.erase(edit_snapshots_.begin());
        }
      }
    }
  }
  fire_autocmd("BufChange", f, index);
  last_edit_ = LuaEditDelta{};
}
void LuaAPI::on_buffer_save(const std::string &f)
{
  if (editor)
    editor->notify_lsp_save(f);
  fire_autocmd("BufSave", f, -1);
  emit_buffer_event("buffer.save", f);
}
void LuaAPI::register_command(const std::string &n, const std::string &c, const std::string &d)
{
  if (n.empty() || c.empty())
    return;
  for (auto &x : plugin_commands)
    if (x.name == lower(n))
    {
      x.callback = c;
      x.detail = d;
      return;
    }
  plugin_commands.push_back({lower(n), c, d});
}
bool LuaAPI::run_plugin_command(const std::string &n, const std::string &a)
{
  for (auto &x : plugin_commands)
    if (x.name == n)
    {
      call_callback_string(x.callback, a);
      return true;
    }
  return false;
}
bool LuaAPI::run_plugin_keymap(const std::string &k, const std::string &m)
{
  for (auto &x : plugin_keymaps)
    if (x.key == key_name(k) && (x.mode == "global" || x.mode == m))
    {
      if (!x.command.empty() && editor)
      {
        if (!x.command.empty() && x.command[0] == ':')
          editor->execute_ex_command(x.command);
        else if (editor->host_api)
          editor->host_api->io.execute_command(x.command);
      }
      else
        call_callback_string(x.callback, "");
      return true;
    }
  return false;
}
void LuaAPI::fire_autocmd(const std::string &e, const std::string &f, int b)
{
  for (auto &x : plugin_autocmds)
    if (x.event == e)
      call_callback_event(x.callback, e, f, b);
}
void LuaAPI::register_keymap(const std::string &k,
                             const std::string &c,
                             const std::string &cmd,
                             const std::string &d,
                             const std::string &m)
{
  plugin_keymaps.push_back({key_name(k), c, cmd, d, m});
}
void LuaAPI::register_autocmd(const std::string &e, const std::string &c)
{
  plugin_autocmds.push_back({e, c});
}
void LuaAPI::register_panel(const std::string &n, const std::string &c, const std::string &t)
{
  plugin_panels.push_back({n, c, t});
}
bool LuaAPI::run_plugin_callback(const std::string &c, const std::string &a)
{
  return call_callback_string(c, a);
}
std::string LuaAPI::get_current_buffer()
{
  return editor && editor->host_api ? editor->host_api->core.buffer_content() : "";
}
void LuaAPI::set_current_buffer(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_buffer_content(s);
}
std::string LuaAPI::get_selection()
{
  return editor && editor->host_api ? editor->host_api->core.selected_text() : "";
}
void LuaAPI::replace_selection(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.replace_selection(s);
}
void LuaAPI::insert_text(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.insert_text(s);
}
std::pair<int, int> LuaAPI::get_cursor()
{
  return editor && editor->host_api ? editor->host_api->core.cursor() : std::pair<int, int>{0, 0};
}
void LuaAPI::set_cursor(int l, int c)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_cursor(l, c);
}
std::string LuaAPI::current_file()
{
  return editor && editor->host_api ? editor->host_api->core.current_file() : "";
}
void LuaAPI::open_file(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.open_file(s);
}
void LuaAPI::save_current_file()
{
  if (editor && editor->host_api)
    editor->host_api->io.save_current_file();
}
void LuaAPI::execute_command(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.execute_command(s);
}
void LuaAPI::run_job(const std::string &a, const std::string &b, const std::string &c)
{
  if (editor && editor->host_api)
    editor->host_api->io.run_job(a, b, c);
}

bool LuaAPI::run_job_capture(const std::string &command,
                             const std::string &cwd,
                             const std::string &callback)
{
  if (!editor || !editor->task_queue_ || command.empty())
    return false;
  std::string full = command;
  if (!cwd.empty())
    full = "cd " + lua_shell_quote(cwd) + " && " + command;
  return editor->task_queue_->submit_val<std::pair<std::string, int>>(
      [full]() { return lua_capture_shell(full); },
      [this, callback](std::pair<std::string, int> res)
      {
        if (!editor || !editor->running)
          return;
        deliver_job_result(callback, res.first, res.second);
      });
}

bool LuaAPI::deliver_job_result(const std::string &callback,
                                const std::string &output,
                                int exit_code)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
  {
    return false;
  }
  auto it = lua_callbacks.find(callback);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  int ref = it->second;
  lua_callbacks.erase(it);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  lua_newtable(L);
  lua_pushstring(L, output.c_str());
  lua_setfield(L, -2, "output");
  lua_pushinteger(L, exit_code);
  lua_setfield(L, -2, "exit_code");
  lua_pushboolean(L, exit_code == 0);
  lua_setfield(L, -2, "ok");
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua job callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return true;
}

int LuaAPI::resolve_buffer_arg(lua_State *L, int arg_index)
{
  if (!editor || editor->buffers.empty())
    return -1;
  int idx = -1;
  if (arg_index <= lua_gettop(L) && !lua_isnil(L, arg_index))
  {
    if (lua_isnumber(L, arg_index))
    {
      idx = (int)lua_tointeger(L, arg_index) - 1;
    }
    else if (lua_isstring(L, arg_index))
    {
      std::string path = lua_tostring(L, arg_index);
      for (size_t i = 0; i < editor->buffers.size(); i++)
      {
        if (editor->buffers[i].filepath == path)
        {
          idx = (int)i;
          break;
        }
      }
    }
  }
  else
  {
    idx = editor->current_buffer;
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
    return -1;
  return idx;
}

void LuaAPI::push_diagnostics(lua_State *L)
{
  int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  int n = 1;
  for (const auto &d : buf.diagnostics)
  {
    lua_newtable(L);
    lua_pushinteger(L, d.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, d.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, d.end_line + 1);
    lua_setfield(L, -2, "end_line");
    lua_pushinteger(L, d.end_col + 1);
    lua_setfield(L, -2, "end_col");
    lua_pushinteger(L, d.severity);
    lua_setfield(L, -2, "severity");
    lua_pushstring(L, lua_diag_severity_name(d.severity));
    lua_setfield(L, -2, "severity_name");
    lua_pushstring(L, d.message.c_str());
    lua_setfield(L, -2, "message");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::set_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int t = lua_type(L, 2);
  if (t != LUA_TSTRING && t != LUA_TNUMBER && t != LUA_TBOOLEAN)
  {
    luaL_error(L, "set_var value must be a string, number, or boolean");
    return;
  }
  lua_pushvalue(L, 2);
  const char *s = lua_tostring(L, -1);
  std::string value = s ? s : "";
  lua_pop(L, 1);
  int idx = resolve_buffer_arg(L, 3);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars[name] = std::move(value);
}

void LuaAPI::push_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  auto it = editor->buffers[(size_t)idx].lua_vars.find(name);
  if (it == editor->buffers[(size_t)idx].lua_vars.end())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, it->second.c_str());
}

void LuaAPI::delete_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars.erase(name);
}

void LuaAPI::set_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (std::islower((unsigned char)c))
  {
    buf.marks[c] = buf.cursor;
    lua_pushboolean(L, 1);
    return;
  }
  if (std::isupper((unsigned char)c))
  {
    if (buf.filepath.empty())
    {
      lua_pushboolean(L, 0);
      return;
    }
    GlobalMark gm;
    gm.filepath = buf.filepath;
    gm.line = buf.cursor.y;
    gm.col = buf.cursor.x;
    editor->global_marks[c] = gm;
    lua_pushboolean(L, 1);
    return;
  }
  lua_pushboolean(L, 0);
}

void LuaAPI::push_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushnil(L);
    return;
  }
  char c = s[0];
  if (std::islower((unsigned char)c))
  {
    if (!editor || editor->buffers.empty() || editor->current_buffer < 0
        || editor->current_buffer >= (int)editor->buffers.size())
    {
      lua_pushnil(L);
      return;
    }
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    auto it = buf.marks.find(c);
    if (it == buf.marks.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_newtable(L);
    lua_pushinteger(L, editor->current_buffer + 1);
    lua_setfield(L, -2, "buffer");
    lua_pushstring(L, buf.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, it->second.y + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, it->second.x + 1);
    lua_setfield(L, -2, "col");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "global");
    return;
  }
  if (std::isupper((unsigned char)c) && editor)
  {
    auto it = editor->global_marks.find(c);
    if (it == editor->global_marks.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_newtable(L);
    lua_pushstring(L, it->second.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, it->second.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, it->second.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "global");
    return;
  }
  lua_pushnil(L);
}

void LuaAPI::delete_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (std::islower((unsigned char)c) && editor && editor->current_buffer >= 0
      && editor->current_buffer < (int)editor->buffers.size())
  {
    lua_pushboolean(L, editor->buffers[(size_t)editor->current_buffer].marks.erase(c) > 0);
    return;
  }
  if (std::isupper((unsigned char)c) && editor)
  {
    lua_pushboolean(L, editor->global_marks.erase(c) > 0);
    return;
  }
  lua_pushboolean(L, 0);
}

void LuaAPI::jump_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int target_line = -1, target_col = 0;
  std::string target_path;
  if (std::islower((unsigned char)c))
  {
    if (editor->current_buffer < 0 || editor->current_buffer >= (int)editor->buffers.size())
    {
      lua_pushboolean(L, 0);
      return;
    }
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    auto it = buf.marks.find(c);
    if (it == buf.marks.end())
    {
      lua_pushboolean(L, 0);
      return;
    }
    target_line = it->second.y;
    target_col = it->second.x;
    target_path = buf.filepath;
  }
  else if (std::isupper((unsigned char)c))
  {
    auto it = editor->global_marks.find(c);
    if (it == editor->global_marks.end())
    {
      lua_pushboolean(L, 0);
      return;
    }
    target_line = it->second.line;
    target_col = it->second.col;
    target_path = it->second.filepath;
  }
  else
  {
    lua_pushboolean(L, 0);
    return;
  }
  // Ensure the owning buffer is open and current.
  int idx = -1;
  for (size_t i = 0; i < editor->buffers.size(); i++)
  {
    if (editor->buffers[i].filepath == target_path)
    {
      idx = (int)i;
      break;
    }
  }
  if (idx < 0)
  {
    if (target_path.empty())
    {
      lua_pushboolean(L, 0);
      return;
    }
    editor->open_file(target_path);
    for (size_t i = 0; i < editor->buffers.size(); i++)
    {
      if (editor->buffers[i].filepath == target_path)
      {
        idx = (int)i;
        break;
      }
    }
  }
  if (idx < 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  if (editor->current_buffer != idx)
  {
    editor->host_api->core.switch_buffer(idx);
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.line_count() == 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  buf.cursor.y = std::clamp(target_line, 0, (int)buf.line_count() - 1);
  buf.cursor.x = std::clamp(target_col, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  editor->clear_selection();
  editor->ensure_cursor_visible();
  editor->needs_redraw = true;
  lua_pushboolean(L, 1);
}

void LuaAPI::push_mark_list(lua_State *L)
{
  lua_newtable(L);
  int n = 1;
  if (!editor)
    return;
  if (editor->current_buffer >= 0 && editor->current_buffer < (int)editor->buffers.size())
  {
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    for (const auto &kv : buf.marks)
    {
      lua_newtable(L);
      lua_pushlstring(L, &kv.first, 1);
      lua_setfield(L, -2, "name");
      lua_pushboolean(L, 0);
      lua_setfield(L, -2, "global");
      lua_pushinteger(L, editor->current_buffer + 1);
      lua_setfield(L, -2, "buffer");
      lua_pushstring(L, buf.filepath.c_str());
      lua_setfield(L, -2, "path");
      lua_pushinteger(L, kv.second.y + 1);
      lua_setfield(L, -2, "line");
      lua_pushinteger(L, kv.second.x + 1);
      lua_setfield(L, -2, "col");
      lua_rawseti(L, -2, n++);
    }
  }
  for (const auto &kv : editor->global_marks)
  {
    lua_newtable(L);
    lua_pushlstring(L, &kv.first, 1);
    lua_setfield(L, -2, "name");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "global");
    lua_pushstring(L, kv.second.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, kv.second.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, kv.second.col + 1);
    lua_setfield(L, -2, "col");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::register_status_segment(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_getfield(L, 2, "text");
  if (!lua_isfunction(L, -1))
  {
    luaL_error(L, "status segment requires a 'text' function");
    return;
  }
  PluginStatusSegment seg;
  seg.name = name;
  lua_pushvalue(L, -1);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  seg.callback = "status." + std::to_string(ref);
  lua_callbacks[seg.callback] = ref;
  lua_pop(L, 1); // pop the fetched 'text' value (consumed by luaL_ref)
  seg.side = table_string(L, 2, "side", "right");
  seg.priority = table_int(L, 2, "priority", 50);
  seg.fg = table_int(L, 2, "fg", -1);
  for (auto it = status_segments_.begin(); it != status_segments_.end(); ++it)
  {
    if (it->name != seg.name)
      continue;
    auto old = lua_callbacks.find(it->callback);
    if (old != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, old->second);
      lua_callbacks.erase(old);
    }
    status_segments_.erase(it);
    break;
  }
  status_segments_.push_back(std::move(seg));
}

void LuaAPI::unregister_status_segment(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  for (auto it = status_segments_.begin(); it != status_segments_.end(); ++it)
  {
    if (it->name != name)
      continue;
    auto old = lua_callbacks.find(it->callback);
    if (old != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, old->second);
      lua_callbacks.erase(old);
    }
    status_segments_.erase(it);
    break;
  }
}

std::vector<RenderedStatusSegment> LuaAPI::render_status_segments()
{
  std::vector<RenderedStatusSegment> out;
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread()
      || status_segments_.empty())
  {
    return out;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  for (const auto &seg : status_segments_)
  {
    auto it = lua_callbacks.find(seg.callback);
    if (it == lua_callbacks.end())
      continue;
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    lua_pushstring(L, seg.name.c_str());
    if (lua_pcall(L, 1, 2, 0) != LUA_OK)
    {
      std::cerr << "Lua status segment error (" << seg.name << "): " << lua_tostring(L, -1) << "\n";
      lua_settop(L, top);
      continue;
    }
    RenderedStatusSegment rs;
    rs.side = seg.side;
    rs.priority = seg.priority;
    rs.fg = seg.fg;
    const char *text = lua_tostring(L, -2);
    if (text)
      rs.text = text;
    if (lua_isnumber(L, -1))
    {
      int f = (int)lua_tointeger(L, -1);
      if (f >= 0 && f <= 255)
        rs.fg = f;
    }
    lua_settop(L, top);
    if (!rs.text.empty())
      out.push_back(std::move(rs));
  }
  lua_settop(L, top);
  return out;
}
void LuaAPI::show_picker(const std::string &a, const std::string &b, const std::string &c)
{
  if (editor && editor->host_api)
    editor->host_api->io.show_plugin_picker(a, b, c);
}
void LuaAPI::show_panel(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.show_plugin_panel(s);
}
std::vector<std::string> LuaAPI::plugin_panel_lines(const std::string &name)
{
  for (auto &p : plugin_panels)
    if (p.name == name)
    {
      auto i = lua_callbacks.find(p.callback);
      if (i == lua_callbacks.end())
        return {};
      lua_State *L = static_cast<lua_State *>(lua_state);
      lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
      lua_pushstring(L, name.c_str());
      if (lua_pcall(L, 1, 1, 0))
      {
        lua_pop(L, 1);
        return {};
      }
      std::vector<std::string> out;
      if (lua_istable(L, -1))
      {
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
          out.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);
      return out;
    }
  return {};
}
std::vector<std::string> LuaAPI::plugin_picker_items(const std::string &callback)
{
  auto i = lua_callbacks.find(callback);
  if (i == lua_callbacks.end())
    return {};
  lua_State *L = static_cast<lua_State *>(lua_state);
  lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
  lua_pushstring(L, "");
  if (lua_pcall(L, 1, 1, 0))
  {
    lua_pop(L, 1);
    return {};
  }
  std::vector<std::string> out;
  if (lua_istable(L, -1))
  {
    lua_pushnil(L);
    while (lua_next(L, -2))
    {
      out.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  return out;
}

// ---------------------------------------------------------------------------
// Extended native surface (jot.config / jot.git / jot.tasks / jot.symbols /
// jot.debugger / jot.editor / jot.theme). Every function below reads or acts
// on live Editor state directly, so Lua plugins get first-class access to the
// same native capabilities the UI uses — no C++ rebuild needed for features.

static std::string lua_abs_path(const std::string &path)
{
  std::error_code ec;
  fs::path p = path;
  if (!p.is_absolute())
  {
    p = fs::absolute(p, ec);
  }
  return p.lexically_normal().string();
}

static void lua_git_xy_flags(const std::string &xy,
                             bool &staged,
                             bool &unstaged,
                             bool &untracked,
                             bool &deleted,
                             bool &renamed,
                             bool &conflict)
{
  conflict = xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA"
             || xy == "UU" || xy.find('U') != std::string::npos;
  untracked = (xy == "??");
  const char a = xy.size() > 0 ? xy[0] : ' ';
  const char b = xy.size() > 1 ? xy[1] : ' ';
  staged = !conflict && !untracked && a != ' ' && a != '?';
  unstaged = !conflict && !untracked && b != ' ' && b != '?';
  deleted = (a == 'D' || b == 'D');
  renamed = (a == 'R' || b == 'R');
}

static void lua_push_str_field(lua_State *L, const char *k, const std::string &v)
{
  lua_pushstring(L, v.c_str());
  lua_setfield(L, -2, k);
}
static void lua_push_int_field(lua_State *L, const char *k, long long v)
{
  lua_pushinteger(L, v);
  lua_setfield(L, -2, k);
}
static void lua_push_bool_field(lua_State *L, const char *k, bool v)
{
  lua_pushboolean(L, v);
  lua_setfield(L, -2, k);
}

static void lua_push_git_entry(lua_State *L,
                               const std::string &root,
                               const std::string &abs_path,
                               const std::string &xy)
{
  lua_newtable(L);
  lua_push_str_field(L, "path", abs_path);
  std::string rel = abs_path;
  if (abs_path.size() > root.size() && abs_path.compare(0, root.size(), root) == 0
      && abs_path[root.size()] == '/')
  {
    rel = abs_path.substr(root.size() + 1);
  }
  else if (abs_path == root)
  {
    rel = "";
  }
  lua_push_str_field(L, "rel", rel);
  lua_push_str_field(L, "code", xy);
  bool staged = false, unstaged = false, untracked = false;
  bool deleted = false, renamed = false, conflict = false;
  lua_git_xy_flags(xy, staged, unstaged, untracked, deleted, renamed, conflict);
  lua_push_bool_field(L, "staged", staged);
  lua_push_bool_field(L, "unstaged", unstaged);
  lua_push_bool_field(L, "untracked", untracked);
  lua_push_bool_field(L, "deleted", deleted);
  lua_push_bool_field(L, "renamed", renamed);
  lua_push_bool_field(L, "conflict", conflict);
}

void LuaAPI::lua_config_get(lua_State *L, int kind)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const std::string key = luaL_optstring(L, 1, "");
  const bool has_default = lua_gettop(L) >= 2 && !lua_isnil(L, 2);
  if (!has_default && !editor->config.has(key))
  {
    lua_pushnil(L);
    return;
  }
  switch (kind)
  {
  case 1:
  { // number
    const double def = has_default ? lua_tonumber(L, 2) : 0.0;
    lua_pushnumber(L, editor->config.get_double(key, def));
    return;
  }
  case 2:
  { // boolean
    const bool def = has_default ? lua_toboolean(L, 2) : false;
    lua_pushboolean(L, editor->config.get_bool(key, def));
    return;
  }
  default:
  { // string
    const std::string def = has_default ? (lua_tostring(L, 2) ? lua_tostring(L, 2) : "") : "";
    lua_pushstring(L, editor->config.get(key, def).c_str());
    return;
  }
  }
}

void LuaAPI::config_set_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const char *key = luaL_checkstring(L, 1);
  std::string value;
  if (lua_isboolean(L, 2))
  {
    value = lua_toboolean(L, 2) ? "true" : "false";
  }
  else if (lua_isnumber(L, 2))
  {
    const double n = lua_tonumber(L, 2);
    if (n == (double)(long long)n)
    {
      value = std::to_string((long long)n);
    }
    else
    {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%g", n);
      value = buf;
    }
  }
  else
  {
    value = luaL_checkstring(L, 2);
  }
  editor->config.set(key, value);
  // Live config: every setting is re-applied to editor state immediately, so
  // config.set() takes effect without a restart (and config.lua / init.lua
  // are full config sources).
  editor->apply_config_live();
  editor->config.save();
  if (has_event_subscribers("config.changed"))
  {
    const std::string k = key;
    const std::string v = value;
    emit_event_bus("config.changed",
                   [&](lua_State *L)
                   {
                     lua_push_str_field(L, "key", k);
                     lua_push_str_field(L, "value", v);
                   });
  }
}

void LuaAPI::config_unset_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const std::string key = luaL_checkstring(L, 1);
  editor->config.unset(key);
  editor->apply_config_live();
  editor->config.save();
  if (has_event_subscribers("config.changed"))
  {
    const std::string k = key;
    emit_event_bus("config.changed",
                   [&](lua_State *L)
                   {
                     lua_push_str_field(L, "key", k);
                     lua_push_bool_field(L, "removed", true);
                   });
  }
}

void LuaAPI::config_has_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->config.has(luaL_optstring(L, 1, "")));
}

void LuaAPI::push_config_keys(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &k : editor->config.keys())
  {
    lua_pushstring(L, k.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_config_path(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->config.path().c_str());
}

void LuaAPI::push_theme_current(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->config.get("color_scheme", "dark").c_str());
}

void LuaAPI::push_editor_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_str_field(L, "theme", editor->config.get("color_scheme", "dark"));
  lua_push_str_field(L, "path", current_file());
  lua_push_int_field(L, "buffers", (long long)editor->buffers.size());
  int line = 1, column = 1, line_count = 0;
  std::string word;
  bool modified = false;
  bool has_buffer =
      editor->current_buffer >= 0 && editor->current_buffer < (int)editor->buffers.size();
  if (has_buffer)
  {
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    modified = buf.modified;
    line = buf.cursor.y + 1;
    column = buf.cursor.x + 1;
    line_count = (int)buf.line_count();
    lua_push_int_field(L, "buffer", (long long)editor->current_buffer + 1);
    // Plain identifier under the cursor (word chars: alnum / underscore).
    if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count())
    {
      const std::string &text = buf.line(buf.cursor.y);
      const int x = std::clamp(buf.cursor.x, 0, (int)text.size());
      auto is_word_char = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
      int start = x;
      while (start > 0 && is_word_char(text[(size_t)start - 1]))
        start--;
      int end = x;
      while (end < (int)text.size() && is_word_char(text[(size_t)end]))
        end++;
      if (start < end)
      {
        word = text.substr((size_t)start, (size_t)(end - start));
      }
    }
  }
  lua_push_int_field(L, "line", line);
  lua_push_int_field(L, "column", column);
  lua_push_int_field(L, "line_count", line_count);
  lua_push_str_field(L, "word", word);
  lua_push_bool_field(L, "modified", modified);
}

void LuaAPI::push_git_info(lua_State *L)
{
  if (!editor || editor->git_root.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  lua_push_str_field(L, "root", editor->git_root);
  lua_push_str_field(L, "branch", editor->git_branch);
  lua_push_int_field(L, "dirty", editor->git_dirty_count);
  lua_push_int_field(L, "staged", editor->git_staged_count);
  lua_push_int_field(L, "unstaged", editor->git_unstaged_count);
  lua_push_int_field(L, "untracked", editor->git_untracked_count);
  lua_push_int_field(L, "deleted", editor->git_deleted_count);
  lua_push_int_field(L, "renamed", editor->git_renamed_count);
  lua_push_int_field(L, "conflict", editor->git_conflict_count);
  lua_push_int_field(L, "files", (long long)editor->git_file_status.size());
}

void LuaAPI::push_git_status(lua_State *L)
{
  if (!editor || editor->git_root.empty())
  {
    lua_pushnil(L);
    return;
  }
  // Single entry lookup when a path argument is given.
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isstring(L, 1))
  {
    const std::string key = lua_abs_path(lua_tostring(L, 1));
    auto it = editor->git_file_status.find(key);
    if (it == editor->git_file_status.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_push_git_entry(L, editor->git_root, it->first, it->second);
    return;
  }
  lua_newtable(L);
  std::vector<std::pair<std::string, std::string>> entries(editor->git_file_status.begin(),
                                                           editor->git_file_status.end());
  std::sort(entries.begin(),
            entries.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  int n = 1;
  for (const auto &entry : entries)
  {
    lua_push_git_entry(L, editor->git_root, entry.first, entry.second);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::git_stage_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_stage_path(luaL_optstring(L, 1, "")));
}
void LuaAPI::git_unstage_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_unstage_path(luaL_optstring(L, 1, "")));
}
void LuaAPI::git_stage_all_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_stage_all());
}
void LuaAPI::git_unstage_all_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_unstage_all());
}
void LuaAPI::git_commit_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->git_commit_message(luaL_optstring(L, 1, "")));
}
void LuaAPI::git_refresh_from_lua(lua_State *L)
{
  if (editor)
    editor->refresh_git_status(true);
}

void LuaAPI::push_task_list(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  editor->load_terminal_tasks();
  int n = 1;
  for (const auto &task : editor->terminal_tasks)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", task.name);
    lua_push_str_field(L, "command", task.command);
    lua_push_str_field(L, "cwd", task.cwd);
    lua_push_str_field(L, "source", task.source_kind);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::run_task_from_lua(lua_State *L)
{
  const char *name = luaL_optstring(L, 1, "");
  const bool force_new = lua_toboolean(L, 2);
  lua_pushboolean(L, editor && editor->run_terminal_task(name, force_new));
}

void LuaAPI::rerun_task_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->rerun_last_terminal_task());
}

void LuaAPI::push_symbols(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy() || buf.filepath.empty() || buf.line_count() == 0)
    return;
  const std::vector<SymbolMatch> symbols =
      SymbolIndex::extract_document_symbols(buf.lines, buf.filepath);
  int n = 1;
  for (const auto &s : symbols)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", s.name);
    lua_push_str_field(L, "kind", s.kind);
    lua_push_str_field(L, "detail", s.detail);
    lua_push_int_field(L, "line", (long long)s.line + 1);
    lua_push_int_field(L, "column", (long long)s.column + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_debugger_configs(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  editor->load_debugger_configs();
  int n = 1;
  for (const auto &cfg : editor->debugger_configs)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", cfg.name);
    lua_push_str_field(L, "program", cfg.program);
    lua_push_str_field(L, "adapter", cfg.adapter);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::run_debugger_config_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->run_debugger_config(luaL_checkstring(L, 1)));
}

void LuaAPI::push_buffer_current(lua_State *L)
{
  if (!editor || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushinteger(L, editor->current_buffer + 1);
}

void LuaAPI::push_buffer_count(lua_State *L)
{
  lua_pushinteger(L, editor ? (long long)editor->buffers.size() : 0);
}

void LuaAPI::push_buffer_text(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  std::string text;
  for (size_t i = 0; i < buf.lines.size(); i++)
  {
    if (i)
      text += '\n';
    text += buf.lines[i];
  }
  lua_pushlstring(L, text.data(), text.size());
}

void LuaAPI::push_buffer_meta(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_str_field(L, "path", buf.filepath);
  std::string name;
  if (!buf.filepath.empty())
  {
    name = fs::path(buf.filepath).filename().string();
  }
  lua_push_str_field(L, "name", name);
  lua_push_bool_field(L, "modified", buf.modified);
  lua_push_int_field(L, "line_count", (long long)buf.line_count());
}

void LuaAPI::push_buffer_selection(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_bool_field(L, "active", buf.selection.active);
  lua_push_int_field(L, "start_line", (long long)buf.selection.start.y + 1);
  lua_push_int_field(L, "start_col", (long long)buf.selection.start.x + 1);
  lua_push_int_field(L, "end_line", (long long)buf.selection.end.y + 1);
  lua_push_int_field(L, "end_col", (long long)buf.selection.end.x + 1);
}

void LuaAPI::push_buffer_bookmarks(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const int line : buf.bookmarks)
  {
    lua_pushinteger(L, line + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_buffer_folds(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const FoldRange &r : buf.fold_ranges)
  {
    lua_newtable(L);
    lua_push_int_field(L, "start", (long long)r.start_line + 1);
    lua_push_int_field(L, "end", (long long)r.end_line + 1);
    lua_push_bool_field(L, "collapsed", r.collapsed);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::clipboard_copy_from_lua(lua_State *L)
{
  if (editor)
    editor->copy();
}
void LuaAPI::clipboard_cut_from_lua(lua_State *L)
{
  if (editor)
    editor->cut();
}
void LuaAPI::clipboard_paste_from_lua(lua_State *L)
{
  if (editor)
    editor->paste();
}

void LuaAPI::push_terminal_list(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (size_t i = 0; i < editor->integrated_terminals.size(); i++)
  {
    IntegratedTerminal *term = editor->integrated_terminals[i].get();
    if (!term)
      continue;
    lua_newtable(L);
    lua_push_int_field(L, "index", (long long)i + 1);
    lua_push_str_field(L, "label", term->get_label());
    lua_push_bool_field(L, "active", term->is_active());
    lua_push_bool_field(L, "current", (int)i == editor->current_integrated_terminal);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::terminal_write_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string text = luaL_checkstring(L, 1);
  int resolved = -1;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
  {
    resolved = (int)lua_tointeger(L, 2) - 1;
  }
  IntegratedTerminal *term = editor->get_integrated_terminal(resolved);
  lua_pushboolean(L, term ? term->send_text(text) : false);
}

void LuaAPI::terminal_close_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int resolved = editor->current_integrated_terminal;
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isnumber(L, 1))
  {
    resolved = (int)lua_tointeger(L, 1) - 1;
  }
  if (resolved < 0 || resolved >= (int)editor->integrated_terminals.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->close_integrated_terminal(resolved);
  lua_pushboolean(L, 1);
}

void LuaAPI::terminal_activate_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int resolved = editor->current_integrated_terminal;
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isnumber(L, 1))
  {
    resolved = (int)lua_tointeger(L, 1) - 1;
  }
  if (resolved < 0 || resolved >= (int)editor->integrated_terminals.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->activate_integrated_terminal(resolved, true);
  lua_pushboolean(L, 1);
}

void LuaAPI::terminal_spawn_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushinteger(L, 0);
    return;
  }
  const std::string label = luaL_optstring(L, 1, "");
  const std::string cwd = luaL_optstring(L, 2, "");
  const size_t before = editor->integrated_terminals.size();
  editor->create_integrated_terminal(label, cwd);
  if (editor->integrated_terminals.size() == before)
  {
    lua_pushinteger(L, 0); // shell failed to open
    return;
  }
  lua_pushinteger(L, editor->current_integrated_terminal + 1);
}

void LuaAPI::push_workspace_path(lua_State *L)
{
  if (!editor || editor->root_dir.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->root_dir.c_str());
}

void LuaAPI::push_recent_files(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_files)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_recent_workspaces(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_workspaces)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_lsp_clients(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    lua_push_bool_field(L, "running", client->is_running());
    lua_push_bool_field(L, "initialized", client->is_initialized());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::popup_from_lua(lua_State *L)
{
  if (!editor)
    return;
  editor->show_popup(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
}

// Canonical token-type names, indexed by the shared TreeSitterTokenKind ids
// used by both the regex highlighter and the tree-sitter query engine.
static const char *const kLuaTokenNames[] = {"none",                   // 0
                                             "keyword",                // 1
                                             "string",                 // 2
                                             "comment",                // 3
                                             "number",                 // 4
                                             "type",                   // 5
                                             "function",               // 6
                                             "variable",               // 7
                                             "constant",               // 8
                                             "builtin",                // 9
                                             "operator",               // 10
                                             "punctuation",            // 11
                                             "tag",                    // 12
                                             "attribute",              // 13
                                             "namespace",              // 14
                                             "module",                 // 15
                                             "parameter",              // 16
                                             "field",                  // 17
                                             "keyword.control",        // 18
                                             "keyword.storage",        // 19
                                             "keyword.preproc",        // 20
                                             "function.method",        // 21
                                             "function.constructor",   // 22
                                             "type.builtin",           // 23
                                             "constant.macro",         // 24
                                             "string.escape",          // 25
                                             "punctuation.bracket",    // 26
                                             "punctuation.delimiter"}; // 27

// Resolve a token id to the active theme color, mirroring the renderer's own
// mapping so Lua callers see the exact highlight color on screen.
static int lua_syntax_fg(const Theme &theme, int token)
{
  switch (token)
  {
  case 1:
    return theme.fg_keyword;
  case 2:
    return theme.fg_string;
  case 3:
    return theme.fg_comment;
  case 4:
    return theme.fg_number;
  case 5:
    return theme.fg_type;
  case 6:
    return theme.fg_function;
  case 7:
    return theme.fg_variable;
  case 8:
    return theme.fg_constant;
  case 9:
    return theme.fg_builtin;
  case 10:
    return theme.fg_operator;
  case 11:
    return theme.fg_punctuation;
  case 12:
    return theme.fg_tag;
  case 13:
    return theme.fg_attribute;
  case 14:
    return theme.fg_namespace;
  case 15:
    return theme.fg_module;
  case 16:
    return theme.fg_parameter;
  case 17:
    return theme.fg_field;
  case 18:
    return theme.fg_keyword_control;
  case 19:
    return theme.fg_keyword_storage;
  case 20:
    return theme.fg_keyword_preproc;
  case 21:
    return theme.fg_function_method;
  case 22:
    return theme.fg_function_constructor;
  case 23:
    return theme.fg_type_builtin;
  case 24:
    return theme.fg_constant_macro;
  case 25:
    return theme.fg_string_escape;
  case 26:
    return theme.fg_punctuation_bracket;
  case 27:
    return theme.fg_punctuation_delimiter;
  default:
    return theme.fg_command;
  }
}

void LuaAPI::push_search_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_bool_field(L, "visible", editor->show_search);
  lua_push_str_field(L, "query", editor->search_query);
  lua_push_bool_field(L, "case_sensitive", editor->search_case_sensitive);
  lua_push_bool_field(L, "whole_word", editor->search_whole_word);
  lua_push_bool_field(L, "regex", editor->search_regex);
  lua_push_bool_field(L, "replace", editor->search_replace_visible);
  lua_push_str_field(L, "replace_text", editor->search_replace_text);
  lua_push_bool_field(L, "scoped", editor->search_scoped_to_selection);
  lua_push_int_field(L, "result_count", (long long)editor->search_results.size());
  lua_push_int_field(L,
                     "result_index",
                     editor->search_result_index >= 0 ? (long long)editor->search_result_index + 1
                                                      : 0);
}

void LuaAPI::push_search_matches(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const SearchMatch &m : editor->search_results)
  {
    lua_newtable(L);
    lua_push_int_field(L, "line", (long long)m.line + 1);
    lua_push_int_field(L, "column", (long long)m.col + 1);
    lua_push_int_field(L, "len", m.len);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_picker_active(lua_State *L)
{
  lua_pushboolean(L, editor && editor->show_quick_pick);
}

void LuaAPI::push_picker_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  lua_push_str_field(L, "title", editor->quick_pick_title);
  lua_push_str_field(L, "query", editor->quick_pick_query);
  lua_push_int_field(L, "selected", (long long)editor->quick_pick_selected + 1);
  lua_push_int_field(L, "visible", (long long)editor->quick_pick_items.size());
  lua_push_int_field(L, "total", (long long)editor->quick_pick_all_items.size());
}

void LuaAPI::push_picker_items(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  int n = 1;
  for (const QuickPickItem &item : editor->quick_pick_items)
  {
    lua_newtable(L);
    lua_push_str_field(L, "label", item.label);
    lua_push_str_field(L, "detail", item.detail);
    lua_push_str_field(L, "preview", item.preview);
    lua_push_str_field(L, "filepath", item.filepath);
    lua_push_int_field(L, "line", item.line >= 0 ? (long long)item.line + 1 : 0);
    lua_push_int_field(L, "column", item.col >= 0 ? (long long)item.col + 1 : 0);
    lua_push_int_field(L, "severity", item.severity);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::picker_accept_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->accept_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::picker_close_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->close_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::push_buffer_tokens(lua_State *L)
{
  if (!editor)
  {
    lua_newtable(L);
    return;
  }
  // Read args BEFORE pushing the result table: lua_newtable shifts the stack,
  // so the optional buffer-id argument would otherwise be masked by the table
  // itself (never nil), making resolve_buffer_arg fail and this always return
  // an empty token list.
  const int line_no = (int)luaL_checkinteger(L, 1) - 1;
  int idx = editor->current_buffer;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
  {
    idx = resolve_buffer_arg(L, 2);
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
  {
    lua_newtable(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line_no < 0 || line_no >= (int)buf.line_count())
  {
    lua_newtable(L);
    return;
  }
  lua_newtable(L);
  const auto &colors = editor->get_line_syntax_colors(buf, line_no);
  const std::string &text = buf.line(line_no);
  const size_t n_colors = colors.size();
  int n = 1;
  size_t i = 0;
  while (i < n_colors)
  {
    const int token = colors[i].first == 1 ? colors[i].second : 0;
    size_t j = i + 1;
    while (j < n_colors)
    {
      const int next = colors[j].first == 1 ? colors[j].second : 0;
      if (next != token)
        break;
      j++;
    }
    if (token > 0 && token < 28)
    {
      const size_t span_end = std::min(j, (size_t)std::max(0, (int)text.size()));
      if (i < span_end)
      {
        lua_newtable(L);
        lua_push_int_field(L, "start", (long long)i);
        lua_push_int_field(L, "end", (long long)span_end);
        lua_push_int_field(L, "token", token);
        lua_push_str_field(L, "name", kLuaTokenNames[token]);
        lua_push_str_field(L, "text", text.substr(i, span_end - i));
        lua_push_int_field(L, "fg", lua_syntax_fg(editor->theme, token));
        lua_rawseti(L, -2, n++);
      }
    }
    i = j;
  }
}

// Runs a one-shot Lua callback (registered under `id` in `callbacks`) with a
// single table argument built by `build`. Returns true when the callback
// existed and ran.
template <typename BuildFn>
static bool lua_deliver_one_shot(const std::string &id,
                                 std::unordered_map<std::string, int> &callbacks,
                                 void *lua_state,
                                 BuildFn build)
{
  if (id.empty() || !lua_state)
    return false;
  auto it = callbacks.find(id);
  if (it == callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  const int ref = it->second;
  callbacks.erase(it);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  build(L);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua one-shot callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return true;
}

static void lua_push_diag_row(lua_State *L, const Diagnostic &d)
{
  lua_newtable(L);
  lua_push_int_field(L, "line", (long long)d.line + 1);
  lua_push_int_field(L, "column", (long long)d.col + 1);
  lua_push_int_field(L, "end_line", (long long)d.end_line + 1);
  lua_push_int_field(L, "end_col", (long long)d.end_col + 1);
  lua_push_str_field(L, "message", d.message);
  lua_push_int_field(L, "severity", d.severity);
  lua_push_str_field(L, "severity_name", lua_diag_severity_name(d.severity));
}

static void lua_push_location(lua_State *L, const LSPLocation &loc)
{
  lua_newtable(L);
  lua_push_str_field(L, "path", loc.filepath);
  lua_push_int_field(L, "line", (long long)loc.line + 1);
  lua_push_int_field(L, "column", (long long)loc.character + 1);
  lua_push_int_field(L, "end_line", (long long)loc.end_line + 1);
  lua_push_int_field(L, "end_column", (long long)loc.end_character + 1);
}

static void lua_push_symbol_row(lua_State *L, const LSPSymbol &s)
{
  lua_newtable(L);
  lua_push_str_field(L, "name", s.name);
  lua_push_str_field(L, "kind", s.kind);
  lua_push_str_field(L, "detail", s.detail);
  lua_push_str_field(L, "path", s.filepath);
  lua_push_int_field(L, "line", (long long)s.line + 1);
  lua_push_int_field(L, "column", (long long)s.character + 1);
  lua_push_int_field(L, "end_line", (long long)s.end_line + 1);
  lua_push_int_field(L, "end_column", (long long)s.end_character + 1);
}

bool LuaAPI::try_deliver_lsp_hover(const LSPHoverResult &hover)
{
  if (pending_lsp_hover.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_hover.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != hover.origin_filepath || buf.cursor.y != hover.origin_line
      || buf.cursor.x != hover.origin_character)
  {
    return false;
  }
  const std::string id = pending_lsp_hover;
  pending_lsp_hover.clear();
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "contents", hover.contents);
                                lua_push_str_field(L, "path", hover.origin_filepath);
                                lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
                                lua_push_int_field(
                                    L, "column", (long long)hover.origin_character + 1);
                              });
}

bool LuaAPI::try_deliver_lsp_definition(const LSPDefinitionResult &definition)
{
  if (pending_lsp_definition.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_definition.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != definition.origin_filepath || buf.cursor.y != definition.origin_line
      || buf.cursor.x != definition.origin_character)
  {
    return false;
  }
  const std::string id = pending_lsp_definition;
  pending_lsp_definition.clear();
  return lua_deliver_one_shot(
      id,
      lua_callbacks,
      lua_state,
      [&](lua_State *L)
      {
        lua_newtable(L);
        lua_push_str_field(L, "path", definition.origin_filepath);
        lua_push_int_field(L, "line", (long long)definition.origin_line + 1);
        lua_push_int_field(L, "column", (long long)definition.origin_character + 1);
        lua_newtable(L);
        int n = 1;
        for (const LSPLocation &loc : definition.locations)
        {
          lua_push_location(L, loc);
          lua_rawseti(L, -2, n++);
        }
        lua_setfield(L, -2, "locations");
      });
}

bool LuaAPI::try_deliver_lsp_symbols(const LSPDocumentSymbolResult &symbols)
{
  if (pending_lsp_symbols.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_symbols.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != symbols.filepath)
    return false;
  const std::string id = pending_lsp_symbols;
  pending_lsp_symbols.clear();
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "path", symbols.filepath);
                                lua_newtable(L);
                                int n = 1;
                                for (const LSPSymbol &s : symbols.symbols)
                                {
                                  lua_push_symbol_row(L, s);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "symbols");
                              });
}

void LuaAPI::lsp_request_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "lsp." + std::to_string(kind) + "." + std::to_string(ref);
  lua_callbacks[id] = ref;
  switch (kind)
  {
  case 0:
    pending_lsp_hover = id;
    editor->request_lsp_hover();
    break;
  case 1:
    pending_lsp_definition = id;
    editor->request_lsp_definition();
    break;
  case 2:
    pending_lsp_symbols = id;
    editor->request_document_symbols();
    break;
  default:
    pending_lsp_completion = id;
    editor->request_lsp_completion(true);
    break;
  }
}

static const char *lua_completion_kind_name(int kind)
{
  switch (kind)
  {
  case 1:
    return "Text";
  case 2:
    return "Method";
  case 3:
    return "Function";
  case 4:
    return "Constructor";
  case 5:
    return "Field";
  case 6:
    return "Variable";
  case 7:
    return "Class";
  case 8:
    return "Interface";
  case 9:
    return "Module";
  case 10:
    return "Property";
  case 11:
    return "Unit";
  case 12:
    return "Value";
  case 13:
    return "Enum";
  case 14:
    return "Keyword";
  case 15:
    return "Snippet";
  case 16:
    return "Color";
  case 17:
    return "File";
  case 18:
    return "Reference";
  case 19:
    return "Folder";
  case 20:
    return "EnumMember";
  case 21:
    return "Constant";
  case 22:
    return "Struct";
  case 23:
    return "Event";
  case 24:
    return "Operator";
  case 25:
    return "TypeParameter";
  default:
    return "Unknown";
  }
}

static void lua_push_completion_row(lua_State *L, const LSPCompletionItem &item)
{
  lua_newtable(L);
  lua_push_str_field(L, "label", item.label);
  lua_push_str_field(L, "insert_text", item.insert_text);
  lua_push_str_field(L, "detail", item.detail);
  lua_push_str_field(L, "documentation", item.documentation);
  lua_push_str_field(L, "filter_text", item.filter_text);
  lua_push_str_field(L, "sort_text", item.sort_text);
  lua_push_int_field(L, "kind", item.kind);
  lua_push_str_field(L, "kind_name", lua_completion_kind_name(item.kind));
  lua_push_int_field(L, "insert_text_format", item.insert_text_format);
  lua_push_bool_field(L, "deprecated", item.deprecated);
  lua_push_bool_field(L, "preselect", item.preselect);
  lua_newtable(L);
  int cn = 1;
  for (const std::string &c : item.commit_characters)
  {
    lua_pushstring(L, c.c_str());
    lua_rawseti(L, -2, cn++);
  }
  lua_setfield(L, -2, "commit_characters");
  lua_push_bool_field(L, "has_edit_range", item.has_text_edit_range);
  lua_push_int_field(L, "edit_start_line", (long long)item.edit_start_line + 1);
  lua_push_int_field(L, "edit_start_char", (long long)item.edit_start_char + 1);
  lua_push_int_field(L, "edit_end_line", (long long)item.edit_end_line + 1);
  lua_push_int_field(L, "edit_end_char", (long long)item.edit_end_char + 1);
}

bool LuaAPI::try_deliver_lsp_completion(const std::string &filepath,
                                        const std::vector<LSPCompletionItem> &items)
{
  if (pending_lsp_completion.empty())
    return false;
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    pending_lsp_completion.clear();
    return false;
  }
  const FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (buf.filepath != filepath)
    return false;
  const std::string id = pending_lsp_completion;
  pending_lsp_completion.clear();
  const Cursor anchor = editor->lsp_completion_anchor;
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_str_field(L, "path", filepath);
                                lua_push_int_field(L, "anchor_line", (long long)anchor.y + 1);
                                lua_push_int_field(L, "anchor_col", (long long)anchor.x + 1);
                                lua_newtable(L);
                                int n = 1;
                                for (const LSPCompletionItem &item : items)
                                {
                                  lua_push_completion_row(L, item);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "items");
                              });
}

void LuaAPI::push_lsp_completions(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_bool_field(L, "visible", editor->lsp_completion_visible);
  lua_push_str_field(L, "path", editor->lsp_completion_filepath);
  lua_push_str_field(L, "prefix", editor->lsp_completion_prefix);
  lua_push_int_field(L, "anchor_line", (long long)editor->lsp_completion_anchor.y + 1);
  lua_push_int_field(L, "anchor_col", (long long)editor->lsp_completion_anchor.x + 1);
  lua_push_int_field(L,
                     "selected",
                     editor->lsp_completion_visible ? (long long)editor->lsp_completion_selected + 1
                                                    : 0);
  lua_push_int_field(L, "total", (long long)editor->lsp_completion_all_items.size());
  lua_newtable(L);
  int n = 1;
  for (const LSPCompletionItem &item : editor->lsp_completion_items)
  {
    lua_push_completion_row(L, item);
    lua_rawseti(L, -2, n++);
  }
  lua_setfield(L, -2, "items");
}

// ---------------------------------------------------------------------------
// Ambient event bus (jot.events) and viewport mirror (jot.viewport).

void LuaAPI::events_subscribe_from_lua(lua_State *L)
{
  const std::string name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  EventBusSubscriber sub;
  sub.ref = ref;
  sub.key = "ev." + name + "." + std::to_string(next_event_sub_id_++);
  event_subscribers_[name].push_back(std::move(sub));
  lua_pushstring(L, event_subscribers_[name].back().key.c_str());
}

void LuaAPI::events_unsubscribe_from_lua(lua_State *L)
{
  const std::string name = luaL_checkstring(L, 1);
  const std::string key = luaL_checkstring(L, 2);
  auto it = event_subscribers_.find(name);
  if (it == event_subscribers_.end())
    return;
  auto &vec = it->second;
  for (auto i = vec.begin(); i != vec.end(); ++i)
  {
    if (i->key == key)
    {
      if (lua_state)
      {
        luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, i->ref);
      }
      vec.erase(i);
      break;
    }
  }
}

void LuaAPI::emit_event_bus(const std::string &name, const std::function<void(lua_State *)> &build)
{
  if (!lua_initialized || !editor || !is_main_thread())
    return;
  auto it = event_subscribers_.find(name);
  if (it == event_subscribers_.end() || it->second.empty())
    return;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  // Copy so a subscriber can unsubscribe itself mid-dispatch.
  const std::vector<EventBusSubscriber> subs = it->second;
  for (const auto &sub : subs)
  {
    const int frame_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, sub.ref);
    lua_newtable(L);
    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "event");
    build(L);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
      std::cerr << "Lua event bus error (" << name << "): " << lua_tostring(L, -1) << "\n";
    }
    lua_settop(L, frame_top);
  }
  lua_settop(L, top);
}

void LuaAPI::emit_lsp_hover(const LSPHoverResult &hover)
{
  if (!has_event_subscribers("lsp.hover"))
    return;
  emit_event_bus("lsp.hover",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "contents", hover.contents);
                   lua_push_str_field(L, "path", hover.origin_filepath);
                   lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
                   lua_push_int_field(L, "column", (long long)hover.origin_character + 1);
                 });
}

void LuaAPI::emit_lsp_definition(const LSPDefinitionResult &definition)
{
  if (!has_event_subscribers("lsp.definition"))
    return;
  emit_event_bus("lsp.definition",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", definition.origin_filepath);
                   lua_push_int_field(L, "line", (long long)definition.origin_line + 1);
                   lua_push_int_field(L, "column", (long long)definition.origin_character + 1);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPLocation &loc : definition.locations)
                   {
                     lua_push_location(L, loc);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "locations");
                 });
}

void LuaAPI::emit_lsp_symbols(const LSPDocumentSymbolResult &symbols)
{
  if (!has_event_subscribers("lsp.symbols"))
    return;
  emit_event_bus("lsp.symbols",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", symbols.filepath);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPSymbol &s : symbols.symbols)
                   {
                     lua_push_symbol_row(L, s);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "symbols");
                 });
}

void LuaAPI::emit_lsp_completion(const std::string &filepath,
                                 const std::vector<LSPCompletionItem> &items)
{
  if (!has_event_subscribers("lsp.completion"))
    return;
  const Cursor anchor = editor ? editor->lsp_completion_anchor : Cursor{0, 0};
  emit_event_bus("lsp.completion",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", filepath);
                   lua_push_int_field(L, "anchor_line", (long long)anchor.y + 1);
                   lua_push_int_field(L, "anchor_col", (long long)anchor.x + 1);
                   lua_newtable(L);
                   int n = 1;
                   for (const LSPCompletionItem &item : items)
                   {
                     lua_push_completion_row(L, item);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "items");
                 });
}

void LuaAPI::emit_git_refreshed()
{
  if (!editor || !has_event_subscribers("git.refreshed"))
    return;
  emit_event_bus("git.refreshed",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "root", editor->git_root);
                   lua_push_str_field(L, "branch", editor->git_branch);
                   lua_push_int_field(L, "dirty", editor->git_dirty_count);
                   lua_push_int_field(L, "staged", editor->git_staged_count);
                   lua_push_int_field(L, "unstaged", editor->git_unstaged_count);
                   lua_push_int_field(L, "untracked", editor->git_untracked_count);
                   lua_push_int_field(L, "deleted", editor->git_deleted_count);
                   lua_push_int_field(L, "renamed", editor->git_renamed_count);
                   lua_push_int_field(L, "conflict", editor->git_conflict_count);
                   lua_push_int_field(L, "files", (long long)editor->git_file_status.size());
                 });
}

void LuaAPI::push_viewport_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int win_w = editor->ui ? editor->ui->get_render_width() : 0;
  const int win_h = editor->ui ? editor->ui->get_height() : 0;
  lua_newtable(L);
  lua_push_int_field(L, "width", win_w);
  lua_push_int_field(L, "height", win_h);
  lua_push_int_field(L, "status_height", editor->status_height);
  lua_push_int_field(L, "tab_height", editor->tab_height);
  lua_setfield(L, -2, "window");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_sidebar);
  lua_push_int_field(L, "width", editor->sidebar_width);
  lua_setfield(L, -2, "sidebar");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_minimap);
  lua_push_int_field(L, "width", editor->minimap_width);
  lua_setfield(L, -2, "minimap");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_integrated_terminal);
  lua_push_int_field(L, "height", editor->integrated_terminal_height);
  lua_setfield(L, -2, "terminal");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_right_panel);
  lua_push_int_field(L, "width", editor->right_panel_width);
  lua_setfield(L, -2, "right_panel");
  if (!editor->panes.empty())
  {
    const SplitPane &pane = editor->get_pane();
    lua_newtable(L);
    lua_push_int_field(L, "x", pane.x);
    lua_push_int_field(L, "y", pane.y);
    lua_push_int_field(L, "width", pane.w);
    lua_push_int_field(L, "height", pane.h);
    lua_push_int_field(L, "buffer", (long long)pane.buffer_id + 1);
    lua_push_bool_field(L, "active", pane.active);
    lua_setfield(L, -2, "pane");
    if (pane.buffer_id >= 0 && pane.buffer_id < (int)editor->buffers.size())
    {
      FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
      const int rows = std::max(0, pane.h - editor->tab_height - 1);
      const int line_count = (int)buf.line_count();
      const int first = buf.scroll_offset + 1;
      const int last = std::min(line_count, buf.scroll_offset + rows);
      lua_newtable(L);
      lua_push_int_field(L, "line_count", line_count);
      lua_push_int_field(L, "first_line", first);
      lua_push_int_field(L, "last_line", last);
      lua_push_int_field(L, "visible_lines", rows);
      lua_push_int_field(L, "cursor_line", (long long)buf.cursor.y + 1);
      lua_push_int_field(L, "cursor_col", (long long)buf.cursor.x + 1);
      lua_push_int_field(L, "scroll_x", buf.scroll_x);
      lua_setfield(L, -2, "buffer");
    }
  }
}

void LuaAPI::push_viewport_line_at(lua_State *L)
{
  if (!editor || editor->panes.empty())
  {
    lua_pushnil(L);
    return;
  }
  const int sy = (int)luaL_checkinteger(L, 1) - 1; // 0-based screen row
  const SplitPane &pane = editor->get_pane();
  const int top = pane.y + editor->tab_height;
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  if (sy < top || sy >= top + rows)
  {
    lua_pushnil(L);
    return;
  }
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int line = buf.scroll_offset + (sy - top);
  if (line < 0 || line >= (int)buf.line_count())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushinteger(L, line + 1);
}

// Recursively converts one native FileNode (and its children) into a Lua
// table — the exact tree the explorer sidebar renders.
static void lua_push_file_node(lua_State *L, const FileNode &node)
{
  lua_newtable(L);
  lua_push_str_field(L, "name", node.name);
  lua_push_str_field(L, "path", node.path);
  lua_push_bool_field(L, "is_dir", node.is_dir);
  lua_push_bool_field(L, "expanded", node.expanded);
  lua_push_int_field(L, "depth", node.depth);
  lua_newtable(L);
  int cn = 1;
  for (const FileNode &child : node.children)
  {
    lua_push_file_node(L, child);
    lua_rawseti(L, -2, cn++);
  }
  lua_setfield(L, -2, "children");
}

void LuaAPI::push_filetree_root(lua_State *L)
{
  if (!editor || editor->root_dir.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->root_dir.c_str());
}

void LuaAPI::push_filetree_tree(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const FileNode &node : editor->file_tree)
  {
    lua_push_file_node(L, node);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_filetree_children(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const std::string wanted = luaL_optstring(L, 1, "");
  if (wanted.empty())
    return;
  const FileNode *found = nullptr;
  std::function<void(const FileNode &)> search = [&](const FileNode &node)
  {
    if (found)
      return;
    if (node.path == wanted)
    {
      found = &node;
      return;
    }
    for (const FileNode &child : node.children)
      search(child);
  };
  for (const FileNode &node : editor->file_tree)
    search(node);
  if (!found)
    return;
  int n = 1;
  for (const FileNode &child : found->children)
  {
    lua_push_file_node(L, child);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::viewport_scroll_top_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  const int line_count = (int)buf.line_count();
  const int target = (int)luaL_checkinteger(L, 1) - 1;
  const int max_top = std::max(0, line_count - rows);
  buf.scroll_offset = std::clamp(target, 0, max_top);
  buf.scroll_offset = std::min(buf.scroll_offset, std::max(0, line_count - 1));
  editor->needs_redraw = true;
}

void LuaAPI::viewport_scroll_lines_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  const int line_count = (int)buf.line_count();
  const int delta = (int)luaL_checkinteger(L, 1);
  const int max_top = std::max(0, line_count - rows);
  buf.scroll_offset = std::clamp(buf.scroll_offset + delta, 0, max_top);
  editor->needs_redraw = true;
}

void LuaAPI::viewport_scroll_col_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  buf.scroll_x = std::max(0, (int)luaL_checkinteger(L, 1) - 1);
  editor->needs_redraw = true;
}

void LuaAPI::viewport_reveal_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  editor->ensure_cursor_visible();
  editor->needs_redraw = true;
}

void LuaAPI::buffer_select_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 5);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  const int line_count = (int)buf.line_count();
  if (line_count == 0)
    return;
  const int sl = (int)luaL_checkinteger(L, 1) - 1;
  const int sc = (int)luaL_checkinteger(L, 2) - 1;
  const int el = (int)luaL_checkinteger(L, 3) - 1;
  const int ec = (int)luaL_checkinteger(L, 4) - 1;
  buf.selection.start.y = std::clamp(sl, 0, line_count - 1);
  buf.selection.start.x = std::clamp(sc, 0, (int)buf.line(buf.selection.start.y).size());
  buf.selection.end.y = std::clamp(el, 0, line_count - 1);
  buf.selection.end.x = std::clamp(ec, 0, (int)buf.line(buf.selection.end.y).size());
  buf.selection.active = true;
  editor->needs_redraw = true;
}

void LuaAPI::buffer_clear_selection_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].selection.active = false;
  editor->needs_redraw = true;
}

void LuaAPI::emit_buffer_event(const std::string &event, const std::string &path)
{
  if (!has_event_subscribers(event))
    return;
  emit_event_bus(event, [&](lua_State *L) { lua_push_str_field(L, "path", path); });
}

void LuaAPI::emit_diagnostics_changed(const std::string &path, const std::vector<Diagnostic> &items)
{
  if (!has_event_subscribers("diagnostics.changed"))
    return;
  emit_event_bus("diagnostics.changed",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "path", path);
                   lua_push_int_field(L, "count", (long long)items.size());
                   lua_newtable(L);
                   int n = 1;
                   for (const Diagnostic &d : items)
                   {
                     lua_push_diag_row(L, d);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "items");
                 });
}

void LuaAPI::emit_theme_switched(const std::string &name)
{
  if (!has_event_subscribers("theme.switched"))
    return;
  emit_event_bus("theme.switched", [&](lua_State *L) { lua_push_str_field(L, "name", name); });
}

void LuaAPI::push_lsp_diagnostics(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    const auto &files = client->last_diagnostics();
    lua_newtable(L);
    int fn = 1;
    for (const auto &entry : files)
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", entry.first);
      lua_newtable(L);
      int dn = 1;
      for (const Diagnostic &d : entry.second)
      {
        lua_push_diag_row(L, d);
        lua_rawseti(L, -2, dn++);
      }
      lua_setfield(L, -2, "diagnostics");
      lua_rawseti(L, -2, fn++);
    }
    lua_setfield(L, -2, "files");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_lsp_last_results(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &client : editor->lsp_clients)
  {
    if (!client)
      continue;
    lua_newtable(L);
    lua_push_str_field(L, "language", client->get_language());
    lua_push_str_field(L, "root", client->get_root_path());
    const LSPHoverResult &hover = client->last_hover();
    if (hover.contents.empty())
    {
      lua_pushnil(L);
    }
    else
    {
      lua_newtable(L);
      lua_push_str_field(L, "contents", hover.contents);
      lua_push_str_field(L, "path", hover.origin_filepath);
      lua_push_int_field(L, "line", (long long)hover.origin_line + 1);
      lua_push_int_field(L, "column", (long long)hover.origin_character + 1);
    }
    lua_setfield(L, -2, "hover");
    lua_newtable(L);
    int dn = 1;
    for (const LSPDefinitionResult &def : client->last_definitions())
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", def.origin_filepath);
      lua_push_int_field(L, "line", (long long)def.origin_line + 1);
      lua_push_int_field(L, "column", (long long)def.origin_character + 1);
      lua_newtable(L);
      int ln = 1;
      for (const LSPLocation &loc : def.locations)
      {
        lua_push_location(L, loc);
        lua_rawseti(L, -2, ln++);
      }
      lua_setfield(L, -2, "locations");
      lua_rawseti(L, -2, dn++);
    }
    lua_setfield(L, -2, "definitions");
    lua_newtable(L);
    int sn = 1;
    for (const LSPDocumentSymbolResult &res : client->last_document_symbols())
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", res.filepath);
      lua_newtable(L);
      int rn = 1;
      for (const LSPSymbol &s : res.symbols)
      {
        lua_push_symbol_row(L, s);
        lua_rawseti(L, -2, rn++);
      }
      lua_setfield(L, -2, "symbols");
      lua_rawseti(L, -2, sn++);
    }
    lua_setfield(L, -2, "symbols");
    lua_rawseti(L, -2, n++);
  }
}

// ---------------------------------------------------------------------------
// Batch 5: jot.timer / jot.debugger live state / jot.theme.palette /
// jot.buffer.lines / jot.clipboard.get
// ---------------------------------------------------------------------------

void LuaAPI::timer_set_from_lua(lua_State *L, bool repeat)
{
  const int ms = (int)luaL_checkinteger(L, 1);
  if (!editor || ms < 0 || !lua_isfunction(L, 2))
  {
    lua_pushinteger(L, 0);
    return;
  }
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string key = "timer." + std::to_string(ref);
  lua_callbacks[key] = ref;
  LuaAPI *self = this;
  uint64_t timer_id = 0;
  try
  {
    timer_id = editor->event_loop_.set_timer(ms, repeat, [self, ref]() { self->fire_timer(ref); });
  }
  catch (...)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    lua_callbacks.erase(key);
    lua_pushinteger(L, 0);
    return;
  }
  timer_entries_[timer_id] = LuaTimerEntry{ref, repeat};
  lua_pushinteger(L, (lua_Integer)timer_id);
}

void LuaAPI::timer_clear_from_lua(lua_State *L)
{
  const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
  auto it = timer_entries_.find(id);
  if (it == timer_entries_.end())
    return;
  if (editor)
    editor->event_loop_.cancel_timer(id);
  if (lua_state && it->second.ref >= 0)
  {
    const std::string key = "timer." + std::to_string(it->second.ref);
    auto cb = lua_callbacks.find(key);
    if (cb != lua_callbacks.end())
    {
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, cb->second);
      lua_callbacks.erase(cb);
    }
  }
  timer_entries_.erase(it);
}

void LuaAPI::cancel_all_timers()
{
  if (editor)
  {
    for (const auto &kv : timer_entries_)
    {
      editor->event_loop_.cancel_timer(kv.first);
    }
  }
  timer_entries_.clear();
}

void LuaAPI::fire_timer(int ref)
{
  if (!lua_initialized || !lua_state)
    return;
  lua_State *L = static_cast<lua_State *>(lua_state);
  uint64_t timer_id = 0;
  bool repeat = false;
  for (const auto &kv : timer_entries_)
  {
    if (kv.second.ref == ref)
    {
      timer_id = kv.first;
      repeat = kv.second.repeat;
      break;
    }
  }
  if (timer_id == 0)
    return;
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (lua_pcall(L, 0, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua timer callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  if (!repeat)
  {
    // Re-check: the callback may have cleared this timer itself.
    auto it = timer_entries_.find(timer_id);
    if (it == timer_entries_.end())
      return;
    if (editor)
      editor->event_loop_.cancel_timer(timer_id);
    const std::string key = "timer." + std::to_string(ref);
    auto cb = lua_callbacks.find(key);
    if (cb != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, cb->second);
      lua_callbacks.erase(cb);
    }
    timer_entries_.erase(it);
  }
}

void LuaAPI::push_debugger_state(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  const size_t sessions =
      std::min(editor->debugger_sessions.size(), editor->debugger_session_state.size());
  for (size_t i = 0; i < sessions; i++)
  {
    const DebuggerSessionState &st = editor->debugger_session_state[i];
    lua_newtable(L);
    lua_push_str_field(L, "name", st.name);
    lua_push_str_field(L, "adapter", st.adapter);
    lua_push_str_field(L, "program", st.program);
    lua_push_bool_field(L, "running", st.running);
    lua_push_bool_field(L, "stopped", st.stopped);
    lua_push_int_field(L, "active_thread_id", st.active_thread_id);
    lua_push_int_field(L, "active_frame_id", st.active_frame_id);
    lua_push_str_field(L, "output", st.output);
    lua_push_str_field(L, "last_error", st.last_error);
    lua_newtable(L);
    int tn = 1;
    for (const DebuggerThread &th : st.threads)
    {
      lua_newtable(L);
      lua_push_int_field(L, "id", th.id);
      lua_push_str_field(L, "name", th.name);
      lua_newtable(L);
      int fn = 1;
      for (const DebuggerFrame &fr : th.frames)
      {
        lua_newtable(L);
        lua_push_int_field(L, "id", fr.id);
        lua_push_str_field(L, "name", fr.name);
        lua_push_str_field(L, "path", fr.filepath);
        lua_push_int_field(L, "line", (long long)fr.line + 1);
        lua_push_int_field(L, "column", (long long)fr.column + 1);
        lua_rawseti(L, -2, fn++);
      }
      lua_setfield(L, -2, "frames");
      lua_rawseti(L, -2, tn++);
    }
    lua_setfield(L, -2, "threads");
    lua_newtable(L);
    int vn = 1;
    for (const DebuggerVariable &v : st.variables)
    {
      lua_newtable(L);
      lua_push_str_field(L, "name", v.name);
      lua_push_str_field(L, "value", v.value);
      lua_push_str_field(L, "type", v.type);
      lua_push_int_field(L, "variables_reference", v.variables_reference);
      lua_rawseti(L, -2, vn++);
    }
    lua_setfield(L, -2, "variables");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_debugger_breakpoints(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &kv : editor->debugger_breakpoints)
  {
    for (const DebuggerBreakpoint &bp : kv.second)
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", bp.filepath);
      lua_push_int_field(L, "line", (long long)bp.line + 1);
      lua_push_bool_field(L, "verified", bp.verified);
      lua_rawseti(L, -2, n++);
    }
  }
}

void LuaAPI::debugger_toggle_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  if (line < 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->toggle_debugger_breakpoint(path, line);
  lua_pushboolean(L, editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::debugger_has_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  lua_pushboolean(L, line >= 0 && editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::push_theme_palette(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const Theme &t = editor->theme;
  lua_newtable(L);
  auto color = [&](const char *slot, int fg, int bg)
  {
    lua_newtable(L);
    lua_push_int_field(L, "fg", fg);
    if (bg >= 0)
      lua_push_int_field(L, "bg", bg);
    lua_setfield(L, -2, slot);
  };
  auto flag = [&](const char *slot, bool v) { lua_push_bool_field(L, slot, v); };
  color("default", t.fg_default, t.bg_default);
  color("keyword", t.fg_keyword, t.bg_keyword);
  color("string", t.fg_string, t.bg_string);
  color("comment", t.fg_comment, t.bg_comment);
  color("number", t.fg_number, t.bg_number);
  color("function", t.fg_function, t.bg_function);
  color("type", t.fg_type, t.bg_type);
  color("variable", t.fg_variable, t.bg_variable);
  color("constant", t.fg_constant, t.bg_constant);
  color("builtin", t.fg_builtin, t.bg_builtin);
  color("operator", t.fg_operator, t.bg_operator);
  color("punctuation", t.fg_punctuation, t.bg_punctuation);
  color("tag", t.fg_tag, t.bg_tag);
  color("attribute", t.fg_attribute, t.bg_attribute);
  color("namespace", t.fg_namespace, t.bg_namespace);
  color("module", t.fg_module, t.bg_module);
  color("parameter", t.fg_parameter, t.bg_parameter);
  color("field", t.fg_field, t.bg_field);
  color("keyword_control", t.fg_keyword_control, t.bg_keyword_control);
  color("keyword_storage", t.fg_keyword_storage, t.bg_keyword_storage);
  color("keyword_preproc", t.fg_keyword_preproc, t.bg_keyword_preproc);
  color("function_method", t.fg_function_method, t.bg_function_method);
  color("function_constructor", t.fg_function_constructor, t.bg_function_constructor);
  color("type_builtin", t.fg_type_builtin, t.bg_type_builtin);
  color("constant_macro", t.fg_constant_macro, t.bg_constant_macro);
  color("string_escape", t.fg_string_escape, t.bg_string_escape);
  color("punctuation_bracket", t.fg_punctuation_bracket, t.bg_punctuation_bracket);
  color("punctuation_delimiter", t.fg_punctuation_delimiter, t.bg_punctuation_delimiter);
  color("panel_border", t.fg_panel_border, t.bg_panel_border);
  color("selection", t.fg_selection, t.bg_selection);
  color("line_num", t.fg_line_num, t.bg_line_num);
  color("cursor", t.fg_cursor, t.bg_cursor);
  color("status", t.fg_status, t.bg_status);
  color("status_message", t.fg_status_message, -1);
  color("status_logo", t.fg_status_logo, t.bg_status_logo);
  color("status_file", t.fg_status_file, t.bg_status_file);
  color("status_info", t.fg_status_info, t.bg_status_info);
  color("status_warning", t.fg_status_warning, t.bg_status_warning);
  color("status_error", t.fg_status_error, t.bg_status_error);
  color("status_muted", t.fg_status_muted, t.bg_status_muted);
  color("command", t.fg_command, t.bg_command);
  color("search_match", t.fg_search_match, t.bg_search_match);
  color("minimap", t.fg_minimap, t.bg_minimap);
  color("sidebar", t.fg_sidebar, t.bg_sidebar);
  color("sidebar_directory", t.fg_sidebar_directory, -1);
  color("sidebar_selected", t.fg_sidebar_selected, t.bg_sidebar_selected);
  color(
      "sidebar_selected_inactive", t.fg_sidebar_selected_inactive, t.bg_sidebar_selected_inactive);
  color("sidebar_border", t.fg_sidebar_border, -1);
  color("tab_active", t.fg_tab_active, t.bg_tab_active);
  color("tab_inactive", t.fg_tab_inactive, t.bg_tab_inactive);
  color("tab_close", t.fg_tab_close, -1);
  color("tab_separator", t.fg_tab_separator, -1);
  color("active_border", t.fg_active_border, t.bg_active_border);
  color("image_border", t.fg_image_border, t.bg_image_border);
  color("diagnostic_error", t.fg_diagnostic_error, -1);
  color("diagnostic_warning", t.fg_diagnostic_warning, -1);
  color("diagnostic_info", t.fg_diagnostic_info, -1);
  color("diagnostic_hint", t.fg_diagnostic_hint, -1);
  color("bracket1", t.fg_bracket1, -1);
  color("bracket2", t.fg_bracket2, -1);
  color("bracket3", t.fg_bracket3, -1);
  color("bracket4", t.fg_bracket4, -1);
  color("bracket5", t.fg_bracket5, -1);
  color("bracket6", t.fg_bracket6, -1);
  color("bracket_match", t.fg_bracket_match, t.bg_bracket_match);
  color("telescope", t.fg_telescope, t.bg_telescope);
  color("telescope_selected", t.fg_telescope_selected, t.bg_telescope_selected);
  color("telescope_preview", t.fg_telescope_preview, t.bg_telescope_preview);
  color("terminal", t.fg_terminal, t.bg_terminal);
  color("terminal_tab_inactive", t.fg_terminal_tab_inactive, t.bg_terminal_tab_inactive);
  color("terminal_tab_active", t.fg_terminal_tab_active, t.bg_terminal_tab_active);
  color("terminal_tab_focused", t.fg_terminal_tab_focused, t.bg_terminal_tab_focused);
  color("terminal_tab_close", t.fg_terminal_tab_close, -1);
  color("terminal_tab_plus", t.fg_terminal_tab_plus, -1);
  color("terminal_tab_separator", t.fg_terminal_tab_separator, -1);
  color("git_modified", t.fg_git_modified, t.bg_git_modified);
  color("git_added", t.fg_git_added, t.bg_git_added);
  color("git_deleted", t.fg_git_deleted, t.bg_git_deleted);
  color("git_renamed", t.fg_git_renamed, t.bg_git_renamed);
  color("git_untracked", t.fg_git_untracked, t.bg_git_untracked);
  color("git_conflict", t.fg_git_conflict, t.bg_git_conflict);
  flag("syntax_variable_explicit", t.syntax_variable_explicit);
  flag("syntax_constant_explicit", t.syntax_constant_explicit);
  flag("syntax_builtin_explicit", t.syntax_builtin_explicit);
  flag("syntax_operator_explicit", t.syntax_operator_explicit);
  flag("syntax_punctuation_explicit", t.syntax_punctuation_explicit);
  flag("syntax_tag_explicit", t.syntax_tag_explicit);
  flag("syntax_attribute_explicit", t.syntax_attribute_explicit);
  flag("syntax_namespace_explicit", t.syntax_namespace_explicit);
  flag("syntax_module_explicit", t.syntax_module_explicit);
  flag("syntax_parameter_explicit", t.syntax_parameter_explicit);
  flag("syntax_field_explicit", t.syntax_field_explicit);
  flag("syntax_keyword_control_explicit", t.syntax_keyword_control_explicit);
  flag("syntax_keyword_storage_explicit", t.syntax_keyword_storage_explicit);
  flag("syntax_keyword_preproc_explicit", t.syntax_keyword_preproc_explicit);
  flag("syntax_function_method_explicit", t.syntax_function_method_explicit);
  flag("syntax_function_constructor_explicit", t.syntax_function_constructor_explicit);
  flag("syntax_type_builtin_explicit", t.syntax_type_builtin_explicit);
  flag("syntax_constant_macro_explicit", t.syntax_constant_macro_explicit);
  flag("syntax_string_escape_explicit", t.syntax_string_escape_explicit);
  flag("syntax_punctuation_bracket_explicit", t.syntax_punctuation_bracket_explicit);
  flag("syntax_punctuation_delimiter_explicit", t.syntax_punctuation_delimiter_explicit);
}

void LuaAPI::push_buffer_lines(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  for (const std::string &line : buf.lines)
  {
    lua_pushlstring(L, line.data(), line.size());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_clipboard_text(lua_State *L)
{
  lua_pushstring(L, editor ? editor->clipboard.c_str() : "");
}

void LuaAPI::emit_debugger_state_changed()
{
  if (!editor || !has_event_subscribers("debugger.state_changed"))
    return;
  std::string sig;
  for (const auto &st : editor->debugger_session_state)
  {
    sig += st.name + "|" + (st.running ? "1" : "0") + (st.stopped ? "1" : "0") + "|"
           + std::to_string(st.active_thread_id) + "|" + std::to_string(st.active_frame_id) + "|"
           + std::to_string(st.threads.size()) + "|" + std::to_string(st.variables.size()) + ";";
  }
  if (sig == last_debugger_sig_)
    return;
  last_debugger_sig_ = sig;
  emit_event_bus("debugger.state_changed",
                 [this](lua_State *L)
                 {
                   lua_newtable(L);
                   int n = 1;
                   for (const auto &st : editor->debugger_session_state)
                   {
                     lua_newtable(L);
                     lua_push_str_field(L, "name", st.name);
                     lua_push_str_field(L, "adapter", st.adapter);
                     lua_push_bool_field(L, "running", st.running);
                     lua_push_bool_field(L, "stopped", st.stopped);
                     lua_push_int_field(L, "active_thread_id", st.active_thread_id);
                     lua_push_int_field(L, "active_frame_id", st.active_frame_id);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "sessions");
                 });
}

// ---------------------------------------------------------------------------
// One-shot debugger requests (jot.debugger.request_stack / request_variables
// / request_threads)
// ---------------------------------------------------------------------------

void LuaAPI::debugger_request_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int target = editor->current_debugger_session;
  if (lua_isnumber(L, 2))
    target = (int)luaL_checkinteger(L, 2) - 1;
  if (target < 0 || target >= (int)editor->debugger_sessions.size()
      || !editor->debugger_sessions[(size_t)target])
  {
    return; // no live session for the target: a sink would never deliver
  }
  DebuggerClient *client = editor->debugger_sessions[(size_t)target].get();
  DebuggerSessionState empty;
  const DebuggerSessionState &st = target < (int)editor->debugger_session_state.size()
                                       ? editor->debugger_session_state[(size_t)target]
                                       : empty;
  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "dbg." + std::to_string(kind) + "." + std::to_string(ref);
  lua_callbacks[id] = ref;
  pending_debugger_session = target;
  switch (kind)
  {
  case 0:
    pending_debugger_stack = id;
    if (st.active_thread_id > 0)
      client->stack_trace(st.active_thread_id);
    break;
  case 1:
    pending_debugger_variables = id;
    if (st.active_frame_id > 0)
      client->scopes(st.active_frame_id);
    break;
  default:
    pending_debugger_threads = id;
    client->threads();
    break;
  }
}

bool LuaAPI::try_deliver_debugger_stack(int session, const std::vector<DebuggerFrame> &frames)
{
  if (pending_debugger_stack.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_stack;
  pending_debugger_stack.clear();
  pending_debugger_session = -1;
  int thread_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    thread_id = editor->debugger_session_state[(size_t)session].active_thread_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "thread_id", thread_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerFrame &f : frames)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", f.id);
                                  lua_push_str_field(L, "name", f.name);
                                  lua_push_str_field(L, "path", f.filepath);
                                  lua_push_int_field(L, "line", (long long)f.line + 1);
                                  lua_push_int_field(L, "column", (long long)f.column + 1);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "frames");
                              });
}

bool LuaAPI::try_deliver_debugger_variables(int session,
                                            const std::vector<DebuggerVariable> &variables)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_variables;
  pending_debugger_variables.clear();
  pending_debugger_session = -1;
  int frame_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    frame_id = editor->debugger_session_state[(size_t)session].active_frame_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "frame_id", frame_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerVariable &v : variables)
                                {
                                  lua_newtable(L);
                                  lua_push_str_field(L, "name", v.name);
                                  lua_push_str_field(L, "value", v.value);
                                  lua_push_str_field(L, "type", v.type);
                                  lua_push_int_field(
                                      L, "variables_reference", v.variables_reference);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "variables");
                              });
}

bool LuaAPI::try_deliver_debugger_threads(int session, const std::vector<DebuggerThread> &threads)
{
  if (pending_debugger_threads.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_threads;
  pending_debugger_threads.clear();
  pending_debugger_session = -1;
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerThread &t : threads)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", t.id);
                                  lua_push_str_field(L, "name", t.name);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "threads");
                              });
}

bool LuaAPI::debugger_chain_variables(int session, const std::vector<DebuggerVariable> &scopes)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  if (!editor || session < 0 || session >= (int)editor->debugger_sessions.size())
  {
    return false;
  }
  auto &client = editor->debugger_sessions[(size_t)session];
  if (!client)
    return false;
  for (const DebuggerVariable &scope : scopes)
  {
    if (scope.variables_reference > 0)
    {
      client->variables(scope.variables_reference);
      return true;
    }
  }
  // No expandable scope: deliver an empty answer so the sink cannot leak.
  try_deliver_debugger_variables(session, {});
  return false;
}

// ---------------------------------------------------------------------------
// Batch 6: jot.motion / jot.sidebar / LSP manager actions / buffer extras /
// jot.git.diff / config.changed bus event
// ---------------------------------------------------------------------------

void LuaAPI::motion_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  switch (kind)
  {
  case 0:
    editor->move_word_forward(false);
    break;
  case 1:
    editor->move_word_backward(false);
    break;
  case 2:
    editor->move_to_line_smart_start(false);
    break;
  case 3:
    editor->move_to_line_end(false);
    break;
  case 4:
    editor->move_to_file_start(false);
    break;
  case 5:
    editor->move_to_file_end(false);
    break;
  case 6:
    editor->jump_to_matching_bracket();
    break;
  case 7:
    editor->select_current_function();
    break;
  default:
    return;
  }
  editor->needs_redraw = true;
}

void LuaAPI::push_sidebar_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const bool git = editor->active_sidebar_view == editor->SIDEBAR_VIEW_GIT;
  lua_push_bool_field(L, "visible", editor->show_sidebar);
  lua_push_int_field(L, "width", editor->sidebar_width);
  lua_push_str_field(L, "view", git ? "git" : "explorer");
  lua_push_int_field(
      L, "selected", git ? editor->git_sidebar_selected : editor->file_tree_selected);
  lua_push_int_field(L, "scroll", git ? editor->git_sidebar_scroll : editor->file_tree_scroll);
}

void LuaAPI::sidebar_set_view_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const std::string view = luaL_checkstring(L, 1);
  editor->active_sidebar_view =
      view == "git" ? editor->SIDEBAR_VIEW_GIT : editor->SIDEBAR_VIEW_EXPLORER;
  editor->show_sidebar = true;
  editor->needs_redraw = true;
}

void LuaAPI::push_lsp_disabled(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const std::string &s : editor->lsp_disabled_servers)
  {
    lua_pushstring(L, s.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::lsp_set_enabled_from_lua(lua_State *L)
{
  if (!editor)
    return;
  editor->set_lsp_server_enabled(luaL_checkstring(L, 1), lua_toboolean(L, 2));
}

void LuaAPI::lsp_install_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->install_lsp_server(luaL_checkstring(L, 1)));
}

void LuaAPI::lsp_remove_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->remove_lsp_server(luaL_checkstring(L, 1)));
}

void LuaAPI::lsp_restart_all_from_lua(lua_State *L)
{
  if (editor)
    editor->restart_all_lsp_clients();
}

void LuaAPI::push_buffer_filetype(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_pushstring(L, editor->get_file_extension(buf.filepath).c_str());
}

void LuaAPI::push_buffer_get_line(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int line = (int)luaL_checkinteger(L, 1) - 1;
  const int idx = resolve_buffer_arg(L, 2);
  if (idx < 0 || line < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line >= buf.line_count())
  {
    lua_pushnil(L);
    return;
  }
  const std::string &s = buf.line(line);
  lua_pushlstring(L, s.data(), s.size());
}

void LuaAPI::git_diff_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const bool staged = lua_toboolean(L, 2);
  lua_pushboolean(L, editor->open_git_diff_panel(path, staged));
}
