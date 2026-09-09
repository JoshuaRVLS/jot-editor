// LuaAPI debugger surface: session state, breakpoints, stack /
// variables / threads delivery, and the debugger request dispatcher.
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

void LuaAPI::push_debugger_configs(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  editor->load_debugger_configs();
  int n = 1;
  for (const auto &cfg : editor->debugger_configs)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", cfg.name);
    lua_push_str_field(L, "program", cfg.program);
    lua_push_str_field(L, "adapter", cfg.adapter);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::run_debugger_config_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->run_debugger_config(luaL_checkstring(L, 1)));
}

void LuaAPI::push_debugger_state(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  const size_t sessions =
      std::min(editor->debugger_sessions.size(), editor->debugger_session_state.size());
  for (size_t i = 0; i < sessions; i++)
  {
    const DebuggerSessionState &st = editor->debugger_session_state[i];
    lua_newtable(L);
    lua_push_str_field(L, "name", st.name);
    lua_push_str_field(L, "adapter", st.adapter);
    lua_push_str_field(L, "program", st.program);
    lua_push_bool_field(L, "running", st.running);
    lua_push_bool_field(L, "stopped", st.stopped);
    lua_push_int_field(L, "active_thread_id", st.active_thread_id);
    lua_push_int_field(L, "active_frame_id", st.active_frame_id);
    lua_push_str_field(L, "output", st.output);
    lua_push_str_field(L, "last_error", st.last_error);
    lua_newtable(L);
    int tn = 1;
    for (const DebuggerThread &th : st.threads)
    {
      lua_newtable(L);
      lua_push_int_field(L, "id", th.id);
      lua_push_str_field(L, "name", th.name);
      lua_newtable(L);
      int fn = 1;
      for (const DebuggerFrame &fr : th.frames)
      {
        lua_newtable(L);
        lua_push_int_field(L, "id", fr.id);
        lua_push_str_field(L, "name", fr.name);
        lua_push_str_field(L, "path", fr.filepath);
        lua_push_int_field(L, "line", (long long)fr.line + 1);
        lua_push_int_field(L, "column", (long long)fr.column + 1);
        lua_rawseti(L, -2, fn++);
      }
      lua_setfield(L, -2, "frames");
      lua_rawseti(L, -2, tn++);
    }
    lua_setfield(L, -2, "threads");
    lua_newtable(L);
    int vn = 1;
    for (const DebuggerVariable &v : st.variables)
    {
      lua_newtable(L);
      lua_push_str_field(L, "name", v.name);
      lua_push_str_field(L, "value", v.value);
      lua_push_str_field(L, "type", v.type);
      lua_push_int_field(L, "variables_reference", v.variables_reference);
      lua_rawseti(L, -2, vn++);
    }
    lua_setfield(L, -2, "variables");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_debugger_breakpoints(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &kv : editor->debugger_breakpoints)
  {
    for (const DebuggerBreakpoint &bp : kv.second)
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", bp.filepath);
      lua_push_int_field(L, "line", (long long)bp.line + 1);
      lua_push_bool_field(L, "verified", bp.verified);
      lua_rawseti(L, -2, n++);
    }
  }
}

void LuaAPI::debugger_toggle_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  if (line < 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->toggle_debugger_breakpoint(path, line);
  lua_pushboolean(L, editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::debugger_has_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  lua_pushboolean(L, line >= 0 && editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::debugger_scroll_output_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const int delta = (int)luaL_checkinteger(L, 1);
  const int before = editor->current_debugger_session >= 0
                         && editor->current_debugger_session
                                < (int)editor->debugger_session_state.size()
                     ? editor->debugger_session_state[editor->current_debugger_session].output_scroll
                     : 0;
  editor->debugger_scroll_output(delta);
  const int after = editor->current_debugger_session >= 0
                        && editor->current_debugger_session
                               < (int)editor->debugger_session_state.size()
                    ? editor->debugger_session_state[editor->current_debugger_session].output_scroll
                    : 0;
  lua_pushboolean(L, after != before);
}

void LuaAPI::debugger_cycle_thread_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const int delta = (int)luaL_checkinteger(L, 1);
  const int session = editor->current_debugger_session;
  const int before =
      session >= 0 && session < (int)editor->debugger_session_state.size()
          ? editor->debugger_session_state[session].active_thread_id
          : 0;
  editor->debugger_cycle_thread(delta);
  const int after =
      session >= 0 && session < (int)editor->debugger_session_state.size()
          ? editor->debugger_session_state[session].active_thread_id
          : 0;
  lua_pushboolean(L, after != before);
}

void LuaAPI::debugger_cycle_frame_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const int delta = (int)luaL_checkinteger(L, 1);
  const int session = editor->current_debugger_session;
  const int before =
      session >= 0 && session < (int)editor->debugger_session_state.size()
          ? editor->debugger_session_state[session].active_frame_id
          : 0;
  editor->debugger_cycle_frame(delta);
  const int after =
      session >= 0 && session < (int)editor->debugger_session_state.size()
          ? editor->debugger_session_state[session].active_frame_id
          : 0;
  lua_pushboolean(L, after != before);
}

