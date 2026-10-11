// Headless test of the bundled Lua UI kit (runtime/lua/features/ui.lua).
// The module is loaded into a raw Lua state whose jot.* API is stubbed with
// recording functions, so no Editor / terminal is needed.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "lua/api/api_internal.h"
#include "lua/embedded_lua.h"
#include "ui/text.h"

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
    std::string handlers[24];
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
    std::vector<std::string> lines; // every body row as rendered
    std::string last_row1; // first body row as rendered (status line text)
    int spans_total = 0; // sum of span lens across set_spans calls
    // Every fg passed through set_spans in order (used to verify that a
    // row's per-language icon glyph is colored with its own brand color).
    std::vector<int> spans_fg;
    // Full geometry of the most recent set_spans call, so a test can assert
    // where a part landed (the completion popup right-aligns its type column).
    struct RecordedSpan
    {
      int start;
      int len;
      int fg;
    };
    std::vector<RecordedSpan> last_spans;
    // Spans keyed by the float line they were set for, which is what a
    // multi-row surface (the completion popup) needs to be asserted row by row.
    std::map<int, std::vector<RecordedSpan>> spans_by_line;
    int set_cursor_count = 0;
    int last_cursor_x = -1;
    int last_cursor_y = -1;
    // Every float open, in call order, so a multi-float surface (the command
    // palette's grid + explanation box) can assert each frame separately.
    struct RecordedOpen
    {
      int x = 0;
      int y = 0;
      int w = 0;
      int h = 0;
      std::string border;
      int fg = -1;
      int bg = -1;
      int border_fg = -1;
      bool strip = false;
    };
    std::vector<RecordedOpen> opens;
    // Every buffer.set_lines body, in call order: the same reason -- two
    // floats mean two bodies and only the last one lands in `lines`.
    std::vector<std::vector<std::string>> line_sets;
  };

  StubState g;

  int stub_handler(lua_State *L)
  {
    const char *name = luaL_checkstring(L, 1);
    if (g.handler_count < 24)
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
    g.lines.clear();
    g.lines.reserve((size_t)n);
    for (int i = 1; i <= n; i++)
    {
      lua_rawgeti(L, 5, i);
      if (!lua_isnil(L, -1) && lua_isstring(L, -1))
      {
        g.lines.push_back(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
    lua_rawgeti(L, 5, 1);
    if (!lua_isnil(L, -1) && lua_isstring(L, -1))
    {
      g.last_row1 = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    g.line_sets.push_back(g.lines);
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
    g.opens.push_back({});
    StubState::RecordedOpen &opened = g.opens.back();
    lua_getfield(L, 2, "col");
    opened.x = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "row");
    opened.y = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    opened.w = g.last_width;
    lua_pop(L, 1);
    lua_getfield(L, 2, "height");
    g.last_height = (int)lua_tointeger(L, -1);
    opened.h = g.last_height;
    lua_pop(L, 1);
    lua_getfield(L, 2, "border");
    g.last_border = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    opened.border = g.last_border;
    lua_pop(L, 1);
    lua_getfield(L, 2, "fg");
    g.last_fg = (int)lua_tointeger(L, -1);
    opened.fg = g.last_fg;
    lua_pop(L, 1);
    lua_getfield(L, 2, "bg");
    g.last_bg = (int)lua_tointeger(L, -1);
    opened.bg = g.last_bg;
    lua_pop(L, 1);
    lua_getfield(L, 2, "border_fg");
    g.last_border_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    opened.border_fg = g.last_border_fg;
    lua_pop(L, 1);
    lua_getfield(L, 2, "strip");
    opened.strip = lua_toboolean(L, -1) != 0;
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

  int stub_float_is_valid(lua_State *L)
  {
    // Surfaces that keep their float across emits (the home screen) ask before
    // reusing the handle they cached: valid from open() until close().
    lua_pushboolean(L, g.open_count > g.close_count);
    return 1;
  }

  int stub_float_configure(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "height");
    g.last_height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "border");
    g.last_border = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    return 1;
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
    g.last_spans.clear();
    const int line_index = (int)luaL_checkinteger(L, 2);
    g.spans_by_line[line_index].clear();
    for (int i = 1; i <= n; i++)
    {
      g.last_spans.push_back({0, 0, -1});
      g.spans_by_line[line_index].push_back({0, 0, -1});
      StubState::RecordedSpan &line_rec = g.spans_by_line[line_index].back();
      lua_rawgeti(L, 3, i);
      if (lua_istable(L, -1))
      {
        StubState::RecordedSpan &rec = g.last_spans.back();
        StubState::RecordedSpan &line_rec = g.spans_by_line[line_index].back();
        lua_getfield(L, -1, "len");
        if (lua_isnumber(L, -1))
        {
          rec.len = (int)lua_tointeger(L, -1);
          line_rec.len = rec.len;
          // Count only single-char emphasis spans (match highlighting); full
          // row-background spans are long and not what these asserts measure.
          if (rec.len == 1)
          {
            g.spans_total++;
          }
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "start");
        if (lua_isnumber(L, -1))
        {
          rec.start = (int)lua_tointeger(L, -1);
          line_rec.start = rec.start;
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "fg");
        if (lua_isnumber(L, -1))
        {
          rec.fg = (int)lua_tointeger(L, -1);
          line_rec.fg = rec.fg;
          g.spans_fg.push_back(rec.fg);
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
    lua_pushcfunction(L, stub_float_is_valid);
    lua_setfield(L, -2, "is_valid");
    lua_pushcfunction(L, stub_float_configure);
    lua_setfield(L, -2, "configure");
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
    lua_pushinteger(L, 81);
    lua_setfield(L, -2, "type");
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

  // The kit is split into per-surface modules: pre-load them the same way the
  // app boot does (helpers first), then run the orchestrator ui.lua.
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));  REQUIRE( g.handler_count == 22 );
  bool has_palette = false, has_quick_pick = false, has_popup = false;
  bool has_save = false, has_quit = false, has_ts = false;
  bool has_lsp = false, has_lsp_status = false, has_telescope = false;
  bool has_completion = false, has_signature = false, has_context = false, has_menu = false;
  bool has_search = false, has_home = false, has_status = false, has_sidebar = false;
  bool has_side_panel = false, has_winbar = false, has_winbar_menu = false;
  for (int i = 0; i < g.handler_count; i++)
  {
    has_palette = has_palette || g.handlers[i] == "command_palette";
    has_quick_pick = has_quick_pick || g.handlers[i] == "quick_pick";
    has_popup = has_popup || g.handlers[i] == "popup";
    has_save = has_save || g.handlers[i] == "save_prompt";
    has_quit = has_quit || g.handlers[i] == "quit_prompt";
    has_ts = has_ts || g.handlers[i] == "tree_sitter_status";
    has_lsp = has_lsp || g.handlers[i] == "lsp_manager";
    has_lsp_status = has_lsp_status || g.handlers[i] == "lsp_status";
    has_telescope = has_telescope || g.handlers[i] == "telescope";
    has_completion = has_completion || g.handlers[i] == "lsp_completion";
    has_signature = has_signature || g.handlers[i] == "lsp_signature";
    has_context = has_context || g.handlers[i] == "context_menu";
    has_menu = has_menu || g.handlers[i] == "menu_dropdown";
    has_search = has_search || g.handlers[i] == "search_panel";
    has_home = has_home || g.handlers[i] == "home_screen";
    has_status = has_status || g.handlers[i] == "status_line";
    has_sidebar = has_sidebar || g.handlers[i] == "sidebar";
    has_side_panel = has_side_panel || g.handlers[i] == "side_panel";
    has_winbar = has_winbar || g.handlers[i] == "winbar";
    has_winbar_menu = has_winbar_menu || g.handlers[i] == "winbar_menu";
  }
  REQUIRE(has_palette);
  REQUIRE(has_quick_pick);
  REQUIRE(has_popup);
  REQUIRE(has_save);
  REQUIRE(has_quit);
  REQUIRE(has_ts);
  REQUIRE(has_lsp);
  REQUIRE(has_lsp_status);
  REQUIRE(has_telescope);
  REQUIRE(has_completion);
  REQUIRE(has_signature);
  REQUIRE(has_context);
  REQUIRE(has_menu);
  REQUIRE(has_search);
  REQUIRE(has_home);
  REQUIRE(has_status);
  REQUIRE(has_sidebar);
  REQUIRE(has_side_panel);
  REQUIRE(has_winbar);
  REQUIRE(has_winbar_menu);

  // --- command palette: Helix-style grid + explanation box ---
  push_module_field(L, 1, "command_palette");
  // The payload carries the whole geometry now: a 60-cell screen split into
  // two 30-cell columns, 5 results over three rows (filled column by column),
  // the explanation box for the selected row above the grid, and the prompt on
  // the box's last row.
  push_box(L, 0, 20, 60, 7);
  lua_pushstring(L, "open");
  lua_setfield(L, -2, "query");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "grid_x");
  lua_pushinteger(L, 23);
  lua_setfield(L, -2, "grid_y");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "grid_cols");
  lua_pushinteger(L, 30);
  lua_setfield(L, -2, "grid_col_w");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "grid_rows");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "grid_first_row");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "rows_per_col");
  lua_pushinteger(L, 26);
  lua_setfield(L, -2, "prompt_y");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "help_x");
  lua_pushinteger(L, 19);
  lua_setfield(L, -2, "help_y");
  lua_pushinteger(L, 40);
  lua_setfield(L, -2, "help_w");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "help_h");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "help_for");
  lua_pushinteger(L, 250);
  lua_setfield(L, -2, "help_fg");
  lua_pushinteger(L, 236);
  lua_setfield(L, -2, "help_bg");
  lua_pushinteger(L, 240);
  lua_setfield(L, -2, "help_border_fg");
  lua_newtable(L); // help_lines
  lua_pushstring(L, "Open a file or folder; a new path creates the file first.");
  lua_rawseti(L, -2, 1);
  lua_pushstring(L, "Aliases: edit, open");
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -2, "help_lines");
  lua_newtable(L); // results
  const char *palette_labels[] = {"open", "open buffer", "close all", "write", "quit"};
  for (int i = 1; i <= 5; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, palette_labels[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "file");
    lua_setfield(L, -2, "category");
    lua_pushstring(L, "detail text");
    lua_setfield(L, -2, "detail");
    lua_newtable(L);
    if (i == 1)
    {
      // Two matched cells on the first result only, so the emphasis count is
      // unambiguous.
      lua_pushinteger(L, 0);
      lua_rawseti(L, -2, 1);
      lua_pushinteger(L, 1);
      lua_rawseti(L, -2, 2);
    }
    lua_setfield(L, -2, "match");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "results");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);

  // Two floats: the grid (borderless, sitting on the statusline slot) and the
  // explanation box (bordered, standing on the grid's top edge).
  REQUIRE(g.open_count == 2);
  REQUIRE(g.opens.size() == 2);
  REQUIRE(g.opens[0].border == "none");
  REQUIRE(g.opens[0].x == 0);
  REQUIRE(g.opens[0].y == 23);
  REQUIRE(g.opens[0].w == 60);
  REQUIRE(g.opens[0].h == 4); // three grid rows + the prompt
  REQUIRE(g.opens[0].strip);
  REQUIRE(g.opens[1].border == "single");
  REQUIRE(g.opens[1].x == 0);
  REQUIRE(g.opens[1].y == 19);
  REQUIRE(g.opens[1].w == 40);
  REQUIRE(g.opens[1].h == 4);
  REQUIRE(g.opens[1].fg == 250);
  REQUIRE(g.opens[1].bg == 236);
  REQUIRE(g.opens[1].border_fg == 240);

  // The grid body fills column by column (rows_per_col = 3): row 0 holds
  // results 1 and 4 ("open" and "write"), row 1 holds 2 and 5, row 2 holds 3.
  REQUIRE(g.line_sets.size() == 2);
  REQUIRE(g.line_sets[0].size() == 4); // 3 grid rows + prompt
  REQUIRE(g.line_sets[0][0].rfind(" open", 0) == 0);
  REQUIRE(g.line_sets[0][0].rfind(" write", 30) == 30);
  REQUIRE(g.line_sets[0][1].rfind(" open buffer", 0) == 0);
  REQUIRE(g.line_sets[0][1].rfind(" quit", 30) == 30);
  REQUIRE(g.line_sets[0][2].rfind(" close all", 0) == 0);
  REQUIRE(g.line_sets[0][3].rfind(":open", 0) == 0); // the prompt is last
  // The explanation box's own body: the sentence and the aliases line.
  REQUIRE(g.line_sets[1].size() == 2);
  REQUIRE(g.line_sets[1][0].rfind("Open a file or folder", 0) == 0);
  REQUIRE(g.line_sets[1][1].rfind("Aliases: edit, open", 0) == 0);
  // Match emphasis: only the first cell's two matched characters.
  REQUIRE(g.set_spans_count >= 1);
  REQUIRE(g.spans_total == 2);

  // --- palette re-present replaces both floats; nil closes them ---
  push_module_field(L, 1, "command_palette");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.close_count == 2);
  REQUIRE(g.delete_count == 2);

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
  lua_pushinteger(L, 173); // the row's own ink
  lua_setfield(L, -2, "fg");
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 3);
  REQUIRE(g.lines_count >= 3);
  REQUIRE(g.last_title.find("Files") != std::string::npos);
  REQUIRE(g.last_title.find("2/2") != std::string::npos);
  REQUIRE(g.last_footer == "preview text");

  // Labels sit at the panel's inner edge under a two-space prefix, with no
  // accent bar pushing the selected row right, and a row that names its own
  // ink keeps it while unselected. The selected row stays on the selection
  // pair (16 fg on 17 bg here).
  REQUIRE(g.lines.size() >= 4); // query + divider + two items
  REQUIRE(g.lines[2].rfind("  main.cpp", 0) == 0);
  REQUIRE(g.lines[3].rfind("  other.txt", 0) == 0);
  REQUIRE(g.lines[2].find("▎") == std::string::npos);
  REQUIRE(g.lines[3].find("▎") == std::string::npos);
  REQUIRE(g.spans_by_line[3].front().fg == 16);  // selection fg
  REQUIRE(g.spans_by_line[4].front().fg == 173); // the row's own ink

  push_module_field(L, 1, "quick_pick");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- settings menu ---
  push_module_field(L, 1, "settings");
  push_box(L, 20, 6, 96, 18);
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "scroll");
  lua_pushinteger(L, 34);
  lua_setfield(L, -2, "all_count");
  lua_newtable(L); // items (visible window)
  lua_newtable(L); // row 1: a section header
  lua_pushboolean(L, true);
  lua_setfield(L, -2, "header");
  lua_pushstring(L, "Appearance");
  lua_setfield(L, -2, "label");
  lua_rawseti(L, -2, 1);
  lua_newtable(L); // row 2: bool, plain
  lua_pushstring(L, "Auto save");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "on");
  lua_setfield(L, -2, "value");
  lua_pushstring(L, "bool");
  lua_setfield(L, -2, "type");
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "selected");
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "editing");
  lua_rawseti(L, -2, 2);
  lua_newtable(L); // row 3: int, selected + editing
  lua_pushstring(L, "Tab size");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "4");
  lua_setfield(L, -2, "value");
  lua_pushstring(L, "int");
  lua_setfield(L, -2, "type");
  lua_pushboolean(L, true);
  lua_setfield(L, -2, "selected");
  lua_pushboolean(L, true);
  lua_setfield(L, -2, "editing");
  lua_pushstring(L, "8");
  lua_setfield(L, -2, "edit_input");
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 4);
  REQUIRE(g.last_width == 96);
  REQUIRE(g.last_height == 18);
  REQUIRE(g.last_border != "none"); // modal panel: bordered float
  REQUIRE(g.last_title.find("Settings") != std::string::npos);
  REQUIRE(g.last_title.find("34 keys") != std::string::npos);
  // The footer is gone: the surface shows its state, not a list of bindings.
  REQUIRE(g.last_footer.empty());
  // Search bar, divider, then the rows: a section header, the selected bool
  // showing "on", and the editing row showing its " > 8" input prompt.
  REQUIRE(g.lines_count >= 5);
  REQUIRE(g.last_row1.find("Search") != std::string::npos);
  REQUIRE(g.lines[1].find("─") != std::string::npos);
  // A header is a row of its own: the group's name on the column the labels
  // start at, with no value or affordances after it.
  REQUIRE(g.lines[2].find("Appearance") != std::string::npos);
  REQUIRE(g.lines[2].find("4") == std::string::npos);
  // And it carries the panel's quiet ink rather than the row ink a setting
  // reads in -- the ink is the whole difference between a heading and a row,
  // and all the header flag changes when the row is drawn. The first span of
  // a line is present_panel's full-line fill, which carries the row's ink; in
  // the stub's palette that is comment 244 for the heading, fg 250 for a row.
  int header_line = -1;
  int setting_line = -1;
  for (int i = 0; i < g.lines_count; i++)
  {
    if (g.lines[i].find("Appearance") != std::string::npos)
      header_line = i + 1;
    if (g.lines[i].find("Auto save") != std::string::npos)
      setting_line = i + 1;
  }
  REQUIRE(header_line > 0);
  REQUIRE(setting_line > 0);
  REQUIRE_FALSE(g.spans_by_line[header_line].empty());
  REQUIRE_FALSE(g.spans_by_line[setting_line].empty());
  REQUIRE(g.spans_by_line[header_line].front().fg == 244);
  REQUIRE(g.spans_by_line[setting_line].front().fg == 250);
  bool saw_edit_input = false;
  bool edit_span_on_byte = false;
  for (int i = 0; i < g.lines_count; i++)
  {
    if (g.lines[i].find("> 8") != std::string::npos)
    {
      saw_edit_input = true;
      // Span offsets are bytes into the rendered line (the float renderer maps
      // them to cells). The selected row starts with a three-byte chevron, so
      // an offset advanced by cells lands two bytes short and the renderer
      // paints the input's tail in the float's default ink.
      const size_t at = g.lines[i].find("> 8");
      for (const auto &span : g.spans_by_line[i + 1])
      {
        if (span.start == (int)at && span.len == 3)
        {
          edit_span_on_byte = true;
        }
      }
    }
  }
  REQUIRE(saw_edit_input);
  REQUIRE(edit_span_on_byte);

  push_module_field(L, 1, "settings");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.close_count >= 2); // quick pick + settings closed

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
  REQUIRE(g.open_count == 5);
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
  REQUIRE(g.open_count == 6);
  // One row: the filename input. "Save As" moved to the panel title, and the
  // line that spelled out Enter/Esc is gone. The box is taller than the content
  // because the native layout reserves the height.
  REQUIRE(g.lines_count == 1);
  REQUIRE(g.last_title.find("Save As") != std::string::npos);
  REQUIRE(g.last_footer.empty());

  push_module_field(L, 1, "save_prompt");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // The rename prompt: same one-field shape, seeded with the identifier under
  // the cursor by the native side and titled so it does not need an
  // instruction line.
  push_module_field(L, 1, "rename_prompt");
  push_box(L, 30, 10, 60, 4);
  lua_pushstring(L, "counter_value");
  lua_setfield(L, -2, "input");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.lines_count == 1);
  REQUIRE(g.last_title.find("Rename Symbol") != std::string::npos);
  REQUIRE(g.last_footer.empty());
  bool saw_name_input = false;
  for (int i = 0; i < g.lines_count; i++)
  {
    if (g.lines[i].find("counter_value") != std::string::npos)
    {
      saw_name_input = true;
    }
  }
  REQUIRE(saw_name_input);

  push_module_field(L, 1, "rename_prompt");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  push_module_field(L, 1, "quit_prompt");
  push_box(L, 30, 10, 40, 3);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == 8);
  REQUIRE(g.lines_count == 1);
  // The panel has no title, so the row has to carry the keys: "quit anyway?"
  // alone leaves the answer unguessable (and the native painter, which is the
  // other rendering of this same prompt, spells them out).
  REQUIRE(g.lines[0].find("(y/n)") != std::string::npos);

  push_module_field(L, 1, "quit_prompt");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.close_count == 8);

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
  REQUIRE(g.open_count == 9);
  REQUIRE(g.last_title.find("Tree-sitter") != std::string::npos);
  REQUIRE(g.lines_count == 14); // h-2 padded rows
  // No key hints; this list fits without scrolling, so there is no counter
  // footer either.
  REQUIRE(g.last_footer.empty());

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
  REQUIRE(g.open_count == 10);
  REQUIRE(g.last_title.find("LSP Manager") != std::string::npos);
  REQUIRE(g.last_footer == "1 servers");
  REQUIRE(g.lines_count == 10);
  REQUIRE(g.set_spans_count > 0); // bg + colored segments

  push_module_field(L, 1, "lsp_manager");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- telescope ---
  // The two-box layout telescope_layout_for() produces for a 120x32 screen: the
  // float is the whole region (both boxes plus the column between them), w is
  // the list box and preview_x/... the file view.
  push_module_field(L, 1, "telescope");
  push_box(L, 0, 1, 58, 18);
  lua_pushinteger(L, 88);
  lua_setfield(L, -2, "region_w");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "inner_x");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "inner_y");
  lua_pushinteger(L, 56);
  lua_setfield(L, -2, "inner_w");
  lua_pushinteger(L, 16);
  lua_setfield(L, -2, "inner_h");
  // The prompt is the box's last interior row (inner_y + inner_h - 1) and the
  // results fill the box above it, which is the relationship the native layout
  // hands over.
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "query_x");
  lua_pushinteger(L, 17);
  lua_setfield(L, -2, "query_y");
  lua_pushinteger(L, 54);
  lua_setfield(L, -2, "query_w");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "body_y");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "body_h");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "list_x");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "list_y");
  lua_pushinteger(L, 56);
  lua_setfield(L, -2, "list_w");
  lua_pushinteger(L, 15);
  lua_setfield(L, -2, "list_h");
  lua_pushinteger(L, 59);
  lua_setfield(L, -2, "preview_x");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "preview_y");
  lua_pushinteger(L, 29);
  lua_setfield(L, -2, "preview_w");
  lua_pushinteger(L, 18);
  lua_setfield(L, -2, "preview_h");
  lua_pushinteger(L, 60);
  lua_setfield(L, -2, "preview_inner_x");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "preview_inner_y");
  lua_pushinteger(L, 27);
  lua_setfield(L, -2, "preview_inner_w");
  lua_pushinteger(L, 16);
  lua_setfield(L, -2, "preview_inner_h");
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "preview_text_y");
  lua_pushinteger(L, 18);
  lua_setfield(L, -2, "preview_status_y");
  lua_pushinteger(L, 18);
  lua_setfield(L, -2, "footer_y");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "show_preview");
  lua_pushstring(L, "mai");
  lua_setfield(L, -2, "query");
  lua_pushstring(L, "/home/user");
  lua_setfield(L, -2, "root");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "folder");
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
  // The row's label is the path; the name is its tail, which is where the match
  // offsets below index into.
  lua_pushstring(L, "src/main.cpp");
  lua_setfield(L, -2, "path");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "is_directory");
  lua_newtable(L); // match: 0-based offsets of the query's `mai`
  lua_pushinteger(L, 0);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, 1);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, 2);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "match");
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
  REQUIRE(g.open_count == 11);
  REQUIRE(g.lines_count == 18);     // the borderless float's whole height
  REQUIRE(g.set_cursor_count == 1); // query focus caret
  // Absolute screen cells, in the prompt row: past the `> ` prompt and after the
  // query. A float-relative column parks the caret outside the picker, on
  // whatever the editor had there.
  REQUIRE(g.last_cursor_y == 17);
  REQUIRE(g.last_cursor_x == 1 + 2 + 3);

  // The picker's own chrome: each box names itself in its top border and the
  // finder in its foot, a row is the file's path behind a selection pointer, and
  // the prompt -- with the match count at its end -- is the box's last interior
  // row, under the rows it filtered.
  const std::vector<std::string> &tel = g.line_sets.back();
  REQUIRE(tel.size() == 18);
  REQUIRE(tel[0].find("Results") != std::string::npos);
  REQUIRE(tel[17].find("Find Files") != std::string::npos);
  REQUIRE(tel[1].find("src/main.cpp") != std::string::npos);
  // The box's left border glyph is three bytes, so the row's first cell -- the
  // pointer's -- begins at byte 3.
  REQUIRE(tel[1].substr(3, 2) == "> ");
  REQUIRE(tel[16].substr(3, 5) == "> mai");
  REQUIRE(tel[16].find("1 / 3") != std::string::npos);

  // The query's characters are highlighted on the SELECTED row, and the accent
  // is allowed to be the selection's own background (the bundled dark theme's
  // pink is both) -- painted literally the matched `mai` would be invisible on
  // the row the user is looking at. The highlight takes the selection's text
  // colour there, so every match-span on that row is t_sel_fg (16 in the stub).
  bool saw_match_span = false;
  for (const auto &line : g.spans_by_line)
  {
    for (const auto &sp : line.second)
    {
      if (sp.len == 1)
      {
        saw_match_span = true;
        REQUIRE(sp.fg == 16);
      }
    }
  }
  REQUIRE(saw_match_span);

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

  // --- search panel ---
  // The compact find bar: one row, toggles and the match count on it, and no
  // key-hint footer row (the footer was the second body line before).
  push_module_field(L, 1, "search_panel");
  push_box(L, 60, 2, 70, 3);
  lua_pushstring(L, "emplace");
  lua_setfield(L, -2, "query");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "replace_text");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "replace_visible");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focus_replace");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "case_sensitive");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "whole_word");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "regex");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "scoped_to_selection");
  lua_pushstring(L, "2/7");
  lua_setfield(L, -2, "count");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 70);
  REQUIRE(g.last_border == "single");
  REQUIRE(g.lines_count == 1); // the find row only, no hint footer
  REQUIRE(g.lines[0].substr(0, 8) == "Find    ");
  REQUIRE(g.lines[0].substr(8, 7) == "emplace");
  // "Aa W .*" then the count, right-aligned on the row.
  REQUIRE(g.lines[0].size() >= 12);
  REQUIRE(g.lines[0].substr(g.lines[0].size() - 12) == "Aa W .*  2/7");
  // The field's focused background covers the whole input, not just the text.
  bool field_span = false;
  for (const StubState::RecordedSpan &sp : g.spans_by_line[1])
  {
    if (sp.start == 8 && sp.len > 7)
    {
      field_span = true;
    }
  }
  REQUIRE(field_span);

  // With the replacement up the bar is two rows: find, then replace.
  push_module_field(L, 1, "search_panel");
  push_box(L, 60, 2, 70, 4);
  lua_pushstring(L, "emplace");
  lua_setfield(L, -2, "query");
  lua_pushstring(L, "push_back");
  lua_setfield(L, -2, "replace_text");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "replace_visible");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "focus_replace");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "case_sensitive");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "whole_word");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "regex");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "scoped_to_selection");
  lua_pushstring(L, "3 closed");
  lua_setfield(L, -2, "count");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.lines_count == 2);
  REQUIRE(g.lines[0].substr(0, 8) == "Find    ");
  REQUIRE(g.lines[1].substr(0, 8) == "Replace ");
  REQUIRE(g.lines[1].substr(8, 9) == "push_back");
  REQUIRE(g.lines[0].find("Aa W .*  3 closed") != std::string::npos);
  push_module_field(L, 1, "search_panel");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- home screen ---
  push_module_field(L, 1, "home_screen");
  lua_newtable(L);
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "panel_x");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "panel_y");
  lua_pushinteger(L, 100);
  lua_setfield(L, -2, "panel_w");
  lua_pushinteger(L, 30);
  lua_setfield(L, -2, "panel_h");
  lua_pushstring(L, "JOT");
  lua_setfield(L, -2, "wordmark");
  lua_pushstring(L, "Developer workspace");
  lua_setfield(L, -2, "tagline");
  lua_pushstring(L, "Last folder  repo");
  lua_setfield(L, -2, "context");
  lua_newtable(L); // rows
  const char *h_labels[] = {"Start", "  Open Folder / File", "  New File"};
  const int h_y[] = {5, 7, 8};
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, h_labels[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, h_y[i - 1]);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, 100);
    lua_setfield(L, -2, "w");
    lua_pushboolean(L, i == 1 ? 1 : 0);
    lua_setfield(L, -2, "section");
    lua_pushboolean(L, i == 2 ? 1 : 0);
    lua_setfield(L, -2, "selected");
    if (i == 2)
    {
      lua_pushstring(L, "/home/user/repo");
      lua_setfield(L, -2, "secondary");
    }
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "rows");
  lua_newtable(L); // colors
  lua_pushinteger(L, 250);
  lua_setfield(L, -2, "default_fg");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "default_bg");
  lua_pushinteger(L, 244);
  lua_setfield(L, -2, "comment");
  lua_pushinteger(L, 215);
  lua_setfield(L, -2, "accent");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "sidebar_dir");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "sidebar_sel_fg");
  lua_pushinteger(L, 6);
  lua_setfield(L, -2, "sidebar_sel_bg");
  lua_setfield(L, -2, "colors");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 100);
  REQUIRE(g.last_height == 30);
  REQUIRE(g.last_border == "none");
  push_module_field(L, 1, "home_screen");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- status line ---
  push_module_field(L, 1, "status_line");
  push_box(L, 0, 30, 120, 2);
  lua_pushstring(L, "saved 3 files");
  lua_setfield(L, -2, "message");
  lua_pushstring(L, "  workspace");
  lua_setfield(L, -2, "context");
  lua_newtable(L); // segments
  const char *texts[] = {" file.cpp ", " 12:34 ", " 2L ", " 2 ", " main ", " 1 "};
  const int fgs[] = {15, 7, 0, 0, 0, 7};
  const int bgs[] = {4, 0, 6, 6, 6, 0};
  const char *sides[] = {"left", "left", "left", "right", "right", "right"};
  const bool optionals[] = {false, false, true, true, false, false};
  const int priorities[] = {100, 100, 90, 80, 70, 60};
  for (int i = 1; i <= 6; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, texts[i - 1]);
    lua_setfield(L, -2, "text");
    lua_pushinteger(L, fgs[i - 1]);
    lua_setfield(L, -2, "fg");
    lua_pushinteger(L, bgs[i - 1]);
    lua_setfield(L, -2, "bg");
    lua_pushboolean(L, optionals[i - 1]);
    lua_setfield(L, -2, "optional");
    lua_pushinteger(L, priorities[i - 1]);
    lua_setfield(L, -2, "priority");
    lua_pushstring(L, sides[i - 1]);
    lua_setfield(L, -2, "side");
    lua_rawseti(L, -2, i);
  }    lua_setfield(L, -2, "segments");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 120);
  REQUIRE(g.last_height == 2);
  REQUIRE(g.last_border == "none");
  REQUIRE(g.lines_count == 2); // strip rows
  REQUIRE(g.set_spans_count > 0);
  push_module_field(L, 1, "status_line");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // The bar is one row in the editor now (status_height == 1); the handler
  // still fills every row it is handed, so a one-row strip emits exactly one
  // line and no leftover second row.
  push_module_field(L, 1, "status_line");
  push_box(L, 0, 30, 120, 1);
  lua_pushstring(L, "  workspace");
  lua_setfield(L, -2, "context");
  lua_newtable(L); // segments
  for (int i = 1; i <= 2; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, i == 1 ? " file.cpp " : " 12:34 ");
    lua_setfield(L, -2, "text");
    lua_pushstring(L, "left");
    lua_setfield(L, -2, "side");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "segments");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_height == 1);
  REQUIRE(g.lines_count == 1);
  REQUIRE(g.set_spans_count > 0);
  push_module_field(L, 1, "status_line");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- sidebar ---
  push_module_field(L, 1, "sidebar");
  push_box(L, 0, 1, 28, 10);
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "content_x");
  lua_pushinteger(L, 24);
  lua_setfield(L, -2, "content_w");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "rail_w");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "rail_explorer_row");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "rail_git_row");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "border_fg");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "bg");
  lua_pushstring(L, "  src");
  lua_setfield(L, -2, "header");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "header_x");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "header_y");
  lua_pushstring(L, " 3 items");
  lua_setfield(L, -2, "footer");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "footer_x");
  lua_pushinteger(L, 8);
  lua_setfield(L, -2, "footer_y");
  lua_newtable(L); // rows
  const char *sb_labels[] = {"  src", "  main.cpp", "  api.h"};
  const int sb_fgs[] = {7, 15, 15};
  const int sb_bgs[] = {0, 0, 0};
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushinteger(L, 4);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, 2 + i);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, 23);
    lua_setfield(L, -2, "w");
    lua_pushstring(L, sb_labels[i - 1]);
    lua_setfield(L, -2, "text");
    lua_pushinteger(L, 5);
    lua_setfield(L, -2, "text_x");
    lua_pushinteger(L, sb_fgs[i - 1]);
    lua_setfield(L, -2, "fg");
    lua_pushinteger(L, sb_bgs[i - 1]);
    lua_setfield(L, -2, "bg");
    if (i == 2)
    {
      lua_pushstring(L, "M");
      lua_setfield(L, -2, "badge");
      lua_pushinteger(L, 24);
      lua_setfield(L, -2, "badge_x");
      lua_pushinteger(L, 15);
      lua_setfield(L, -2, "badge_fg");
      // File rows carry a per-language icon glyph in its own brand color
      // (like the status line); the painter must color just the glyph.
      lua_pushstring(L, "\uE61D");
      lua_setfield(L, -2, "icon");
      lua_pushinteger(L, 6);
      lua_setfield(L, -2, "icon_x");
      lua_pushinteger(L, 67);
      lua_setfield(L, -2, "icon_fg");
    }
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "rows");
  g.spans_fg.clear(); // only measure this sidebar frame
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 28);
  REQUIRE(g.last_height == 10);
  REQUIRE(g.last_border == "none");
  REQUIRE(g.lines_count == 10);     // full panel height
  REQUIRE(g.set_spans_count >= 10); // every row gets at least one span
  // The file-type icon glyph must be drawn with its brand color (67), not
  // the row foreground, mirroring the status line's colored file icon.
  bool saw_icon_fg = false;
  for (int fg : g.spans_fg)
  {
    saw_icon_fg = saw_icon_fg || fg == 67;
  }
  REQUIRE(saw_icon_fg);
  push_module_field(L, 1, "sidebar");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- side panel (outline style with detail + selection) ---
  push_module_field(L, 1, "side_panel");
  push_box(L, 100, 1, 30, 12);
  lua_pushstring(L, " Outline ");
  lua_setfield(L, -2, "title");
  lua_pushstring(L, "main.cpp  14 symbols");
  lua_setfield(L, -2, "header");
  lua_newtable(L); // rows
  const char *sp_labels[] = {"namespace jot", "void run()", "int main()"};
  const char *sp_details[] = {"1", "24", "48"};
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, sp_labels[i - 1]);
    lua_setfield(L, -2, "text");
    lua_pushstring(L, sp_details[i - 1]);
    lua_setfield(L, -2, "detail");
    lua_pushinteger(L, 7);
    lua_setfield(L, -2, "fg");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "bg");
    lua_pushboolean(L, i == 2 ? 1 : 0);
    lua_setfield(L, -2, "selected");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "rows");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_width == 30);
  REQUIRE(g.last_height == 12);
  REQUIRE(g.last_border == "single");
  REQUIRE(g.last_title == " Outline ");
  REQUIRE(g.lines_count == 4); // header + 3 rows
  push_module_field(L, 1, "side_panel");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // The dock panel masks its bottom edge, so its last row is content: a full
  // list fills h - 1 rows (only the title strip is chrome), not h - 2.
  push_module_field(L, 1, "side_panel");
  push_box(L, 100, 1, 30, 6);
  lua_newtable(L); // rows
  for (int i = 1; i <= 10; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, i == 1 ? " first entry" : " entry");
    lua_setfield(L, -2, "text");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "rows");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_height == 6);
  REQUIRE(g.lines_count == 5);
  push_module_field(L, 1, "side_panel");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  // --- the winbar paints one row per pane ------------------------------------
  // A split hands the painter every pane's row at once, so each pane gets its
  // own float and a row this frame does not carry is closed. Keyed on one row
  // it could only ever show one pane, and the others kept paying for a
  // breadcrumb row nothing drew.
  auto push_winbar_rows = [&](const std::vector<int> &panes_to_y) {
    lua_newtable(L);
    lua_newtable(L); // colors
    lua_pushinteger(L, 250);
    lua_setfield(L, -2, "fg");
    lua_pushinteger(L, 235);
    lua_setfield(L, -2, "bg");
    lua_setfield(L, -2, "colors");
    lua_newtable(L); // rows
    for (size_t i = 0; i < panes_to_y.size(); i++) {
      lua_newtable(L);
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "x");
      lua_pushinteger(L, panes_to_y[i]);
      lua_setfield(L, -2, "y");
      lua_pushinteger(L, 40);
      lua_setfield(L, -2, "w");
      lua_pushinteger(L, (lua_Integer)i);
      lua_setfield(L, -2, "pane");
      lua_newtable(L); // crumbs
      lua_newtable(L);
      lua_pushstring(L, "a.cpp");
      lua_setfield(L, -2, "label");
      lua_pushstring(L, "file");
      lua_setfield(L, -2, "kind");
      lua_pushinteger(L, 1);
      lua_setfield(L, -2, "x");
      lua_pushinteger(L, 9);
      lua_setfield(L, -2, "end_x");
      lua_rawseti(L, -2, 1);
      lua_setfield(L, -2, "crumbs");
      lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    lua_setfield(L, -2, "rows");
  };

  const int winbar_opens = g.open_count;
  const int winbar_closes = g.close_count;
  push_module_field(L, 1, "winbar");
  push_winbar_rows({1, 15});
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.open_count == winbar_opens + 2); // a float per pane, not one shared
  REQUIRE(g.last_width == 39);               // the pane's row less its last cell

  // The next frame carries one row: the pane that lost its row goes with it.
  push_module_field(L, 1, "winbar");
  push_winbar_rows({1});
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.open_count == winbar_opens + 2); // re-configured, not reopened
  REQUIRE(g.close_count == winbar_closes + 1);

  // ...and nil closes the rest.
  push_module_field(L, 1, "winbar");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.close_count == winbar_closes + 2);

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

