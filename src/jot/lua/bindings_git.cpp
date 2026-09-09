// Lua host bindings: Git status, diff, and stage/commit bindings.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

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
} // namespace lua_bind