void LuaAPI::emit_debugger_state_changed()
{
  if (!editor || !has_event_subscribers("debugger.state_changed"))
    return;
  std::string sig;
  for (const auto &st : editor->debugger_session_state)
  {
    sig += st.name + "|" + (st.running ? "1" : "0") + (st.stopped ? "1" : "0") + "|"
           + std::to_string(st.active_thread_id) + "|" + std::to_string(st.active_frame_id) + "|"
           + std::to_string(st.threads.size()) + "|" + std::to_string(st.variables.size()) + ";";
  }
  if (sig == last_debugger_sig_)
    return;
  last_debugger_sig_ = sig;
  emit_event_bus("debugger.state_changed",
                 [this](lua_State *L)
                 {
                   lua_newtable(L);
                   int n = 1;
                   for (const auto &st : editor->debugger_session_state)
                   {
                     lua_newtable(L);
                     lua_push_str_field(L, "name", st.name);
                     lua_push_str_field(L, "adapter", st.adapter);
                     lua_push_bool_field(L, "running", st.running);
                     lua_push_bool_field(L, "stopped", st.stopped);
                     lua_push_int_field(L, "active_thread_id", st.active_thread_id);
                     lua_push_int_field(L, "active_frame_id", st.active_frame_id);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "sessions");
                 });
}

// ---------------------------------------------------------------------------
// One-shot debugger requests (jot.debugger.request_stack / request_variables
// / request_threads)
// ---------------------------------------------------------------------------

void LuaAPI::debugger_request_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int target = editor->current_debugger_session;
  if (lua_isnumber(L, 2))
    target = (int)luaL_checkinteger(L, 2) - 1;
  if (target < 0 || target >= (int)editor->debugger_sessions.size()
      || !editor->debugger_sessions[(size_t)target])
  {
    return; // no live session for the target: a sink would never deliver
  }
  DebuggerClient *client = editor->debugger_sessions[(size_t)target].get();
  DebuggerSessionState empty;
  const DebuggerSessionState &st = target < (int)editor->debugger_session_state.size()
                                       ? editor->debugger_session_state[(size_t)target]
                                       : empty;
  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "dbg." + std::to_string(kind) + "." + std::to_string(ref);
  lua_callbacks[id] = ref;
  pending_debugger_session = target;
  switch (kind)
  {
  case 0:
    pending_debugger_stack = id;
    if (st.active_thread_id > 0)
      client->stack_trace(st.active_thread_id);
    break;
  case 1:
    pending_debugger_variables = id;
    if (st.active_frame_id > 0)
      client->scopes(st.active_frame_id);
    break;
  default:
    pending_debugger_threads = id;
    client->threads();
    break;
  }
}

bool LuaAPI::try_deliver_debugger_stack(int session, const std::vector<DebuggerFrame> &frames)
{
  if (pending_debugger_stack.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_stack;
  pending_debugger_stack.clear();
  pending_debugger_session = -1;
  int thread_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    thread_id = editor->debugger_session_state[(size_t)session].active_thread_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "thread_id", thread_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerFrame &f : frames)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", f.id);
                                  lua_push_str_field(L, "name", f.name);
                                  lua_push_str_field(L, "path", f.filepath);
                                  lua_push_int_field(L, "line", (long long)f.line + 1);
                                  lua_push_int_field(L, "column", (long long)f.column + 1);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "frames");
                              });
}

bool LuaAPI::try_deliver_debugger_variables(int session,
                                            const std::vector<DebuggerVariable> &variables)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_variables;
  pending_debugger_variables.clear();
  pending_debugger_session = -1;
  int frame_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    frame_id = editor->debugger_session_state[(size_t)session].active_frame_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "frame_id", frame_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerVariable &v : variables)
                                {
                                  lua_newtable(L);
                                  lua_push_str_field(L, "name", v.name);
                                  lua_push_str_field(L, "value", v.value);
                                  lua_push_str_field(L, "type", v.type);
                                  lua_push_int_field(
                                      L, "variables_reference", v.variables_reference);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "variables");
                              });
}

bool LuaAPI::try_deliver_debugger_threads(int session, const std::vector<DebuggerThread> &threads)
{
  if (pending_debugger_threads.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_threads;
  pending_debugger_threads.clear();
  pending_debugger_session = -1;
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerThread &t : threads)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", t.id);
                                  lua_push_str_field(L, "name", t.name);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "threads");
                              });
}

bool LuaAPI::debugger_chain_variables(int session, const std::vector<DebuggerVariable> &scopes)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  if (!editor || session < 0 || session >= (int)editor->debugger_sessions.size())
  {
    return false;
  }
  auto &client = editor->debugger_sessions[(size_t)session];
  if (!client)
    return false;
  for (const DebuggerVariable &scope : scopes)
  {
    if (scope.variables_reference > 0)
    {
      client->variables(scope.variables_reference);
      return true;
    }
  }
  // No expandable scope: deliver an empty answer so the sink cannot leak.
  try_deliver_debugger_variables(session, {});
  return false;
}

// ---------------------------------------------------------------------------
// Batch 6: jot.motion / jot.sidebar / LSP manager actions / buffer extras /
// jot.git.diff / config.changed bus event
// ---------------------------------------------------------------------------

