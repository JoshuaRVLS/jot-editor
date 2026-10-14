// Lua host bindings: Config, theme, and editor-info bindings.

#include "core/editor.h"
#include "lua/api/api.h"
#include "lua/bindings/bindings_internal.h"

namespace lua_bind
{

  int l_config_get(lua_State *L)
  {
    // A table in the second argument names a scope and a target (jot.config.get
    // with {scope = "buffer"} and the like); anything else is the fallback value
    // the plain reader has always taken, so `get(key, "x")` keeps working.
    if (lua_istable(L, 2))
    {
      api(L).option_get_from_lua(L, nullptr);
      return 3;
    }
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
    // An opts table in the third argument names a scope (jot.config.set with
    // {scope = "buffer"} and the like) and answers whether it was written;
    // without one the call is the plain store write it has always been.
    if (lua_istable(L, 3))
    {
      api(L).option_set_from_lua(L, nullptr);
      return 2;
    }
    api(L).config_set_from_lua(L);
    return 0;
  }
  int l_config_unset(lua_State *L)
  {
    if (lua_istable(L, 2))
    {
      api(L).option_unset_from_lua(L, nullptr);
      return 2;
    }
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
  int l_config_info(lua_State *L)
  {
    api(L).push_config_info(L);
    return 1;
  }
  int l_config_list(lua_State *L)
  {
    api(L).push_config_list(L);
    return 1;
  }
  int l_buffer_option_get(lua_State *L)
  {
    api(L).option_get_from_lua(L, "buffer");
    return 3;
  }
  int l_buffer_option_set(lua_State *L)
  {
    api(L).option_set_from_lua(L, "buffer");
    return 2;
  }
  int l_buffer_option_unset(lua_State *L)
  {
    api(L).option_unset_from_lua(L, "buffer");
    return 2;
  }
  int l_window_option_get(lua_State *L)
  {
    api(L).option_get_from_lua(L, "window");
    return 3;
  }
  int l_window_option_set(lua_State *L)
  {
    api(L).option_set_from_lua(L, "window");
    return 2;
  }
  int l_window_option_unset(lua_State *L)
  {
    api(L).option_unset_from_lua(L, "window");
    return 2;
  }
  int l_editor_info(lua_State *L)
  {
    api(L).push_editor_info(L);
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
  int l_theme_current(lua_State *L)
  {
    api(L).push_theme_current(L);
    return 1;
  }
  int l_theme_get(lua_State *L)
  {
    api(L).push_theme_color(L, luaL_optstring(L, 1, ""));
    return 1;
  }
  int l_theme_palette(lua_State *L)
  {
    api(L).push_theme_palette(L);
    return 1;
  }
} // namespace lua_bind
