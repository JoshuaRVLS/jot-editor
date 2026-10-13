#ifndef JOT_LUA_COMPAT_H
#define JOT_LUA_COMPAT_H

// The Lua API differences between the runtimes this editor builds against, in
// one header so no call site carries an #ifdef.
//
// The embedded runtime is LuaJIT when the build can use one (see
// JOT_LUA_ENGINE), and LuaJIT is Lua 5.1 plus the JIT. Four entry points added
// after 5.1 are used here, and this is what stands in for them:
//
//   lua_rawlen     5.2   -> lua_objlen, the same length under the older name
//   lua_absindex   5.2   -> the 5.2 formula, spelled out
//   lua_len        5.2   -> lua_objlen pushed as an integer
//   lua_isinteger  5.3   -> a number with no fractional part, which is what a
//                           5.1 caller asking "is this an integer?" can mean
//
// They are the real names, not renamed wrappers, so the same source compiles on
// either runtime and the call sites read the same. Include this header outside
// any `extern "C"` block the Lua headers sit in: it defines C++ functions.
//
// The Lua-side half of the dialect (utf8, which LuaJIT also lacks) is the
// bundled module runtime/lua/compat/utf8.lua, loaded by the host when the
// runtime does not provide it. docs/LUA_API.md, "The scripting dialect", is the
// contract both halves keep.
#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <cstddef>

#if defined(__has_include)
#if __has_include(<luajit.h>)
// Only LuaJIT ships this header, and it carries the version string a session
// reports (jot.engine).
#include <luajit.h>
#define JOT_LUA_ENGINE_IS_LUAJIT 1
#endif
#endif

#if LUA_VERSION_NUM < 502
// LuaJIT has reported 501 since its first release; if that ever changes, these
// two definitions are where it lands.
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

namespace jot_lua
{
  // The engine behind the dialect: what a session reports as jot.engine, and
  // what a bug report needs to say. The bundled files run on either one.
  inline const char *engine_name()
  {
#if defined(JOT_LUA_ENGINE_IS_LUAJIT)
    return LUAJIT_VERSION;
#else
    return LUA_RELEASE;
#endif
  }
} // namespace jot_lua

#endif
