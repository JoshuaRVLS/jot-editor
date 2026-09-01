#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {
int ok(lua_State *L) { lua_pushboolean(L, 1); return 1; }
int name(lua_State *L) { lua_pushstring(L, "cpp"); return 1; }
int status(lua_State *L) { lua_newtable(L); return 1; }
int execute(lua_State *) { return 0; }
void native(lua_State *L, const char *field, lua_CFunction fn) {
  lua_pushcfunction(L, fn); lua_setfield(L, -2, field);
}
}

TEST_CASE("Bundled Lua Tree-sitter module loads in deterministic layers") {
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  lua_newtable(L); // jot
  lua_newtable(L); // jot.treesitter
  native(L, "register_language", ok); native(L, "language_for_extension", name);
  native(L, "status", status); native(L, "parser", ok); native(L, "parse", ok);
  native(L, "query", ok); native(L, "captures", ok); native(L, "set_query", ok);
  native(L, "set_capture_color", ok); native(L, "reload", ok);
  native(L, "delete_parser", ok); native(L, "delete_tree", ok); native(L, "delete_query", ok);
  lua_pushstring(L, JOT_LUA_SOURCE_DIR "/treesitter");
  lua_setfield(L, -2, "runtime_path");
  lua_pushcfunction(L, execute); lua_setfield(L, -2, "execute");
  lua_pushvalue(L, -1); lua_setfield(L, -2, "native");
  lua_setfield(L, -2, "treesitter");
  lua_setglobal(L, "jot");

  std::string script = "local f,e=loadfile('" JOT_LUA_SOURCE_DIR "/treesitter/init.lua" "'); "
                       "assert(f,e); assert(f())";
  int result = luaL_dostring(L, script.c_str());
  INFO(lua_tostring(L, -1));
  REQUIRE(result == LUA_OK);
  lua_getglobal(L, "jot"); lua_getfield(L, -1, "treesitter");
  lua_getfield(L, -1, "register_language"); REQUIRE(lua_isfunction(L, -1)); lua_pop(L, 1);
  lua_getfield(L, -1, "execute"); REQUIRE(lua_isfunction(L, -1));
  lua_close(L);
}
