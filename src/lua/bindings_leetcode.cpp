#include "extras/leetcode_http.h"
#include "lua/api.h"
#include "lua/bindings_internal.h"
#include "extras/leetcode_credentials.h"

#include <string>

namespace lua_bind
{
  namespace
  {
    std::string field_string(lua_State *L, int table, const char *key)
    {
      lua_getfield(L, table, key);
      std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
      lua_pop(L, 1);
      return out;
    }

    void push_credential_result(lua_State *L, const LeetCodeCredentials::Result &result)
    {
      lua_createtable(L, 0, 4);
      lua_pushboolean(L, result.ok);
      lua_setfield(L, -2, "ok");
      lua_pushboolean(L, result.available);
      lua_setfield(L, -2, "available");
      lua_pushlstring(L, result.value.data(), result.value.size());
      lua_setfield(L, -2, "value");
      lua_pushlstring(L, result.error.data(), result.error.size());
      lua_setfield(L, -2, "error");
    }
  } // namespace

  int l_leetcode_request(lua_State *L)
  {
    LuaAPI &a = api(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    LeetCodeHttp::Request request;
    request.method = field_string(L, 1, "method");
    request.url = field_string(L, 1, "url");
    request.body = field_string(L, 1, "body");
    lua_getfield(L, 1, "timeout");
    if (lua_isinteger(L, -1))
      request.timeout_seconds = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1))
    {
      const int headers = lua_absindex(L, -1);
      const size_t count = lua_rawlen(L, headers);
      for (size_t i = 1; i <= count; i++)
      {
        lua_rawgeti(L, headers, (int)i);
        if (lua_istable(L, -1))
        {
          request.headers.push_back({field_string(L, -1, "name"), field_string(L, -1, "value")});
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);

    std::string error;
    if (!LeetCodeHttp::valid_request(request, error))
    {
      lua_pushnil(L);
      lua_pushlstring(L, error.data(), error.size());
      return 2;
    }
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    const std::string callback = "leetcode.http." + std::to_string(ref);
    a.lua_callbacks[callback] = ref;
    if (!a.leetcode_http_request(request, callback))
    {
      a.lua_callbacks.erase(callback);
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
      lua_pushnil(L);
      lua_pushstring(L, "HTTP task queue is unavailable");
      return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
  }

  int l_leetcode_credential_get(lua_State *L)
  {
    push_credential_result(L, LeetCodeCredentials::get());
    return 1;
  }

  int l_leetcode_credential_set(lua_State *L)
  {
    const char *value = luaL_checkstring(L, 1);
    push_credential_result(L, LeetCodeCredentials::set(value));
    return 1;
  }

  int l_leetcode_credential_delete(lua_State *L)
  {
    push_credential_result(L, LeetCodeCredentials::erase());
    return 1;
  }
} // namespace lua_bind
