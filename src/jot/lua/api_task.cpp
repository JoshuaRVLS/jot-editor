// LuaAPI task surface: background job launching, job-result delivery,
// and the task list shown in the tasks panel.
#include "editor.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/lua/api_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

#include <string>
#include <vector>

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