TEST_CASE("Embedded Lua UI kit registers every handler from the binary copy")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);

  // The generated embedded_lua.cpp must carry a byte-identical copy of the
  // source ui.lua and every split module under features/ui/. This guards
  // generator staleness: editing the .lua without regenerating (or generating
  // from the wrong dir) would silently ship an outdated UI in the binary.
  size_t emb_size = 0;
  const unsigned char *emb = jot_embedded::find("features/ui.lua", &emb_size);
  REQUIRE(emb != nullptr);
  REQUIRE(emb_size > 0);

  for (const std::string &rel : jot_embedded::list_files())
  {
    if (rel.rfind("features/ui/", 0) != 0)
    {
      continue;
    }
    size_t m_size = 0;
    const unsigned char *m_emb = jot_embedded::find(rel.c_str(), &m_size);
    REQUIRE(m_emb != nullptr);
    REQUIRE(m_size > 0);
    const std::string m_path = std::string(JOT_LUA_SOURCE_DIR) + "/" + rel;
    std::ifstream m_in(m_path, std::ios::binary);
    REQUIRE(m_in.good());
    const std::string m_src((std::istreambuf_iterator<char>(m_in)), std::istreambuf_iterator<char>());
    REQUIRE(m_src.size() == m_size);
    REQUIRE(std::memcmp(m_src.data(), m_emb, m_size) == 0);
  }

  // The embedded bytes (no disk, no loader) must boot the full UI kit through
  // the same module wiring the app uses.
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  REQUIRE(luaL_loadbuffer(L, reinterpret_cast<const char *>(emb), emb_size, "embedded ui.lua")
          == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));  REQUIRE( g.handler_count == 22 );

  lua_close(L);
}


