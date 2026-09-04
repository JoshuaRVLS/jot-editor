// Headless test of the bundled Lua UI kit (lua/features/ui.lua).
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
    int handler_count = 0;
    std::string handlers[16];
    int open_count = 0;
    int close_count = 0;
    int delete_count = 0;
    int set_spans_count = 0;
    int last_width = 0;
    int last_height = 0;
    int last_fg = -1;
    int last_bg = -1;
    int last_border_fg = -1;
    int last_title_fg = -1;
    int last_footer_fg = -1;
    std::string last_border;
    std::string last_title;
    std::string last_footer;
    int lines_count = 0;
    int spans_total = 0; // sum of span lens across set_spans calls
    int set_cursor_count = 0;
    int last_cursor_x = -1;
    int last_cursor_y = -1;
  };

  StubState g;

  int stub_handler(lua_State *L)
  {
    const char *name = luaL_checkstring(L, 1);
    if (g.handler_count < 16)
    {
      g.handlers[g.handler_count++] = name;
    }
    return 0;
  }

  int stub_buffer_create(lua_State *L)
  {
    lua_pushinteger(L, 1);
    return 1;
  }

  int stub_buffer_set_lines(lua_State *L)
  {
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
    luaL_checktype(L, 2, LUA_TTABLE);
    g.open_count++;
    lua_getfield(L, 2, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "height");
    g.last_height = (int)lua_tointeger(L, -1);
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
    lua_getfield(L, 2, "title_fg");
    g.last_title_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "footer_fg");
    g.last_footer_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "title");
    g.last_title = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "footer");
    g.last_footer = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, 1);
    return 1;
  }

  int stub_float_close(lua_State *)
  {
    g.close_count++;
    return 0;
  }

  int stub_ui_set_cursor(lua_State *L)
  {
    g.set_cursor_count++;
    g.last_cursor_x = (int)luaL_checkinteger(L, 1);
    g.last_cursor_y = (int)luaL_checkinteger(L, 2);
    return 0;
  }

  int stub_float_set_spans(lua_State *L)
  {
    g.set_spans_count++;
    luaL_checktype(L, 3, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 3);
    for (int i = 1; i <= n; i++)
    {
      lua_rawgeti(L, 3, i);
      if (lua_istable(L, -1))
      {
        lua_getfield(L, -1, "len");
        // Count only single-char emphasis spans (match highlighting); full
        // row-background spans are long and not what these asserts measure.
        if ((int)lua_tointeger(L, -1) == 1)
        {
          g.spans_total++;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_newtable(L); // jot.ui
    lua_pushcfunction(L, stub_handler);
    lua_setfield(L, -2, "handler");
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
    lua_pushcfunction(L, stub_ui_set_cursor);
    lua_setfield(L, -2, "set_cursor");
    lua_setfield(L, -2, "ui"); // jot.ui
    lua_setglobal(L, "jot");
  }

  // Pushes a payload table with the standard colors + box onto the stack.
  void push_box(lua_State *L, int x, int y, int w, int h)
  {
    lua_newtable(L);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "h");
    lua_newtable(L); // colors
    lua_pushinteger(L, 250);
    lua_setfield(L, -2, "fg");
    lua_pushinteger(L, 237);
    lua_setfield(L, -2, "bg");
    lua_pushinteger(L, 235);
    lua_setfield(L, -2, "panel_bg");
    lua_pushinteger(L, 240);
    lua_setfield(L, -2, "border");
    lua_pushinteger(L, 16);
    lua_setfield(L, -2, "selection_fg");
    lua_pushinteger(L, 17);
    lua_setfield(L, -2, "selection_bg");
    lua_pushinteger(L, 244);
    lua_setfield(L, -2, "comment");
    lua_pushinteger(L, 215);
    lua_setfield(L, -2, "accent");
    lua_setfield(L, -2, "colors");
  }

  void push_module_field(lua_State *L, int table_index, const char *name)
  {
    lua_getfield(L, table_index, name);
  }
} // namespace

TEST_CASE("Bundled Lua UI kit renders surfaces from Lua")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);

  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));
  REQUIRE(g.handler_count == 11);
  bool has_palette = false, has_quick_pick = false, has_popup = false;
  bool has_save = false, has_quit = false, has_ts = false;
  bool has_lsp = false, has_telescope = false;
  bool has_completion = false, has_context = false, has_menu = false;
  for (int i = 0; i < g.handler_count; i++)
  {
    has_palette = has_palette || g.handlers[i] == "command_palette";
    has_quick_pick = has_quick_pick || g.handlers[i] == "quick_pick";
    has_popup = has_popup || g.handlers[i] == "popup";
    has_save = has_save || g.handlers[i] == "save_prompt";
    has_quit = has_quit || g.handlers[i] == "quit_prompt";
    has_ts = has_ts || g.handlers[i] == "tree_sitter_status";
    has_lsp = has_lsp || g.handlers[i] == "lsp_manager";
    has_telescope = has_telescope || g.handlers[i] == "telescope";
    has_completion = has_completion || g.handlers[i] == "lsp_completion";
    has_context = has_context || g.handlers[i] == "context_menu";
    has_menu = has_menu || g.handlers[i] == "menu_dropdown";
  }
  REQUIRE(has_palette);
  REQUIRE(has_quick_pick);
  REQUIRE(has_popup);
  REQUIRE(has_save);
  REQUIRE(has_quit);
  REQUIRE(has_ts);
  REQUIRE(has_lsp);
  REQUIRE(has_telescope);
  REQUIRE(has_completion);
  REQUIRE(has_context);
  REQUIRE(has_menu);

  // --- command palette ---
  push_module_field(L, 1, "command_palette");
  push_box(L, 20, 5, 60, 12);
  lua_pushstring(L, "open");
  lua_setfield(L, -2, "query");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_newtable(L); // results
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, i == 1 ? "open file" : (i == 2 ? "open buffer" : "close all"));
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "file");
    lua_setfield(L, -2, "category");
    lua_pushstring(L, "detail text");
    lua_setfield(L, -2, "detail");
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, 1);
    lua_rawseti(L, -2, 2);
    lua_pushinteger(L, 2);
    lua_rawseti(L, -2, 3);
    lua_setfield(L, -2, "match");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "results");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);

  REQUIRE(g.open_count == 1);
  REQUIRE(g.last_width == 60);
  REQUIRE(g.last_height == 12);
  REQUIRE(g.last_border == "rounded");
  REQUIRE(g.last_fg == 250);
  REQUIRE(g.last_bg == 235);
  REQUIRE(g.last_border_fg == 240);
  REQUIRE(g.last_title_fg == 215); // accent
  REQUIRE(g.lines_count == 6);     // input + divider + 3 items + pad row
  // match spans: 3 items x 3 matched chars each
  REQUIRE(g.set_spans_count >= 1);
  REQUIRE(g.spans_total == 9);

  // --- palette re-present replaces the float; nil closes it ---
  push_module_field(L, 1, "command_palette");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.close_count == 1);
  REQUIRE(g.delete_count == 1);

  // --- quick pick ---
  push_module_field(L, 1, "quick_pick");
  push_box(L, 10, 4, 80, 14);
  lua_pushstring(L, "Files");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "mai");
  lua_setfield(L, -2, "query");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "all_count");
  lua_newtable(L); // items
  lua_newtable(L);
  lua_pushstring(L, "main.cpp");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "src");
  lua_setfield(L, -2, "detail");
  lua_pushstring(L, "preview text");
  lua_setfield(L, -2, "preview");
  lua_rawseti(L, -2, 1);
  lua_newtable(L);
  lua_pushstring(L, "other.txt");
  lua_setfield(L, -2, "label");
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 2);
  REQUIRE(g.lines_count >= 3);
  REQUIRE(g.last_title.find("Files") != std::string::npos);
  REQUIRE(g.last_title.find("2/2") != std::string::npos);
  REQUIRE(g.last_footer == "preview text");

  push_module_field(L, 1, "quick_pick");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- popup with scrolling ---
  push_module_field(L, 1, "popup");
  push_box(L, 30, 8, 50, 8);
  lua_pushstring(L, "Help");
  lua_setfield(L, -2, "title");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "scroll");
  lua_newtable(L);
  for (int i = 1; i <= 10; i++)
  {
    lua_pushstring(L, ("line " + std::to_string(i)).c_str());
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "lines");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 3);
  REQUIRE(g.lines_count == 6);        // h-2 rows windowed from scroll=2
  REQUIRE(g.last_footer == "3-8/10"); // scroll+1 .. scroll+inner_h
  REQUIRE(g.last_title.find("Help") != std::string::npos);

  push_module_field(L, 1, "popup");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- prompts ---
  push_module_field(L, 1, "save_prompt");
  push_box(L, 30, 10, 60, 4);
  lua_pushstring(L, "myfile.cpp");
  lua_setfield(L, -2, "input");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 4);
  REQUIRE(g.lines_count == 2);

  push_module_field(L, 1, "save_prompt");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  push_module_field(L, 1, "quit_prompt");
  push_box(L, 30, 10, 40, 3);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 5);
  REQUIRE(g.lines_count == 1);

  push_module_field(L, 1, "quit_prompt");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.close_count == 5);

  // --- tree-sitter status modal ---
  push_module_field(L, 1, "tree_sitter_status");
  push_box(L, 30, 4, 60, 16);
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "scroll");
  lua_newtable(L); // rows
  lua_newtable(L);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "section");
  lua_pushstring(L, "Active");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "1");
  lua_setfield(L, -2, "detail");
  lua_rawseti(L, -2, 1);
  lua_newtable(L);
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "section");
  lua_pushstring(L, "cpp");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "parser loaded");
  lua_setfield(L, -2, "detail");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "color");
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -2, "rows");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 6);
  REQUIRE(g.last_title.find("Tree-sitter") != std::string::npos);
  REQUIRE(g.lines_count == 14); // h-2 padded rows
  REQUIRE(g.last_footer.find("Up/Down scroll") != std::string::npos);

  push_module_field(L, 1, "tree_sitter_status");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- LSP manager modal ---
  push_module_field(L, 1, "lsp_manager");
  push_box(L, 20, 5, 70, 12);
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "scroll");
  lua_pushinteger(L, 20);
  lua_setfield(L, -2, "label_w");
  lua_pushinteger(L, 32);
  lua_setfield(L, -2, "state_x");
  lua_pushinteger(L, 52);
  lua_setfield(L, -2, "action_x");
  lua_newtable(L); // rows
  lua_newtable(L);
  lua_pushstring(L, "clangd");
  lua_setfield(L, -2, "server");
  lua_pushstring(L, "cpp");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "ready");
  lua_setfield(L, -2, "state");
  lua_pushinteger(L, 244);
  lua_setfield(L, -2, "state_color");
  lua_newtable(L); // actions
  lua_newtable(L);
  lua_pushstring(L, "update");
  lua_setfield(L, -2, "action");
  lua_pushstring(L, "Update");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "primary");
  lua_setfield(L, -2, "variant");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "enabled");
  lua_pushinteger(L, 52);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, 6);
  lua_setfield(L, -2, "y");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "w");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "actions");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "rows");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 7);
  REQUIRE(g.last_title.find("LSP Manager") != std::string::npos);
  REQUIRE(g.last_footer == "1 servers");
  REQUIRE(g.lines_count == 10);
  REQUIRE(g.set_spans_count > 0); // bg + colored segments

  push_module_field(L, 1, "lsp_manager");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- telescope ---
  push_module_field(L, 1, "telescope");
  push_box(L, 0, 1, 60, 18);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "inner_x");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "inner_y");
  lua_pushinteger(L, 58);
  lua_setfield(L, -2, "inner_w");
  lua_pushinteger(L, 16);
  lua_setfield(L, -2, "inner_h");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "query_x");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "query_y");
  lua_pushinteger(L, 56);
  lua_setfield(L, -2, "query_w");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "body_y");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "body_h");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "list_x");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "list_y");
  lua_pushinteger(L, 56);
  lua_setfield(L, -2, "list_w");
  lua_pushinteger(L, 12);
  lua_setfield(L, -2, "list_h");
  lua_pushinteger(L, 30);
  lua_setfield(L, -2, "preview_x");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "preview_y");
  lua_pushinteger(L, 28);
  lua_setfield(L, -2, "preview_w");
  lua_pushinteger(L, 12);
  lua_setfield(L, -2, "preview_h");
  lua_pushinteger(L, 17);
  lua_setfield(L, -2, "footer_y");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "show_preview");
  lua_pushstring(L, "mai");
  lua_setfield(L, -2, "query");
  lua_pushstring(L, "/home/user");
  lua_setfield(L, -2, "root");
  lua_pushstring(L, " Find Files ");
  lua_setfield(L, -2, "title");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "list_scroll");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "result_count");
  lua_pushstring(L, "query");
  lua_setfield(L, -2, "focus");
  lua_newtable(L); // results
  lua_newtable(L);
  lua_pushstring(L, "main.cpp");
  lua_setfield(L, -2, "name");
  lua_pushstring(L, "src");
  lua_setfield(L, -2, "parent_path");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "is_directory");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "results");
  lua_newtable(L); // preview
  lua_pushstring(L, "main.cpp");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "12 KB");
  lua_setfield(L, -2, "detail");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "start_line");
  lua_pushstring(L, ".cpp");
  lua_setfield(L, -2, "extension");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "is_directory");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "skipped");
  lua_newtable(L); // lines
  lua_pushstring(L, "int main() { return 0; }");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "lines");
  lua_setfield(L, -2, "preview");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 8);
  REQUIRE(g.lines_count == 16);     // h-2 body rows
  REQUIRE(g.set_cursor_count == 1); // query focus caret
  REQUIRE(g.last_cursor_y == 2);
  REQUIRE(g.last_cursor_x >= 1);

  push_module_field(L, 1, "telescope");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- lsp completion ---
  push_module_field(L, 1, "lsp_completion");
  push_box(L, 30, 8, 44, 4); // content box: 3 items + footer
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "max_items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "start");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 6);
  lua_setfield(L, -2, "total");
  lua_pushinteger(L, 9);
  lua_setfield(L, -2, "all_total");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "filtered");
  lua_pushstring(L, "pri");
  lua_setfield(L, -2, "prefix");
  lua_newtable(L); // items
  const char *c_labels[] = {"printf", "printf_s", "print_buffer"};
  const char *c_kinds[] = {"Function", "Function", "Function"};
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, c_labels[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushinteger(L, 12);
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, c_kinds[i - 1]);
    lua_setfield(L, -2, "kind_name");
    lua_pushstring(L, "󰊕");
    lua_setfield(L, -2, "kind_icon");
    lua_pushstring(L, "prints to stdout");
    lua_setfield(L, -2, "detail");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "documentation");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  // float wraps the content box in a 1-cell border
  REQUIRE(g.last_width == 46);
  REQUIRE(g.last_height == 6);
  REQUIRE(g.last_border == "single");
  REQUIRE(g.lines_count == 4); // 3 items + footer
  push_module_field(L, 1, "lsp_completion");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- context menu ---
  push_module_field(L, 1, "context_menu");
  push_box(L, 12, 6, 26, 6);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "selected");
  lua_newtable(L); // items
  const char *x_labels[] = {"Cut", "Copy", "Paste", "Go to Definition"};
  const int x_enabled[] = {1, 1, 1, 1};
  for (int i = 1; i <= 4; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, x_labels[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushboolean(L, x_enabled[i - 1]);
    lua_setfield(L, -2, "enabled");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 26);
  REQUIRE(g.last_height == 6);
  REQUIRE(g.lines_count == 4); // item rows
  push_module_field(L, 1, "context_menu");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- menu dropdown ---
  push_module_field(L, 1, "menu_dropdown");
  push_box(L, 4, 1, 30, 7);
  lua_pushstring(L, "File");
  lua_setfield(L, -2, "menu_label");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "selected");
  lua_newtable(L); // items
  const char *m_labels[] = {"New File", "Open File...", "Save", "Save As...", "Quit"};
  for (int i = 1; i <= 5; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, m_labels[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "enabled");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 30);
  REQUIRE(g.last_title == " File ");
  REQUIRE(g.lines_count == 5); // item rows
  push_module_field(L, 1, "menu_dropdown");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- match_spans helper ---
  push_module_field(L, 1, "match_spans");
  lua_pushstring(L, "hello world");
  lua_pushstring(L, "lo");
  lua_pushinteger(L, 7);
  REQUIRE(lua_pcall(L, 3, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, -1));
  REQUIRE(lua_rawlen(L, -1) == 2);
  lua_pop(L, 1);
  push_module_field(L, 1, "match_spans");
  lua_pushstring(L, "hello");
  lua_pushstring(L, "zz");
  lua_pushinteger(L, 7);
  REQUIRE(lua_pcall(L, 3, 1, 0) == LUA_OK);
  REQUIRE(lua_rawlen(L, -1) == 0);
  lua_pop(L, 1);

  lua_close(L);
}