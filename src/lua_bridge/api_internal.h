// Shared small helpers for the Lua bridge translation units. The LuaAPI class
// lives in api.h; the utilities here (string transforms and lua_State table
// accessors) are used from several bridge .cpp files, so they are defined once
// as inline functions instead of being duplicated per translation unit.
#pragma once

#include "lua_bridge/embedded_lua.h"
#include "lua_bridge/lua_loader.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

#ifndef _WIN32
#include <sys/wait.h>
#endif

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

class LuaAPI;

// Runs a one-shot Lua callback (registered under `id` in `callbacks`) with a
// single table argument built by `build`. Returns true when the callback
// existed and ran. Shared by the LSP and debugger result deliverers.
template <typename BuildFn>
inline bool lua_deliver_one_shot(const std::string &id,
                                 std::unordered_map<std::string, int> &callbacks,
                                 void *lua_state,
                                 BuildFn build)
{
  if (id.empty() || !lua_state)
    return false;
  auto it = callbacks.find(id);
  if (it == callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  const int ref = it->second;
  callbacks.erase(it);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  build(L);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua one-shot callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return true;
}

// Single active bridge instance, used as the fallback target for namespaced
// binding functions that have no upvalue (see api_bindings.cpp). Defined in
// api_bindings.cpp, assigned by the LuaAPI constructor / destructor.
extern LuaAPI *g_lua_active_api;

namespace jot_lua
{
  inline std::string lower(std::string s)
  {
    for (char &c : s)
      c = (char)std::tolower((unsigned char)c);
    return s;
  }

  inline std::string key_name(const std::string &s)
  {
    std::string out;
    for (char c : s)
      if (!std::isspace((unsigned char)c))
        out += c;
    return out;
  }

  inline std::string lua_shell_quote(const std::string &value)
  {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value)
    {
      if (c == '"')
        out += "\"\"";
      else
        out.push_back(c);
    }
    return out + '"';
#else
    std::string out = "'";
    for (char c : value)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out.push_back(c);
    }
    return out + "'";
#endif
  }

  inline const char *lua_diag_severity_name(int severity)
  {
    switch (severity)
    {
    case 1:
      return "Error";
    case 2:
      return "Warning";
    case 3:
      return "Info";
    case 4:
      return "Hint";
    default:
      return "Diagnostic";
    }
  }

  // Runs `command` through the shell on the calling thread, merging stderr into
  // stdout. Returns {output, exit_code}. Used by worker threads only.
  inline std::pair<std::string, int> lua_capture_shell(const std::string &command)
  {
    std::array<char, 512> buf{};
    std::string out;
    FILE *pipe = nullptr;
#ifdef _WIN32
    pipe = _popen((command + " 2>&1").c_str(), "r");
#else
    pipe = popen((command + " 2>&1").c_str(), "r");
#endif
    if (!pipe)
      return {std::string(), 1};
    while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr)
      out += buf.data();
    int status = 0;
#ifdef _WIN32
    status = _pclose(pipe);
#else
    status = pclose(pipe);
#endif
    int code = 1;
#ifndef _WIN32
    if (status != -1 && WIFEXITED(status))
      code = WEXITSTATUS(status);
#else
    code = status;
#endif
    return {std::move(out), code};
  }

  // Loads a bundled Lua feature file. The first candidate path on disk (user
  // config dir -> install data dir -> developer source dir) wins so edits never
  // need a recompile; otherwise the copy embedded into the binary runs straight
  // from memory (no disk writes needed). Shared by the hover / ui-kit runtimes.
  inline bool load_bundled_lua_file(lua_State *L, const char *rel_path, const char *label)
  {
    const auto candidates = jot_lua_candidate_paths(rel_path);
    if (!candidates.empty())
    {
      const int top = lua_gettop(L);
      if (luaL_loadfile(L, candidates.front().string().c_str()) || lua_pcall(L, 0, 0, 0))
      {
        std::cerr << label << " Lua runtime failed: "
                  << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown") << "\n";
        lua_settop(L, top);
        return false;
      }
      return true;
    }
    size_t size = 0;
    const unsigned char *data = jot_embedded::find(rel_path, &size);
    if (!data || size == 0)
    {
      return false;
    }
    const int top = lua_gettop(L);
    if (luaL_loadbuffer(L, reinterpret_cast<const char *>(data), size, rel_path)
        || lua_pcall(L, 0, 0, 0))
    {
      std::cerr << label << " embedded Lua runtime failed: "
                << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown") << "\n";
      lua_settop(L, top);
      return false;
    }
    return true;
  }

  // Reads an integer / boolean / string field off a Lua table, falling back to
  // a default when the field is absent or has the wrong type.
  inline int table_int(lua_State *L, int i, const char *k, int d)
  {
    lua_getfield(L, i, k);
    int v = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : d;
    lua_pop(L, 1);
    return v;
  }

  inline bool table_bool(lua_State *L, int i, const char *k, bool d)
  {
    lua_getfield(L, i, k);
    bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : d;
    lua_pop(L, 1);
    return v;
  }

  inline std::string table_string(lua_State *L, int i, const char *k, const std::string &d)
  {
    lua_getfield(L, i, k);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : d;
    lua_pop(L, 1);
    return v;
  }

  // Push-and-set helpers for building payload tables on top of the stack.
  inline void lua_push_str_field(lua_State *L, const char *k, const std::string &v)
  {
    lua_pushstring(L, v.c_str());
    lua_setfield(L, -2, k);
  }

  inline void lua_push_int_field(lua_State *L, const char *k, long long v)
  {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
  }

  inline void lua_push_bool_field(lua_State *L, const char *k, bool v)
  {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, k);
  }

  // Absolute-index variants used when building UI payload tables.
  inline void lua_set_str_field(lua_State *L, int t, const char *k, const std::string &v)
  {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, t, k);
  }

  inline void lua_set_int_field(lua_State *L, int t, const char *k, long long v)
  {
    lua_pushinteger(L, v);
    lua_setfield(L, t, k);
  }

  inline void lua_set_bool_field(lua_State *L, int t, const char *k, bool v)
  {
    lua_pushboolean(L, v);
    lua_setfield(L, t, k);
  }
} // namespace jot_lua
