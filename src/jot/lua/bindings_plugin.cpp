// Lua host bindings: Plugin registration surface (commands, keymaps, autocmds, panels, update handlers, status segments, restart).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
  // Handler for the native :update command. Stored under the reserved
  // "update.cmd" callback id (see LuaAPI::run_update_command).
  int l_register_update_handler(lua_State *L)
  {
    auto &a = api(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    a.set_update_handler_ref(ref);
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
  int l_keymap_remove(lua_State *L)
  {
    api(L).remove_keymap(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
    return 0;
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
} // namespace lua_bind
