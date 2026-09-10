// Home screen surface (runtime/lua/features/ui/home.lua): the surface keeps a
// single float + scratch buffer across emits and pushes only the lines whose
// rows changed. Any-motion mouse reporting repaints the menu per pointer cell,
// so an emit that rebuilt the surface (or leaked a float per frame, as the
// hand-rolled version did) made hover lag grow without bound.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "jot/lua/api_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  struct SetLinesCall
  {
    int buf = 0;
    int start = 0;
    int end = 0;
    std::vector<std::string> lines;
  };

  struct StubState
  {
    int buffer_creates = 0;
    int buffer_deletes = 0;
    int float_opens = 0;
    int float_closes = 0;
    int open_win = 0;
    int next_buf = 1;
    int next_win = 100;
    std::vector<SetLinesCall> set_lines;
    // One entry per set_spans call: the float line and how many spans it got.
    std::vector<std::pair<int, int>> spans;
  };

  StubState g;

  int stub_buffer_create(lua_State *L)
  {
    g.buffer_creates++;
    lua_pushinteger(L, g.next_buf++);
    return 1;
  }

  int stub_buffer_set_lines(lua_State *L)
  {
    SetLinesCall rec;
    rec.buf = (int)luaL_checkinteger(L, 1);
    rec.start = (int)luaL_checkinteger(L, 2);
    rec.end = (int)luaL_checkinteger(L, 3);
    luaL_checktype(L, 5, LUA_TTABLE);
    for (int i = 1;; i++)
    {
      lua_rawgeti(L, 5, i);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      rec.lines.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
      lua_pop(L, 1);
    }
    g.set_lines.push_back(std::move(rec));
    lua_pushboolean(L, 1);
    return 1;
  }

  int stub_buffer_delete(lua_State *)
  {
    g.buffer_deletes++;
    return 0;
  }

  int stub_float_open(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    g.float_opens++;
    g.open_win = g.next_win++;
    lua_pushinteger(L, g.open_win);
    return 1;
  }

  int stub_float_close(lua_State *L)
  {
    g.float_closes++;
    if ((int)luaL_checkinteger(L, 1) == g.open_win)
    {
      g.open_win = 0;
    }
    return 0;
  }

  int stub_float_is_valid(lua_State *L)
  {
    const int win = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, g.open_win != 0 && win == g.open_win);
    return 1;
  }

  int stub_float_set_spans(lua_State *L)
  {
    const int win = (int)luaL_checkinteger(L, 1);
    const int line = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    g.spans.emplace_back(line, (int)lua_rawlen(L, 3));
    lua_pushboolean(L, g.open_win != 0 && win == g.open_win);
    return 1;
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L);
    lua_newtable(L); // jot.ui
    lua_newtable(L); // jot.ui.buffer
    lua_pushcfunction(L, stub_buffer_create);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, stub_buffer_set_lines);
    lua_setfield(L, -2, "set_lines");
    lua_pushcfunction(L, stub_buffer_delete);
    lua_setfield(L, -2, "delete");
    lua_setfield(L, -2, "buffer");
    lua_newtable(L); // jot.ui.float
    lua_pushcfunction(L, stub_float_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, stub_float_close);
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, stub_float_is_valid);
    lua_setfield(L, -2, "is_valid");
    lua_pushcfunction(L, stub_float_set_spans);
    lua_setfield(L, -2, "set_spans");
    lua_setfield(L, -2, "float");
    lua_setfield(L, -2, "ui"); // jot.ui
    lua_setglobal(L, "jot");
  }

  // Mirrors the native model for a 100-column panel: a "Start" section header
  // on the fifth screen line and two items below it, one of which is selected.
  void push_home_payload(lua_State *L, int panel_w, int panel_h, int selected_row, int default_bg)
  {
    lua_newtable(L);
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "panel_x");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "panel_y");
    lua_pushinteger(L, panel_w);
    lua_setfield(L, -2, "panel_w");
    lua_pushinteger(L, panel_h);
    lua_setfield(L, -2, "panel_h");
    lua_pushstring(L, "JOT");
    lua_setfield(L, -2, "wordmark");
    lua_pushstring(L, "Developer workspace");
    lua_setfield(L, -2, "tagline");
    lua_pushstring(L, "Last folder  repo");
    lua_setfield(L, -2, "context");

    lua_newtable(L); // rows
    const char *labels[] = {"Start", "  Open Folder / File", "  New File"};
    const int ys[] = {5, 7, 8};
    for (int i = 1; i <= 3; i++)
    {
      lua_newtable(L);
      lua_pushstring(L, labels[i - 1]);
      lua_setfield(L, -2, "label");
      lua_pushinteger(L, 2);
      lua_setfield(L, -2, "x");
      lua_pushinteger(L, ys[i - 1]);
      lua_setfield(L, -2, "y");
      lua_pushinteger(L, panel_w);
      lua_setfield(L, -2, "w");
      lua_pushboolean(L, i == 1);
      lua_setfield(L, -2, "section");
      lua_pushboolean(L, i == selected_row);
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
    lua_pushinteger(L, default_bg);
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
  }

  int emit_home(lua_State *L, int panel_w, int panel_h, int selected_row, int default_bg = 0)
  {
    lua_getfield(L, 1, "home_screen");
    push_home_payload(L, panel_w, panel_h, selected_row, default_bg);
    const int rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK)
    {
      FAIL("home_screen error: " << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
      return rc;
    }
    const int consumed = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return consumed;
  }

  int emit_close(lua_State *L)
  {
    lua_getfield(L, 1, "home_screen");
    lua_pushnil(L);
    const int rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK)
    {
      FAIL("home_screen close error: " << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
      return rc;
    }
    lua_pop(L, 1);
    return rc;
  }
} // namespace