TEST_CASE("Status line drops social chrome before LSP across both sides")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  push_module_field(L, 1, "status_line");
  push_box(L, 0, 23, 60, 1);
  REQUIRE(luaL_dostring(L, R"(
    audit_segments = {
      {text=' triangle.c ', side='left', priority=100},
      {text=' 1:1 ', side='left', priority=100},
      {text=' Discord Connected ', side='left', priority=10, optional=true},
      {text=' LatihanAslab ', side='left', priority=50, optional=true},
      {text=' Warning 1 ', side='right', priority=80, optional=true},
      {text=' main ', side='right', priority=70, optional=true},
      {text=' cpp language server ', side='right', priority=60, optional=true}
    }
  )") == LUA_OK);
  lua_getglobal(L, "audit_segments");
  lua_setfield(L, -2, "segments");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(g.last_row1.find("Discord") == std::string::npos);
  REQUIRE(g.last_row1.find("cpp language server") != std::string::npos);
  REQUIRE(g.last_row1.find("triangle.c") != std::string::npos);
  REQUIRE(g.last_row1.find("1:1") != std::string::npos);
  lua_close(L);
}

// The kit used to add a `mem` chip of its own from jot.process.memory(); it is
// gone, because a number about the editor process sat beside the file's own
// news and read as debug output. The bridge itself stays for plugins, so what
// this guards is that nothing puts the segment back on the bar.
TEST_CASE("Status line carries no memory chip")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));

  auto push_payload = [&](int width)
  {
    push_module_field(L, 1, "status_line");
    push_box(L, 0, 30, width, 2);
    lua_pushstring(L, "");
    lua_setfield(L, -2, "message");
    lua_pushstring(L, "  workspace");
    lua_setfield(L, -2, "context");
    lua_newtable(L); // segments
    const char *texts[] = {" file.cpp ", " 12:34 ", " 2L ", " 2 ", " main ", " 1 "};
    const char *sides[] = {"left", "left", "left", "right", "right", "right"};
    const bool optionals[] = {false, false, true, true, false, false};
    const int priorities[] = {100, 100, 90, 80, 70, 60};
    for (int i = 1; i <= 6; i++)
    {
      lua_newtable(L);
      lua_pushstring(L, texts[i - 1]);
      lua_setfield(L, -2, "text");
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "fg");
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "bg");
      lua_pushboolean(L, optionals[i - 1]);
      lua_setfield(L, -2, "optional");
      lua_pushinteger(L, priorities[i - 1]);
      lua_setfield(L, -2, "priority");
      lua_pushstring(L, sides[i - 1]);
      lua_setfield(L, -2, "side");
      lua_rawseti(L, -2, i);
    }
    lua_setfield(L, -2, "segments");
  };

  // Wide bar: the model the editor handed over is what is rendered, and no
  // chip was added on top of it.
  push_payload(120);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_row1.find(" file.cpp ") != std::string::npos);
  REQUIRE(g.last_row1.find("mem") == std::string::npos);

  // Narrow bar: the optional chrome drops and the file keeps its place -- the
  // bar still answers for the thing being edited.
  push_payload(30);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.last_row1.find(" file.cpp ") != std::string::npos);
  REQUIRE(g.last_row1.find("mem") == std::string::npos);

  lua_close(L);
}

