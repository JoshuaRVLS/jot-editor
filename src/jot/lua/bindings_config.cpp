// Lua host bindings: Config, theme, and editor-info bindings.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
  int l_theme_palette(lua_State *L)
  {
    api(L).push_theme_palette(L);
    return 1;
  }
} // namespace lua_bind
