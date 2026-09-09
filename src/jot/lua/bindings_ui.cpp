// Lua host bindings: UI bindings: scratch buffers, float windows, decorations, handlers, popup, palette, toasts.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
  int l_toast_register(lua_State *L)
  {
    api(L).register_toast_module(L);
    return 0;
  }
  int l_toast_show(lua_State *L)
  {
    api(L).toast_show_from_lua(L);
    return 1;
  }
  int l_toast_dismiss(lua_State *L)
  {
    api(L).toast_dismiss_from_lua(L);
    return 0;
  }
  int l_toast_clear(lua_State *L)
  {
    api(L).toast_clear_from_lua(L);
    return 0;
  }
  int l_toast_info(lua_State *L)
  {
    api(L).toast_info_from_lua(L);
    return 1;
  }
} // namespace lua_bind
