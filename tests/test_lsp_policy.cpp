// Lua LSP attach policy (src/lua/lsp/policy.lua): the web extra-server rules
// and the one-shot "web" toolkit preset. Loaded the same way boot does: with a
// minimal jot table whose lsp.installed stub decides presence checks.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  lua_State *make_state()
  {
    lua_State *L = luaL_newstate();
    if (!L)
    {
      return nullptr;
    }
    luaL_openlibs(L);
    // Global `jot` with jot.lsp.installed(id) that reports every server as
    // available (the module guards on the function existing, and extras are
    // additionally gated on project markers in the tests that need them).
    lua_newtable(L); // jot
    lua_newtable(L); // jot.lsp
    lua_pushcfunction(
        L,
        [](lua_State *L2) -> int
        {
          lua_pushboolean(L2, 1);
          return 1;
        });
    lua_setfield(L, -2, "installed");
    lua_setfield(L, -2, "lsp");
    lua_setglobal(L, "jot");

    std::string script = "local f, e = loadfile('" JOT_LUA_SOURCE_DIR
                         "/lsp/policy.lua'); assert(f, e); f()";
    if (luaL_dostring(L, script.c_str()) != LUA_OK)
    {
      const char *err = lua_tostring(L, -1);
      FAIL("policy.lua load failed: " << (err ? err : "?"));
      lua_close(L);
      return nullptr;
    }
    return L;
  }

  // Reads one string field from the table at the stack top.
  std::string field_string(lua_State *L, const char *key)
  {
    lua_getfield(L, -1, key);
    std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return out;
  }
} // namespace

TEST_CASE("Web toolkit preset queues the full LSP + parser set")
{
  lua_State *L = make_state();
  REQUIRE(L != nullptr);
  int result = luaL_dostring(
      L,
      "local tools = jot.lsp.policy.preset('web')\n"
      "local lsp, parsers = {}, {}\n"
      "for _, t in ipairs(tools) do\n"
      "  if t.kind == 'lsp' then lsp[#lsp + 1] = t.name\n"
      "  elseif t.kind == 'parser' then parsers[#parsers + 1] = t.name end\n"
      "end\n"
      "local out = {}\n"
      "out.n = #tools\n"
      "out.lsp = table.concat(lsp, ',')\n"
      "out.parsers = table.concat(parsers, ',')\n"
      "return out\n");
  REQUIRE(result == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  REQUIRE(lua_gettop(L) >= 1);

  lua_getfield(L, -1, "n");
  REQUIRE(lua_tointeger(L, -1) == 10); // 4 LSP + 6 parsers
  lua_pop(L, 1);

  lua_getfield(L, -1, "lsp");
  REQUIRE(std::string(lua_tostring(L, -1)) == "typescript,html,css,json");
  lua_pop(L, 1);

  lua_getfield(L, -1, "parsers");
  REQUIRE(std::string(lua_tostring(L, -1))
          == "javascript,typescript,tsx,html,css,json");
  lua_pop(L, 1);

  // Unknown presets resolve to nothing instead of erroring.
  result = luaL_dostring(L, "return #jot.lsp.policy.preset('nope')");
  REQUIRE(result == LUA_OK);
  REQUIRE(lua_tointeger(L, -1) == 0);
  lua_pop(L, 1);

  lua_close(L);
}

TEST_CASE("Unknown preset returns empty list")
{
  lua_State *L = make_state();
  REQUIRE(L != nullptr);
  int result = luaL_dostring(L, "return jot.lsp.policy.preset('bogus')");
  REQUIRE(result == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  lua_pop(L, 1);
  lua_close(L);
}

TEST_CASE("Policy override hook takes over extra server decisions")
{
  lua_State *L = make_state();
  REQUIRE(L != nullptr);
  int result = luaL_dostring(
      L,
      "jot.lsp.policy.set_override(function(primary, filepath)\n"
      "  if filepath:find('%.tsx$') then\n"
      "    return { { server = 'tailwindcss-language-server', bin = 'x', args = {'--stdio'} } }\n"
      "  end\n"
      "  return {}\n"
      "end)\n"
      "local a = jot.lsp.policy.extra_servers('typescript', '/p/app.tsx')\n"
      "local b = jot.lsp.policy.extra_servers('css', '/p/site.css')\n"
      "local out = {}\n"
      "out.n = #a\n"
      "out.server = a[1] and a[1].server or ''\n"
      "out.arg = a[1] and a[1].args[1] or ''\n"
      "out.other = #b\n"
      "return out\n");
  REQUIRE(result == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  lua_getfield(L, -1, "n");
  REQUIRE(lua_tointeger(L, -1) == 1);
  lua_pop(L, 1);
  lua_getfield(L, -1, "server");
  REQUIRE(std::string(lua_tostring(L, -1)) == "tailwindcss-language-server");
  lua_pop(L, 1);
  lua_getfield(L, -1, "arg");
  REQUIRE(std::string(lua_tostring(L, -1)) == "--stdio");
  lua_pop(L, 1);
  lua_getfield(L, -1, "other");
  REQUIRE(lua_tointeger(L, -1) == 0);
  lua_pop(L, 1);
  lua_close(L);
}

TEST_CASE("Default policy adds nothing outside a web project root")
{
  lua_State *L = make_state();
  REQUIRE(L != nullptr);
  // No package.json / .git anywhere near the caller, so even a tsx file in a
  // tailwind-looking tree returns nothing until a project root appears.
  int result =
      luaL_dostring(L,
                    "return #jot.lsp.policy.extra_servers('typescript', '/nonexistent/x/app.tsx')");
  REQUIRE(result == LUA_OK);
  REQUIRE(lua_tointeger(L, -1) == 0);
  lua_close(L);
}
