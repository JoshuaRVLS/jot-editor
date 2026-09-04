// Headless test of the bundled Lua LSP hover UI (lua/features/hover.lua).
// The module is loaded into a raw Lua state whose jot.* API is stubbed with
// recording functions, so no Editor / terminal is needed.
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
  struct StubState
  {
    int handler_registered = 0;  // 1 when jot.lsp.hover_ui(fn) fired
    int handler_ref = LUA_NOREF; // captured function
    int next_float = 0;
    int open_count = 0;
    int close_count = 0;
    int delete_count = 0;
    int last_width = 0;
    int last_height = 0;
    std::string last_col;
    std::string last_row;
    std::string last_border;
    int last_fg = -1;
    int last_bg = -1;
    int lines_count = 0;
  };

  StubState g;

  int stub_hover_ui(lua_State *L)
  {
    if (lua_isfunction(L, 1))
    {
      g.handler_registered = 1;
      lua_pushvalue(L, 1);
      if (g.handler_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, g.handler_ref);
      g.handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    else if (g.handler_ref != LUA_NOREF)
    {
      luaL_unref(L, LUA_REGISTRYINDEX, g.handler_ref);
      g.handler_ref = LUA_NOREF;
    }
    return 0;
  }

  int stub_buffer_create(lua_State *L)
  {
    lua_pushinteger(L, 1); // a single fake buffer id
    return 1;
  }

  int stub_buffer_set_lines(lua_State *L)
  {
    // (id, start, end, strict, lines)
    luaL_checktype(L, 5, LUA_TTABLE);
    int n = 0;
    for (;;)
    {
      lua_rawgeti(L, 5, n + 1);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      lua_pop(L, 1);
      ++n;
    }
    g.lines_count = n;
    return 0;
  }

  int stub_buffer_delete(lua_State *)
  {
    g.delete_count++;
    return 0;
  }

  int stub_float_open(lua_State *L)
  {
    // (buffer, enter, config)
    luaL_checktype(L, 3, LUA_TTABLE);
    g.open_count++;
    lua_getfield(L, 3, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "height");
    g.last_height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "col");
    g.last_col = lua_isnil(L, -1) ? "?" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "row");
    g.last_row = lua_isnil(L, -1) ? "?" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "border");
    g.last_border = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "fg");
    g.last_fg = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 3, "bg");
    g.last_bg = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, ++g.next_float);
    return 1;
  }

  int stub_float_close(lua_State *)
  {
    g.close_count++;
    return 0;
  }

  // Pushes jot.lsp.hover_ui onto the stack (caller pops).
  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_pushcfunction(L, stub_hover_ui);
    lua_setfield(L, -2, "hover_ui"); // jot.hover_ui (flat, harmless extra)
    lua_newtable(L);                 // jot.ui
    lua_newtable(L);
    lua_pushcfunction(L, stub_buffer_create);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, stub_buffer_set_lines);
    lua_setfield(L, -2, "set_lines");
    lua_pushcfunction(L, stub_buffer_delete);
    lua_setfield(L, -2, "delete");
    lua_setfield(L, -2, "buffer");
    lua_newtable(L);
    lua_pushcfunction(L, stub_float_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, stub_float_close);
    lua_setfield(L, -2, "close");
    lua_setfield(L, -2, "float");
    lua_setfield(L, -2, "ui"); // jot.ui
    lua_newtable(L);
    lua_pushcfunction(L, stub_hover_ui);
    lua_setfield(L, -2, "hover_ui");
    lua_setfield(L, -2, "lsp"); // jot.lsp
    lua_setglobal(L, "jot");
  }

  // Calls table_at_index.fn(arg); returns number of results pushed after.
  void push_module_field(lua_State *L, int table_index, const char *name)
  {
    lua_getfield(L, table_index, name);
  }

  int table_len(lua_State *L, int idx)
  {
    if (!lua_istable(L, idx))
      return 0;
    lua_len(L, idx);
    int n = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return n;
  }
} // namespace

TEST_CASE("Bundled Lua hover UI renders and dismisses a float")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);

  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/hover.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  // Module return table now sits at stack index 1.
  REQUIRE(lua_istable(L, 1));
  REQUIRE(g.handler_registered == 1);

  // --- Internals: cleaning + wrapping ---
  push_module_field(L, 1, "build_lines");
  lua_pushstring(L, "## Title\n\nReturns the **count**. ```cpp\nint x;\n```\n\nA long paragraph");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  const int logical = table_len(L, -1);
  REQUIRE(logical >= 2);
  bool found_title = false;
  bool found_code_line = false;
  for (int i = 1; i <= logical; ++i)
  {
    lua_rawgeti(L, -1, i);
    const std::string text = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    REQUIRE(text.find("```") == std::string::npos);
    if (text == "Title")
      found_title = true;
    if (text.find("int x;") != std::string::npos)
      found_code_line = true;
  }
  REQUIRE(found_title);
  REQUIRE(found_code_line);
  lua_pop(L, 1);

  // --- present(): opens a float sized to the content ---
  push_module_field(L, 1, "present");
  lua_newtable(L);
  lua_pushstring(L, "Some **hover** content here");
  lua_setfield(L, -2, "contents");
  lua_pushstring(L, "main.cpp");
  lua_setfield(L, -2, "path");
  lua_pushinteger(L, 12);
  lua_setfield(L, -2, "line");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "column");
  lua_pushstring(L, "mouse");
  lua_setfield(L, -2, "kind");
  lua_pushinteger(L, 40);
  lua_setfield(L, -2, "anchor_x");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "anchor_y");
  lua_pushinteger(L, 250);
  lua_setfield(L, -2, "fg");
  lua_pushinteger(L, 237);
  lua_setfield(L, -2, "bg");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);

  REQUIRE(g.open_count == 1);
  REQUIRE(g.close_count == 0);
  REQUIRE(g.lines_count == g.last_height - 2);
  REQUIRE(g.last_width >= 1);
  REQUIRE(g.last_border == "single");
  REQUIRE(g.last_fg == 250);
  REQUIRE(g.last_bg == 237);
  REQUIRE(g.last_col == "40");
  REQUIRE(g.last_row == "15");
  REQUIRE((g.last_height >= 3 && g.last_height <= 16));

  // --- close(): dismisses the float and deletes the buffer ---
  push_module_field(L, 1, "close");
  REQUIRE(lua_pcall(L, 0, 0, 0) == LUA_OK);
  REQUIRE(g.close_count == 1);
  REQUIRE(g.delete_count == 1);

  // --- second present() replaces the previous float ---
  push_module_field(L, 1, "present");
  lua_newtable(L);
  lua_pushstring(L, "second");
  lua_setfield(L, -2, "contents");
  lua_pushinteger(L, 200);
  lua_setfield(L, -2, "fg");
  lua_pushinteger(L, 234);
  lua_setfield(L, -2, "bg");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 2);
  REQUIRE(g.close_count == 1);

  // --- empty contents are not consumed (native fallback shows its message) ---
  push_module_field(L, 1, "present");
  lua_newtable(L);
  lua_pushstring(L, "");
  lua_setfield(L, -2, "contents");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE_FALSE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 2);

  lua_close(L);
}
