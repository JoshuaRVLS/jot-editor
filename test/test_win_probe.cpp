// TEMPORARY diagnostic probe for the Windows CI tree-sitter test crashes.
// Prints each stage of the bundled init.lua boot so the CI log shows exactly
// where it breaks. Remove after the root cause is fixed.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  int probe_ok(lua_State *)
  {
    return 1;
  }

  void dump_bytes(const char *label, const std::string &s)
  {
    std::printf("[probe] %s: %zu bytes\n", label, s.size());
    for (unsigned char c : s)
    {
      if (c >= 32 && c < 127)
      {
        std::putchar((int)c);
      }
      else
      {
        std::printf("\\x%02x", (int)c);
      }
    }
    std::printf("\n");
    std::fflush(stdout);
  }
} // namespace

TEST_CASE("Windows CI tree-sitter boot probe")
{
  const char *dir = JOT_LUA_SOURCE_DIR;
  std::printf("[probe] JOT_LUA_SOURCE_DIR=%s\n", dir);
  std::printf("[probe] lua %d.%d (%s)\n", LUA_VERSION_MAJOR, LUA_VERSION_MINOR, LUA_RELEASE);
  std::fflush(stdout);

  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  std::printf("[probe] luaL_openlibs ok\n");
  std::fflush(stdout);

  lua_newtable(L); // jot
  lua_newtable(L); // jot.treesitter
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "register_language");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "language_for_extension");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "disable_language");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "install_command");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "status");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "parser");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "parse");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "query");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "captures");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "set_query");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "set_capture_color");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "reload");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "delete_parser");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "delete_tree");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "delete_query");
  lua_pushstring(L, JOT_LUA_SOURCE_DIR "/treesitter");
  lua_setfield(L, -2, "runtime_path");
  lua_pushcfunction(L, probe_ok);
  lua_setfield(L, -2, "execute");
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "native");
  lua_setfield(L, -2, "treesitter");
  lua_setglobal(L, "jot");
  std::printf("[probe] mock table set up\n");
  std::fflush(stdout);

  // Stage 1: load init.lua directly through the C API (bypasses the script).
  const std::string init_path = std::string(JOT_LUA_SOURCE_DIR) + "/treesitter/init.lua";
  dump_bytes("init path", init_path);
  int rc = luaL_loadfile(L, init_path.c_str());
  std::printf("[probe] luaL_loadfile rc=%d", rc);
  if (rc != LUA_OK)
  {
    std::printf(" err=%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  std::printf("\n");
  std::fflush(stdout);

  // Stage 2: the exact script the real tests run.
  std::string script = "local f,e=loadfile('" JOT_LUA_SOURCE_DIR "/treesitter/init.lua"
                       "'); "
                       "assert(f,e); assert(f())";
  dump_bytes("script", script);
  int rc2 = luaL_dostring(L, script.c_str());
  std::printf("[probe] luaL_dostring rc=%d", rc2);
  if (rc2 != LUA_OK)
  {
    std::printf(" err=%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  std::printf("\n");
  std::fflush(stdout);
  REQUIRE(rc2 == LUA_OK);
  lua_close(L);
  std::printf("[probe] lua_close ok\n");
  std::fflush(stdout);
}