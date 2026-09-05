// Headless test of the bundled inline-diagnostics feature
// (src/lua/features/decorations.lua). The module is loaded into a raw Lua
// state whose jot.* API is stubbed with recording functions, so no Editor or
// terminal is needed. Pins the VSCode-style wavy-underline contract: one
// decoration per diagnostic with underline=2 + severity underline_hl and NO
// virtual text.
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
  struct DecoSpan
  {
    int row = 0;
    int col = 0;
    int width = 0;
    int underline = 0;
    std::string underline_hl;
    std::string virt_text;
  };

  struct StubState
  {
    int handler_ref = LUA_NOREF; // registered DiagnosticChanged callback
    int clear_count = 0;
    std::vector<DecoSpan> spans;
  };

  StubState g;

  int stub_config_get(lua_State *L)
  {
    // jot.config.get(key, default) -> the default (feature enabled).
    lua_pushvalue(L, 2);
    return 1;
  }

  int stub_autocmd(lua_State *L)
  {
    // jot.autocmd(event, fn): only DiagnosticChanged is used by the module.
    const char *event = luaL_checkstring(L, 1);
    if (std::string(event) == "DiagnosticChanged")
    {
      REQUIRE(lua_isfunction(L, 2));
      lua_pushvalue(L, 2);
      if (g.handler_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, g.handler_ref);
      g.handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
  }

  int stub_diagnostics_get(lua_State *L)
  {
    // jot.diagnostics.get(buffer) -> array of {line,col,end_line,end_col,
    // severity,message} (1-based, same shape as the real bridge).
    lua_newtable(L);
    const struct
    {
      int line, col, end_line, end_col, severity;
      const char *message;
    } rows[] = {
        {2, 3, 2, 22, 1, "Use of undeclared identifier 'undeclared_function'"},
        {3, 7, 3, 8, 2, "Something is iffy here"},
        {4, 1, 4, 1, 4, "Hint with zero width"},
    };
    for (int i = 0; i < 3; i++)
    {
      lua_newtable(L);
      lua_pushinteger(L, rows[i].line);
      lua_setfield(L, -2, "line");
      lua_pushinteger(L, rows[i].col);
      lua_setfield(L, -2, "col");
      lua_pushinteger(L, rows[i].end_line);
      lua_setfield(L, -2, "end_line");
      lua_pushinteger(L, rows[i].end_col);
      lua_setfield(L, -2, "end_col");
      lua_pushinteger(L, rows[i].severity);
      lua_setfield(L, -2, "severity");
      lua_pushstring(L, rows[i].message);
      lua_setfield(L, -2, "message");
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  void record_span(lua_State *L, int opts_index)
  {
    DecoSpan s;
    lua_getfield(L, opts_index, "row");
    s.row = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, opts_index, "col");
    s.col = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, opts_index, "width");
    s.width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, opts_index, "underline");
    s.underline = lua_isnil(L, -1) ? 0 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, opts_index, "underline_hl");
    s.underline_hl = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, opts_index, "virt_text");
    s.virt_text = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    g.spans.push_back(std::move(s));
  }

  int stub_decoration_clear(lua_State *)
  {
    g.clear_count++;
    return 0;
  }

  int stub_decoration_set(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    record_span(L, 2);
    lua_pushinteger(L, (lua_Integer)g.spans.size());
    return 1;
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_newtable(L);
    lua_pushcfunction(L, stub_config_get);
    lua_setfield(L, -2, "get");
    lua_setfield(L, -2, "config"); // jot.config
    lua_pushcfunction(L, stub_autocmd);
    lua_setfield(L, -2, "autocmd"); // jot.autocmd
    lua_newtable(L);
    lua_pushcfunction(L, stub_diagnostics_get);
    lua_setfield(L, -2, "get");
    lua_setfield(L, -2, "diagnostics"); // jot.diagnostics
    lua_newtable(L);
    lua_pushcfunction(L, stub_decoration_clear);
    lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, stub_decoration_set);
    lua_setfield(L, -2, "set");
    lua_setfield(L, -2, "decoration"); // jot.decoration
    lua_setglobal(L, "jot");
  }

  void invoke_handler(lua_State *L)
  {
    REQUIRE(g.handler_ref != LUA_NOREF);
    lua_rawgeti(L, LUA_REGISTRYINDEX, g.handler_ref);
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "buffer");
    REQUIRE(lua_pcall(L, 1, 0, 0) == LUA_OK);
  }
} // namespace

TEST_CASE("Bundled decorations feature applies wavy underlines per diagnostic")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);

  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/decorations.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 0, 0) == LUA_OK);
  REQUIRE(g.handler_ref != LUA_NOREF);

  invoke_handler(L);

  REQUIRE(g.clear_count == 1);
  REQUIRE(g.spans.size() == 2); // zero-width hint is skipped

  const DecoSpan &error = g.spans[0];
  REQUIRE(error.row == 2);
  REQUIRE(error.col == 3);
  REQUIRE(error.width == 19);
  REQUIRE(error.underline == 2); // wavy
  REQUIRE(error.underline_hl == "diagnostic_error");

  const DecoSpan &warning = g.spans[1];
  REQUIRE(warning.row == 3);
  REQUIRE(warning.col == 7);
  REQUIRE(warning.width == 1);
  REQUIRE(warning.underline == 2);
  REQUIRE(warning.underline_hl == "diagnostic_warning");

  // No virtual text anywhere: the feature renders squiggles only.
  for (const DecoSpan &s : g.spans)
  {
    REQUIRE(s.virt_text.empty());
  }

  lua_close(L);
}