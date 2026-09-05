// Headless test of the bundled Lua LSP hover UI (src/lua/features/hover.lua).
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
    int last_border_fg = -1;
    int last_footer_fg = -1;
    int lines_count = 0;
    int set_spans_count = 0;
    int last_spans_line = 0;
    int spans_total = 0; // sum of span lens across all set_spans calls
    int highlight_calls = 0;
    std::string last_highlight_ext;
    std::string last_highlight_text;
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
    // Real binding: jot.ui.float.open(buffer, config) - config at arg #2.
    luaL_checktype(L, 2, LUA_TTABLE);
    g.open_count++;
    lua_getfield(L, 2, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "height");
    g.last_height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "col");
    g.last_col = lua_isnil(L, -1) ? "?" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "row");
    g.last_row = lua_isnil(L, -1) ? "?" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "border");
    g.last_border = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "fg");
    g.last_fg = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "bg");
    g.last_bg = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "border_fg");
    g.last_border_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "footer_fg");
    g.last_footer_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, ++g.next_float);
    return 1;
  }

  int stub_float_close(lua_State *)
  {
    g.close_count++;
    return 0;
  }

  int stub_float_set_spans(lua_State *L)
  {
    // (window, line, spans) - spans is an array of {start, len, fg} tables.
    g.set_spans_count++;
    g.last_spans_line = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 3);
    for (int i = 1; i <= n; i++)
    {
      lua_rawgeti(L, 3, i);
      if (lua_istable(L, -1))
      {
        lua_getfield(L, -1, "len");
        g.spans_total += (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
  }

  int stub_syntax_highlight(lua_State *L)
  {
    // jot.syntax.highlight(ext, text) -> {{start, len, kind}, ...}
    g.highlight_calls++;
    g.last_highlight_ext = luaL_optstring(L, 1, "");
    g.last_highlight_text = luaL_optstring(L, 2, "");
    lua_newtable(L);
    if (g.last_highlight_ext == ".cpp")
    {
      // "int x;" -> keyword span over "int" plus a number-free rest.
      lua_newtable(L);
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "start");
      lua_pushinteger(L, 3);
      lua_setfield(L, -2, "len");
      lua_pushstring(L, "keyword");
      lua_setfield(L, -2, "kind");
      lua_rawseti(L, -2, 1);
    }
    return 1;
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
    lua_pushcfunction(L, stub_float_set_spans);
    lua_setfield(L, -2, "set_spans");
    lua_setfield(L, -2, "float");
    lua_setfield(L, -2, "ui"); // jot.ui
    lua_newtable(L);
    lua_pushcfunction(L, stub_syntax_highlight);
    lua_setfield(L, -2, "highlight");
    lua_setfield(L, -2, "syntax"); // jot.syntax
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
  lua_pushstring(L, "Some **hover** content here\n```cpp\nint x;\n```");
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
  lua_pushinteger(L, 41);
  lua_setfield(L, -2, "border_fg");
  lua_newtable(L); // info.colors
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "keyword");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "string");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "comment");
  lua_setfield(L, -2, "colors");
  {
    const int pcr = lua_pcall(L, 1, 1, 0);
    if (pcr != LUA_OK)
      std::fprintf(stderr, "[hover test] present error: %s\n", lua_tostring(L, -1));
    REQUIRE(pcr == LUA_OK);
  }
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
  // The float opens one row below the anchor line (never on it), so the
  // hovered code row stays fully visible above the frame instead of being
  // cut mid-text by the box.
  REQUIRE(g.last_row == "16");
  REQUIRE((g.last_height >= 3 && g.last_height <= 16));

  // Theme colors: border/footer use their own slots (footer falls back to
  // colors.comment), and the code fence produced syntax spans.
  REQUIRE(g.last_border_fg == 41);
  REQUIRE(g.last_footer_fg == 3); // colors.comment
  REQUIRE(g.highlight_calls == 1);
  REQUIRE(g.last_highlight_ext == ".cpp");
  REQUIRE(g.last_highlight_text == "int x;");
  REQUIRE(g.set_spans_count == 1);
  REQUIRE(g.last_spans_line >= 1);
  REQUIRE(g.spans_total == 3); // "int" highlighted as keyword

  // --- build_display exposes spans; lang_to_ext maps fence tags ---
  push_module_field(L, 1, "lang_to_ext");
  lua_pushstring(L, "C++");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(std::string(lua_tostring(L, -1)) == ".cpp");
  lua_pop(L, 1);
  push_module_field(L, 1, "lang_to_ext");
  lua_pushstring(L, "py");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(std::string(lua_tostring(L, -1)) == ".py");
  lua_pop(L, 1);
  push_module_field(L, 1, "lang_to_ext");
  lua_pushstring(L, "unknownlang");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(std::string(lua_tostring(L, -1)) == "");
  lua_pop(L, 1);

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