// Completion rows: the label is split into name / arguments / type / extra info
// and the type is right-aligned into a column measured across the visible rows
// (a port of colorful-menu.nvim's presentation). These assert the rendered rows
// themselves - the alignment and the per-part colours are the whole point - and
// they go through the real handler rather than the builder, so a row that is
// assembled wrong is still caught.
TEST_CASE("Completion rows split the label and right-align the type")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1)); // module table

  const int row_fg = 250;  // colors.fg in the stub payload
  const int comment = 244; // colors.comment
  const int type_fg = 81;  // colors.type
  const int inner_w = 44;

  // A clangd-shaped payload: bare-name labels, the parameter list and include
  // path in labelDetails, the return type in detail. The icon is single-cell
  // ASCII so the column arithmetic reads straight off the row strings.
  push_module_field(L, 1, "lsp_completion");
  push_box(L, 10, 4, inner_w, 4); // content box: 3 items + footer
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "max_items");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "start");
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "selected");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "total");
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "all_total");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "filtered");
  lua_pushstring(L, "pr");
  lua_setfield(L, -2, "prefix");
  lua_pushstring(L, "clangd");
  lua_setfield(L, -2, "server");
  lua_newtable(L); // items
  const char *i_label[] = {"printf", "parse_config", "value"};
  const char *i_detail[] = {"int", "Config", "std::uint32_t"};
  const char *i_ldetail[] = {"(const char *format, ...)", "", ""};
  const char *i_ldesc[] = {"X <stdio.h>", "", "X <cstdint>"};
  for (int i = 1; i <= 3; i++)
  {
    lua_newtable(L);
    lua_pushstring(L, i_label[i - 1]);
    lua_setfield(L, -2, "label");
    lua_pushinteger(L, 12);
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, "Function");
    lua_setfield(L, -2, "kind_name");
    lua_pushstring(L, "*");
    lua_setfield(L, -2, "kind_icon");
    lua_pushstring(L, i_detail[i - 1]);
    lua_setfield(L, -2, "detail");
    lua_pushstring(L, i_ldetail[i - 1]);
    lua_setfield(L, -2, "label_detail");
    lua_pushstring(L, i_ldesc[i - 1]);
    lua_setfield(L, -2, "label_description");
    lua_newtable(L);
    lua_setfield(L, -2, "match");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);

  REQUIRE(g.lines_count == 4); // 3 items + footer
  REQUIRE(g.lines.size() >= 3);
  const std::vector<std::string> &rows = g.lines;

  // Rows are padded to the popup's inner width in *cells*, which is what lets
  // the annotations line up on the right edge. (Count cells, not bytes: the
  // elision marker is one cell but three bytes.)
  for (int r = 0; r < 3; r++)
  {
    REQUIRE(ui_cell_count(rows[r]) == inner_w);
  }

  // Right-aligned: each row ends with its own annotation, so the start columns
  // differ by exactly the width difference. Left-aligned rows would all start
  // right after the name instead.
  const auto cell_at = [](const std::string &row, const std::string &needle)
  {
    const size_t pos = row.find(needle);
    return pos == std::string::npos ? -1 : ui_cell_count(row.substr(0, pos));
  };
  // Annotations of different lengths end on the same column (the row's right
  // edge), which is what "right-aligned" means here: a row with a 6-cell
  // annotation starts 11 cells further right than one with a 17-cell annotation.
  // "int  X <stdio.h>" is 16 cells, "Config" is 6.
  REQUIRE(cell_at(rows[0], "int  X <stdio.h>") + 16 == inner_w);
  REQUIRE(cell_at(rows[1], "Config") + 6 == inner_w);
  REQUIRE(cell_at(rows[1], "Config") - cell_at(rows[0], "int  X <stdio.h>") == 10);

  // Name in the row colour, parameter list dimmed, type and extra info in their
  // own colours - and the spans must not overlap (the float painter never
  // rewinds, so an overlap would double-draw).
  const auto &row0 = g.spans_by_line[1];
  bool saw_name = false, saw_args = false, saw_type = false, saw_extra = false;
  int previous_end = 0;
  for (const auto &sp : row0)
  {
    // The first span is present_panel's full-line background fill (a huge len
    // covering whatever the renderer clips the row to), not part of the label.
    if (sp.len >= 65535)
    {
      continue;
    }
    REQUIRE(sp.start >= previous_end);
    previous_end = sp.start + sp.len;
    const std::string text = rows[0].substr((size_t)sp.start, (size_t)sp.len);
    if (text == "printf")
    {
      REQUIRE(sp.fg == row_fg);
      saw_name = true;
    }
    else if (!text.empty() && text[0] == '(')
    {
      // The parameter list may be clipped to fit (this one is), so match on the
      // opening parenthesis rather than the whole string.
      REQUIRE(sp.fg == comment);
      saw_args = true;
    }
    else if (text == "int")
    {
      REQUIRE(sp.fg == type_fg);
      saw_type = true;
    }
    else if (text == "X <stdio.h>")
    {
      REQUIRE(sp.fg == comment);
      saw_extra = true;
    }
  }
  REQUIRE(saw_name);
  REQUIRE(saw_args);
  REQUIRE(saw_type);
  REQUIRE(saw_extra);

  // A server with no profile still gets a coloured label rather than an
  // unformatted or empty row.
  g.lines.clear();
  g.spans_by_line.clear();
  push_module_field(L, 1, "lsp_completion");
  push_box(L, 10, 4, inner_w, 3);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "max_items");
  lua_pushstring(L, "someone-elses-ls");
  lua_setfield(L, -2, "server");
  lua_newtable(L);
  lua_newtable(L);
  lua_pushstring(L, "compute");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "Function");
  lua_setfield(L, -2, "kind_name");
  lua_pushstring(L, "*");
  lua_setfield(L, -2, "kind_icon");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  REQUIRE(g.lines[0].find("compute") != std::string::npos);
  bool fallback_coloured = false;
  for (const auto &sp : g.spans_by_line[1])
  {
    if (g.lines[0].substr((size_t)sp.start, (size_t)sp.len) == "compute")
    {
      fallback_coloured = sp.fg == row_fg;
    }
  }
  REQUIRE(fallback_coloured);

  // A padded label (clangd really sends " printf") must not leave a stray space
  // after the icon, and the match offsets must move with the trim or the
  // highlighted characters would sit one cell to the left.
  g.lines.clear();
  g.spans_by_line.clear();
  push_module_field(L, 1, "lsp_completion");
  push_box(L, 10, 4, inner_w, 3);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "max_items");
  lua_pushstring(L, "clangd");
  lua_setfield(L, -2, "server");
  lua_newtable(L);
  lua_newtable(L);
  lua_pushstring(L, " printf");
  lua_setfield(L, -2, "label");
  lua_pushstring(L, "int");
  lua_setfield(L, -2, "detail");
  lua_pushstring(L, "(const char *fmt, ...)");
  lua_setfield(L, -2, "label_detail");
  lua_pushstring(L, "Function");
  lua_setfield(L, -2, "kind_name");
  // Icons carry their own trailing space (see completion_kind_icon), so the
  // label must not add a second one.
  lua_pushstring(L, "[F] ");
  lua_setfield(L, -2, "kind_icon");
  lua_newtable(L); // match: offsets 1..3 are "pri" in the padded label
  lua_pushinteger(L, 1);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, 2);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, 3);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "match");
  lua_rawseti(L, -2, 1);
  lua_setfield(L, -2, "items");
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);
  // The row starts with the gutter, the icon, one space, then the name: no
  // leftover padding from the label.
  const size_t name_pos = g.lines[0].find("printf");
  REQUIRE(name_pos != std::string::npos);
  // gutter + "[F] " = 5 cells, i.e. exactly one space of separation.
  REQUIRE(ui_cell_count(g.lines[0].substr(0, name_pos)) == 5);
  // The three matched characters land on "pri", not on " pr".
  // "[F] " is 4 cells, the gutter 1, so the name starts at byte 5 and the three
  // matched characters are the next three.
  int matched_cells = 0;
  for (const auto &sp : g.spans_by_line[1])
  {
    if (sp.start >= 5 && sp.start < 8 && sp.len == 1)
    {
      matched_cells++;
    }
  }
  REQUIRE(matched_cells == 3);

  // Closing the surface still works.
  push_module_field(L, 1, "lsp_completion");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  lua_close(L);
}

