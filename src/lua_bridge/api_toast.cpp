// jot.toast API bridge: keeps a single reference to the Lua toast module
// table (src/lua/features/ui/toast.lua) and forwards the native calls to it.
// All toast logic lives in Lua; this file only marshals the calls.
#include "editor.h"
#include "lua_bridge/api.h"

#include <iostream>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace
{
  // Invokes module[field] with `nargs` arguments from the top of the stack,
  // leaving `nresults` results. Returns false when no module is registered or
  // the call fails.
  bool call_toast_module(LuaAPI *api, lua_State *L, const char *field, int nargs, int nresults)
  {
    if (!api || api->toast_module_ref_ < 0)
    {
      lua_settop(L, 0);
      return false;
    }
    const int base = lua_gettop(L); // the arguments arrive above `base`
    lua_rawgeti(L, LUA_REGISTRYINDEX, api->toast_module_ref_); // module
    if (!lua_istable(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    lua_getfield(L, -1, field); // module, fn
    if (!lua_isfunction(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    // Drop the module value, then move the function below the arguments:
    // lua_pcall expects it at -nargs-1 with the args on top. The arguments
    // start at stack index 1 for these bindings, so inserting at 1 is safe.
    lua_remove(L, -2);
    lua_insert(L, 1);
    if (lua_pcall(L, nargs, nresults, 0) != LUA_OK)
    {
      std::cerr << "toast API error (" << field << "): " << lua_tostring(L, -1) << "\n";
      lua_settop(L, base);
      return false;
    }
    return true;
  }
} // namespace

void LuaAPI::register_toast_module(lua_State *L)
{
  if (toast_module_ref_ >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, toast_module_ref_);
    toast_module_ref_ = -1;
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, 1);
  toast_module_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaAPI::toast_show_from_lua(lua_State *L)
{
  // (opts) -> toast id, or 0 when no module is registered.
  if (!call_toast_module(this, L, "show", 1, 1))
  {
    lua_pushinteger(L, 0);
    return;
  }
}

void LuaAPI::toast_dismiss_from_lua(lua_State *L)
{
  // (id?) — dismiss a specific toast, or the newest when omitted.
  if (!call_toast_module(this, L, "dismiss", lua_gettop(L), 0))
  {
    return;
  }
}

void LuaAPI::toast_clear_from_lua(lua_State *L)
{
  call_toast_module(this, L, "clear", 0, 0);
}

void LuaAPI::toast_info_from_lua(lua_State *L)
{
  // () -> {count, visible}
  if (!call_toast_module(this, L, "info", 0, 1))
  {
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "count");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "visible");
  }
}