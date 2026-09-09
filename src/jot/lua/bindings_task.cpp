// Lua host bindings: Job capture and task list bindings.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
} // namespace lua_bind