// The matched prefix of a completion row is highlighted in the accent colour,
// and the accent is allowed to be the same colour as the selection background
// (the bundled dark theme's sakura pink is both). Drawn literally, the prefix of
// the SELECTED row is then its own row's background and disappears -- typing
// `incl` in C++ showed a suggestion reading `udes` with the matched `in`
// invisible in the pink band. A highlight colour must contrast with the
// surface it sits on, so on the selected row the prefix takes the selection's
// own text colour instead, exactly as the palette and quick-pick do.
TEST_CASE("Completion keeps the matched prefix readable on the selected row")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));

  // The collision the bundled dark theme lives in: one pink for both the accent
  // and the selected row's background.
  const int pink = 215;
  const int selection_fg = 16; // colors.selection_fg from the stub box
  const int inner_w = 44;

  // Renders one `includes` row with its `in` prefix matched, at the given
  // selection index (-1 for "nothing selected").
  auto emit = [&](int selected) {
    push_module_field(L, 1, "lsp_completion");
    push_box(L, 10, 4, inner_w, 3);
    lua_getfield(L, -1, "colors");
    lua_pushinteger(L, pink);
    lua_setfield(L, -2, "accent");
    lua_pushinteger(L, pink);
    lua_setfield(L, -2, "selection_bg");
    lua_pop(L, 1);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "max_items");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "start");
    lua_pushinteger(L, selected);
    lua_setfield(L, -2, "selected");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "total");
    lua_pushstring(L, "clangd");
    lua_setfield(L, -2, "server");
    lua_newtable(L); // items
    lua_newtable(L);
    lua_pushstring(L, "includes");
    lua_setfield(L, -2, "label");
    lua_pushstring(L, "Header");
    lua_setfield(L, -2, "detail");
    lua_pushstring(L, "Function");
    lua_setfield(L, -2, "kind_name");
    lua_pushstring(L, "[F] ");
    lua_setfield(L, -2, "kind_icon");
    lua_newtable(L); // match: the `in` prefix (0-based byte offsets)
    lua_pushinteger(L, 0);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, 1);
    lua_rawseti(L, -2, 2);
    lua_setfield(L, -2, "match");
    lua_rawseti(L, -2, 1);
    lua_setfield(L, -2, "items");
    REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
    REQUIRE(lua_toboolean(L, -1));
    lua_pop(L, 1);
  };

  // Finding the match spans: the gutter and the "[F] " icon are 5 cells, so
  // the name starts at cell 5 and the two matched characters are single-cell
  // spans at 5 and 6. Nothing else on the row is a one-cell span there.
  auto match_fgs = [&]() {
    std::vector<int> fgs;
    for (const auto &line : g.spans_by_line)
    {
      for (const auto &sp : line.second)
      {
        if (sp.len == 1 && (sp.start == 5 || sp.start == 6))
        {
          fgs.push_back(sp.fg);
        }
      }
    }
    return fgs;
  };

  // Selected: the prefix must be readable, so it is the selection's text colour
  // -- never the accent that doubles as this row's background.
  emit(0);
  std::vector<int> selected_fgs = match_fgs();
  REQUIRE(selected_fgs.size() == 2);
  for (int fg : selected_fgs)
  {
    REQUIRE(fg == selection_fg);
    REQUIRE(fg != pink);
  }

  // Not selected: the accent still highlights the prefix, sitting on the row's
  // (dark) background where it contrasts normally.
  g.lines.clear();
  g.spans_by_line.clear();
  emit(-1);
  std::vector<int> plain_fgs = match_fgs();
  REQUIRE(plain_fgs.size() == 2);
  for (int fg : plain_fgs)
  {
    REQUIRE(fg == pink);
  }

  push_module_field(L, 1, "lsp_completion");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);

  lua_close(L);
}

