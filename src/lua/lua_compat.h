#ifndef JOT_LUA_COMPAT_H
#define JOT_LUA_COMPAT_H

// The LuaJIT API gaps, in one header so no call site carries an #ifdef.
//
// The embedded runtime is LuaJIT, which is Lua 5.1 plus the JIT. Four entry
// points added after 5.1 are used here, and this is what stands in for them:
//
//   lua_rawlen     5.2   -> lua_objlen, the same length under the older name
//   lua_absindex   5.2   -> the 5.2 formula, spelled out
//   lua_len        5.2   -> lua_objlen pushed as an integer
//   lua_isinteger  5.3   -> a number with no fractional part, which is what a
//                           5.1 caller asking "is this an integer?" can mean
//
// They are the real names, not renamed wrappers, so a call site written against
// the interface reads the same as it would on a newer Lua. Include this header
// outside any `extern "C"` block the Lua headers sit in: it defines C++
// functions.
//
// The Lua-side half of the dialect (utf8, which LuaJIT also lacks) is the
// bundled module runtime/lua/lib/utf8.lua, loaded by the host. docs/LUA_API.md,
// "The scripting dialect", is the contract both halves keep.
#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <cstddef>

#if LUA_VERSION_NUM < 502
// LuaJIT has reported 501 since its first release; if that ever changes, this
// is where the header would stop defining them.
static inline size_t lua_rawlen(lua_State *L, int index)
{
  return lua_objlen(L, index);
}

static inline int lua_absindex(lua_State *L, int index)
{
  // 5.2's formula: a positive index is already absolute, a pseudo-index is left
  // alone, and anything else is counted from the top of the stack.
  if (index > 0 || index <= LUA_REGISTRYINDEX)
  {
    return index;
  }
  return lua_gettop(L) + index + 1;
}

// 5.2 pushes the length as an integer, running __len for a value that has one.
// Lua 5.1 has the raw length instead, which is the answer for the tables and
// strings read here and the only one it can give.
static inline void lua_len(lua_State *L, int index)
{
  lua_pushinteger(L, (lua_Integer)lua_objlen(L, index));
}
#endif

#if LUA_VERSION_NUM < 503
static inline int lua_isinteger(lua_State *L, int index)
{
  if (!lua_isnumber(L, index))
  {
    return 0;
  }
  const double value = (double)lua_tonumber(L, index);
  return value == std::floor(value) ? 1 : 0;
}
#endif

// Which release of the engine this is ("LuaJIT 2.1.1788856981", what a session
// reports as jot.engine) is read off the running runtime -- jit.version -- and
// not off <luajit.h>: that header is written by LuaJIT's own build, so
// including it would make every translation unit here wait on that build
// instead of only the link. See LuaAPI::init, api_bindings.cpp.

#endif
