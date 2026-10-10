// Lua host bindings: Job capture and task list bindings.

#include "core/editor.h"
#include "lua/api.h"
#include "lua/bindings_internal.h"

namespace lua_bind
{

  // jot.job.capture(command[, cwd], callback) -> true when the job was queued.
  //
  // The boolean is what tells a caller the job is actually running: a caller
  // that owns scratch files for the command (features/ai/http.lua writes the
  // request body, then curls it) must clean up on a refusal and must not on a
  // queued job, and with no return value both cases read as nil.
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
    lua_pushboolean(L, 1);
    return 1;
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