// The palette's prompt row is the box's LAST row -- the statusline slot, which
// is where place_command_palette_cursor puts the caret. The surface paints the
// geometry the payload carries (grid rows, column width, scroll window, help
// box), and with nothing to show it still spends a row saying why -- the native
// renderer puts its "No matches" line there -- so the prompt never drifts off
// the row the caret is placed on. The module used to emit one row per match
// plus the prompt, which meant with no matches the prompt was drawn on the
// box's first row while the caret sat on its last: the input field appeared to
// move up and leave the cursor behind.
TEST_CASE("Palette keeps the prompt on the box's last row")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));

  // One call of the real handler with the geometry the native side computes:
  // `count` results in `cols` columns of `rows_per_col`, a visible grid window
  // of `grid_rows` rows starting at `first`, the prompt on the box's last row
  // (so the box is grid_rows + 1 tall), and the explanation box when `help`.
  auto emit = [&](int grid_rows,
                  int cols,
                  int rows_per_col,
                  int first,
                  int count,
                  const char *query,
                  bool help)
  {
    const int w = 60;
    const int col_w = w / cols;
    push_module_field(L, 1, "command_palette");
    push_box(L, 0, 0, w, grid_rows + 1);
    lua_pushstring(L, query);
    lua_setfield(L, -2, "query");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "selected");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "grid_x");
    lua_pushinteger(L, 20);
    lua_setfield(L, -2, "grid_y");
    lua_pushinteger(L, cols);
    lua_setfield(L, -2, "grid_cols");
    lua_pushinteger(L, col_w);
    lua_setfield(L, -2, "grid_col_w");
    lua_pushinteger(L, grid_rows);
    lua_setfield(L, -2, "grid_rows");
    lua_pushinteger(L, first);
    lua_setfield(L, -2, "grid_first_row");
    lua_pushinteger(L, rows_per_col);
    lua_setfield(L, -2, "rows_per_col");
    lua_pushinteger(L, 21);
    lua_setfield(L, -2, "prompt_y");
    if (help)
    {
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "help_x");
      lua_pushinteger(L, 16);
      lua_setfield(L, -2, "help_y");
      lua_pushinteger(L, 40);
      lua_setfield(L, -2, "help_w");
      lua_pushinteger(L, 4);
      lua_setfield(L, -2, "help_h");
      lua_pushinteger(L, 0);
      lua_setfield(L, -2, "help_for");
      lua_pushinteger(L, 250);
      lua_setfield(L, -2, "help_fg");
      lua_pushinteger(L, 236);
      lua_setfield(L, -2, "help_bg");
      lua_pushinteger(L, 240);
      lua_setfield(L, -2, "help_border_fg");
      lua_newtable(L);
      lua_pushstring(L, "does the thing");
      lua_rawseti(L, -2, 1);
      lua_pushstring(L, "Aliases: m");
      lua_rawseti(L, -2, 2);
      lua_setfield(L, -2, "help_lines");
    }
    else
    {
      lua_pushinteger(L, -1);
      lua_setfield(L, -2, "help_for");
    }
    lua_newtable(L);
    for (int i = 1; i <= count; i++)
    {
      lua_newtable(L);
      lua_pushfstring(L, "match %d", i);
      lua_setfield(L, -2, "label");
      lua_newtable(L);
      lua_setfield(L, -2, "match");
      lua_rawseti(L, -2, i);
    }
    lua_setfield(L, -2, "results");
    REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
    REQUIRE(lua_toboolean(L, -1));
    lua_pop(L, 1);
  };

  // Nothing matches: the box keeps its headroom row, that row says why it is
  // empty, and the prompt is on the last one, exactly where the caret goes.
  g.lines.clear();
  emit(0, 1, 0, 0, 0, "zzz", false);
  REQUIRE(g.lines_count == 2);
  REQUIRE(g.lines.size() == 2);
  REQUIRE(g.lines[1].find(":zzz") != std::string::npos);
  REQUIRE(g.lines[0].find("No matches for \"zzz\"") != std::string::npos);
  REQUIRE(g.lines[0].find(":zzz") == std::string::npos); // not a prompt copy

  // A scrolled two-column window: the grid fills column by column (four rows per
  // column here), and the prompt is still the last row.
  g.lines.clear();
  emit(2, 2, 4, 2, 8, "m", false);
  REQUIRE(g.lines_count == 3);
  REQUIRE(g.opens.back().y == 20); // the grid float starts where the grid does
  REQUIRE(g.opens.back().h == 3);
  REQUIRE(g.lines[0].rfind(" match 3", 0) == 0);   // column 0, first visible row
  REQUIRE(g.lines[0].rfind(" match 7", 30) == 30); // column 1 takes over after it
  REQUIRE(g.lines[1].find("match 4") != std::string::npos);
  REQUIRE(g.lines[1].find("match 8") != std::string::npos);
  REQUIRE(g.lines[2].find(":m") != std::string::npos);
  REQUIRE(g.lines[2].find("match ") == std::string::npos); // the prompt is not a match row

  // The explanation box is a second, bordered float standing on the grid's top
  // edge; it carries the selected row's sentence and aliases.
  const int opens_before = g.open_count;
  const int closes_before = g.close_count;
  emit(2, 2, 4, 0, 8, "m", true);
  REQUIRE(g.open_count == opens_before + 2);
  REQUIRE(g.opens.back().border == "single");
  REQUIRE(g.opens.back().x == 0);
  REQUIRE(g.opens.back().y == 16);
  REQUIRE(g.opens.back().w == 40);
  REQUIRE(g.opens.back().h == 4);
  REQUIRE(g.opens.back().border_fg == 240);
  REQUIRE(g.line_sets.back().size() == 2);
  REQUIRE(g.line_sets.back()[0].rfind("does the thing", 0) == 0);
  REQUIRE(g.line_sets.back()[1].rfind("Aliases: m", 0) == 0);

  // Re-presenting without help closes the box and leaves just the grid (the
  // grid float itself is closed and reopened -- that is present_panel's
  // replace -- and the box is closed on its own).
  emit(2, 2, 4, 0, 8, "m", false);
  REQUIRE(g.open_count == opens_before + 3);
  REQUIRE(g.close_count == closes_before + 3);
  REQUIRE(g.line_sets.back().size() == 3);

  // Closing still works, and takes both floats with it.
  const int closes_at_close = g.close_count;
  push_module_field(L, 1, "command_palette");
  lua_pushnil(L);
  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  lua_pop(L, 1);
  REQUIRE(g.close_count == closes_at_close + 1);

  lua_close(L);
}
