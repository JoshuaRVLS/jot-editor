#include "lua_bridge/api.h"
#include <catch2/catch_test_macros.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  int ok(lua_State *L)
  {
    lua_pushboolean(L, 1);
    return 1;
  }
  int name(lua_State *L)
  {
    lua_pushstring(L, "cpp");
    return 1;
  }
  int status(lua_State *L)
  {
    lua_newtable(L);
    return 1;
  }
  int execute(lua_State *)
  {
    return 0;
  }
  void native(lua_State *L, const char *field, lua_CFunction fn)
  {
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, field);
  }
} // namespace

TEST_CASE("Real Lua API bootstrap exposes jot.treesitter.native")
{
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  LuaAPI api(nullptr);
  api.register_treesitter_api(L);

  lua_getglobal(L, "jot");
  REQUIRE(lua_istable(L, -1));
  lua_getfield(L, -1, "treesitter");
  REQUIRE(lua_istable(L, -1));
  lua_getfield(L, -1, "native");
  REQUIRE(lua_istable(L, -1));
  for (const char *fn : {"register_language",
                         "language_for_extension",
                         "disable_language",
                         "install_command",
                         "status",
                         "parser",
                         "parse",
                         "query",
                         "captures",
                         "set_query",
                         "set_capture_color",
                         "reload"})
  {
    lua_getfield(L, -1, fn);
    REQUIRE(lua_isfunction(L, -1));
    lua_pop(L, 1);
  }
  lua_close(L);
}

namespace
{
  int record_disable(lua_State *L)
  {
    // Append the language name to the jot_ts_disabled global so tests can assert
    // that startup did not disable any registered language. A global is used
    // because init.lua replaces jot.treesitter with its own module table.
    lua_getglobal(L, "jot_ts_disabled");
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, (int)lua_rawlen(L, -2) + 1);
    return 0;
  }
} // namespace

TEST_CASE("Bundled Lua Tree-sitter startup keeps every registry language enabled")
{
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  lua_newtable(L); // jot
  lua_newtable(L); // jot.treesitter
  lua_newtable(L); // disabled log (global so init.lua cannot clobber it)
  lua_setglobal(L, "jot_ts_disabled");
  native(L, "register_language", ok);
  native(L, "language_for_extension", name);
  native(L, "disable_language", record_disable);
  native(L, "install_command", ok);
  native(L, "status", status);
  native(L, "parser", ok);
  native(L, "parse", ok);
  native(L, "query", ok);
  native(L, "captures", ok);
  native(L, "set_query", ok);
  native(L, "set_capture_color", ok);
  native(L, "reload", ok);
  native(L, "delete_parser", ok);
  native(L, "delete_tree", ok);
  native(L, "delete_query", ok);
  lua_pushstring(L, JOT_LUA_SOURCE_DIR "/treesitter");
  lua_setfield(L, -2, "runtime_path");
  lua_pushcfunction(L, execute);
  lua_setfield(L, -2, "execute");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "native");
  lua_setfield(L, -2, "treesitter");
  lua_setglobal(L, "jot");

  std::string script = "local f,e=loadfile('" JOT_LUA_SOURCE_DIR "/treesitter/init.lua"
                       "'); "
                       "assert(f,e); assert(f()); "
                       "assert(jot.treesitter.load_queries, 'missing load_queries'); "
                       "jot.treesitter.load_queries()";
  int result = luaL_dostring(L, script.c_str());
  INFO(lua_tostring(L, -1));
  REQUIRE(result == LUA_OK);

  // The query load is deferred off the boot path (see init.lua load_queries),
  // so the test invokes it explicitly above. Regression: registry.lua used to
  // set query_file with a "queries/" prefix
  // while queries.lua prepends the queries dir again, so every bundled query
  // file failed to resolve and disable_language() wiped the whole registry at
  // startup - making :tsstatus report nothing installed and installed parsers
  // "not found". No language may be disabled when all query files exist.
  lua_getglobal(L, "jot_ts_disabled");
  int disabled_count = (int)lua_rawlen(L, -1);
  if (disabled_count > 0)
  {
    std::string names;
    for (int i = 1; i <= disabled_count; i++)
    {
      lua_rawgeti(L, -1, i);
      if (!names.empty())
        names += ", ";
      names += lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    INFO("disabled languages: " << names);
  }
  REQUIRE(disabled_count == 0);
  lua_close(L);
}

TEST_CASE("Bundled Lua Tree-sitter module loads in deterministic layers")
{
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  lua_newtable(L); // jot
  lua_newtable(L); // jot.treesitter
  native(L, "register_language", ok);
  native(L, "language_for_extension", name);
  native(L, "disable_language", ok);
  native(L, "install_command", ok);
  native(L, "status", status);
  native(L, "parser", ok);
  native(L, "parse", ok);
  native(L, "query", ok);
  native(L, "captures", ok);
  native(L, "set_query", ok);
  native(L, "set_capture_color", ok);
  native(L, "reload", ok);
  native(L, "delete_parser", ok);
  native(L, "delete_tree", ok);
  native(L, "delete_query", ok);
  lua_pushstring(L, JOT_LUA_SOURCE_DIR "/treesitter");
  lua_setfield(L, -2, "runtime_path");
  lua_pushcfunction(L, execute);
  lua_setfield(L, -2, "execute");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "native");
  lua_setfield(L, -2, "treesitter");
  lua_setglobal(L, "jot");

  std::string script = "local f,e=loadfile('" JOT_LUA_SOURCE_DIR "/treesitter/init.lua"
                       "'); "
                       "assert(f,e); assert(f())";
  int result = luaL_dostring(L, script.c_str());
  INFO(lua_tostring(L, -1));
  REQUIRE(result == LUA_OK);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "treesitter");
  lua_getfield(L, -1, "register_language");
  REQUIRE(lua_isfunction(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, -1, "execute");
  REQUIRE(lua_isfunction(L, -1));
  lua_close(L);
}
