// Lua host bindings: Pane layout bindings (split, focus, resize, zoom, swap, equalize).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
  int l_resize_direction(lua_State *L)
  {
    const std::string dir = luaL_checkstring(L, 1);
    const char d = dir.empty() ? '\0' : (char)std::tolower((unsigned char)dir[0]);
    const int step = (int)luaL_optinteger(L, 2, 1);
    lua_pushboolean(L, api(L).host().render.resize_focused_pane_direction(d, step));
    return 1;
  }
  int l_equalize(lua_State *L)
  {
    api(L).host().render.equalize_panes();
    return 0;
  }
  int l_zoom(lua_State *L)
  {
    api(L).host().render.toggle_pane_zoom();
    return 0;
  }
  int l_swap(lua_State *L)
  {
    api(L).host().render.swap_panes();
    return 0;
  }
} // namespace lua_bind