TEST_CASE("Home surface reuses its float and only repaints changed lines", "[jot]")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);
  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui/home.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1)); // module table

  // First emit builds the whole surface: one buffer, one float, all lines.
  REQUIRE(emit_home(L, 100, 30, 2) == 1);
  REQUIRE(g.buffer_creates == 1);
  REQUIRE(g.float_opens == 1);
  REQUIRE(g.set_lines.size() == 1);
  REQUIRE(g.set_lines[0].start == 0);
  REQUIRE(g.set_lines[0].end == -1);
  REQUIRE(g.set_lines[0].lines.size() == 30);
  // panel_y = 1, so the header spans lines 1-2 and the model rows at y = 5/7/8
  // land on float lines 5/7/8.
  REQUIRE(g.spans.size() == 5);
  REQUIRE(g.spans[0].first == 1); // wordmark + tagline
  REQUIRE(g.spans[1].first == 2); // context line
  REQUIRE(g.spans[2].first == 5); // "Start" section
  REQUIRE(g.spans[3].first == 7); // selected item
  REQUIRE(g.spans[4].first == 8); // item
  // The selected item's line carries its label and its secondary path.
  const std::string selected_line = g.set_lines[0].lines[6]; // line 7
  REQUIRE(selected_line.find("Open Folder / File") != std::string::npos);
  REQUIRE(selected_line.find("/home/user/repo") != std::string::npos);

  // Hover moves the highlight down one row: only those two lines are pushed.
  g.set_lines.clear();
  g.spans.clear();
  REQUIRE(emit_home(L, 100, 30, 3) == 1);
  REQUIRE(g.buffer_creates == 1);
  REQUIRE(g.float_opens == 1);
  REQUIRE(g.set_lines.size() == 2);
  REQUIRE(g.set_lines[0].start == 6);
  REQUIRE(g.set_lines[0].end == 7);
  REQUIRE(g.set_lines[1].start == 7);
  REQUIRE(g.set_lines[1].end == 8);
  REQUIRE(g.set_lines[0].lines.size() == 1);
  REQUIRE(g.spans.size() == 2);

  // A repeated emit with identical rows does no work at all.
  g.set_lines.clear();
  g.spans.clear();
  REQUIRE(emit_home(L, 100, 30, 3) == 1);
  REQUIRE(g.set_lines.empty());
  REQUIRE(g.spans.empty());
  REQUIRE(g.buffer_creates == 1);
  REQUIRE(g.float_opens == 1);

  // A new frame (resize) rebuilds the float once.
  REQUIRE(emit_home(L, 120, 30, 3) == 1);
  REQUIRE(g.buffer_creates == 2);
  REQUIRE(g.float_opens == 2);
  REQUIRE(g.float_closes == 1);
  REQUIRE(g.buffer_deletes == 1);

  // A theme change rebuilds it too: a reused float would keep the old colors.
  REQUIRE(emit_home(L, 120, 30, 3, /*default_bg=*/5) == 1);
  REQUIRE(g.buffer_creates == 3);
  REQUIRE(g.float_opens == 3);
  REQUIRE(g.float_closes == 2);
  REQUIRE(g.buffer_deletes == 2);

  // Closing the surface tears the float and the buffer down.
  REQUIRE(emit_close(L) == LUA_OK);
  REQUIRE(g.float_closes == 3);
  REQUIRE(g.buffer_deletes == 3);

  lua_close(L);
}
