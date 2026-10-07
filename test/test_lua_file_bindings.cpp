// jot.file.read's Lua contract: a missing file arrives as nil, never as the
// "cannot read file" error string. The binding probed the top of the stack,
// which push_file_read fills with that error, so it returned the message as
// the file's text and every caller that only checks for nil skipped its
// fallback -- the LeetCode solution template was never written because of it.
#include "editor.h"
#include "lua/api.h"
#include "lua/bindings_internal.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  // A pristine config home so the Editor constructor's config bootstrap does
  // not read the real user config (which would call into an unhosted Lua
  // state), mirroring the other LuaAPI tests.
  void use_temp_config_home_for_read()
  {
    static bool seeded = false;
    if (seeded)
    {
      return;
    }
    char cfgdir[] = "/tmp/jot_lua_file_read_test_XXXXXX";
    if (!mkdtemp(cfgdir))
    {
      FAIL("could not create a temporary config home");
    }
    setenv("JOT_CONFIG_HOME", cfgdir, 1);
    setenv("JOT_CACHE_HOME", cfgdir, 1);
    seeded = true;
  }

  // Runs `code` with the real jot.file.read binding installed as file_read and
  // returns the error message, or "" on success.
  std::string run_with_read_binding(lua_State *L, LuaAPI &api, const char *code)
  {
    lua_pushlightuserdata(L, &api);
    lua_pushcclosure(L, lua_bind::l_file_read, 1);
    lua_setglobal(L, "file_read");
    if (luaL_loadstring(L, code) != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
      std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
      lua_pop(L, 1);
      return error;
    }
    lua_pop(L, 1);
    return "";
  }
} // namespace

TEST_CASE("jot.file.read returns nil for a missing file", "[lua][file]")
{
  use_temp_config_home_for_read();
  Editor e;
  LuaAPI api(&e);
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);

  const std::string missing = "/tmp/jot_lua_file_read_missing_xyz";
  std::remove(missing.c_str());
  const std::string missing_error =
      run_with_read_binding(L,
                            api,
                            "local value, err = file_read('/tmp/jot_lua_file_read_missing_xyz')\n"
                            "assert(value == nil, 'missing file read as ' .. tostring(value))\n"
                            "assert(err == 'cannot read file', tostring(err))\n"
                            "return 'ok'");
  INFO(missing_error);
  REQUIRE(missing_error.empty());

  // An empty file is content, not a failure: one result, the empty string.
  const std::string empty = "/tmp/jot_lua_file_read_empty_xyz";
  {
    std::ofstream out(empty, std::ios::binary | std::ios::trunc);
    out << "";
  }
  const std::string empty_error =
      run_with_read_binding(L,
                            api,
                            "local value, err = file_read('/tmp/jot_lua_file_read_empty_xyz')\n"
                            "assert(value == '', 'empty file read as ' .. tostring(value))\n"
                            "assert(err == nil, tostring(err))\n"
                            "return 'ok'");
  INFO(empty_error);
  REQUIRE(empty_error.empty());

  // A real file round-trips its bytes through the same path the LeetCode
  // template check uses.
  const std::string present = "/tmp/jot_lua_file_read_present_xyz";
  {
    std::ofstream out(present, std::ios::binary | std::ios::trunc);
    out << "int twoSum() {}";
  }
  const std::string present_error =
      run_with_read_binding(L,
                            api,
                            "local value = file_read('/tmp/jot_lua_file_read_present_xyz')\n"
                            "assert(value == 'int twoSum() {}', tostring(value))\n"
                            "return 'ok'");
  INFO(present_error);
  REQUIRE(present_error.empty());

  lua_close(L);
  std::remove(empty.c_str());
  std::remove(present.c_str());
}
