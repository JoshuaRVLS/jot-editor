// Regression test for the Lua telescope renderer (src/lua/features/ui.lua):
//   - every buffer row fits the float interior in *cells* (so the C++
//     renderer never clips a row and appends ".." at the border),
//   - the list/preview separator '│' lands on the same cell column in every
//     row (a straight vertical line, no drift after wide glyphs),
//   - every colored span starts on a rune boundary and points into its row,
//   - syntax-highlight spans on preview code lines cover exactly their token.
//
// The last check regressed twice: spans were built against cell columns while
// the renderer slices rows by *bytes*. Once a row contains a multibyte glyph
// (the '│' separator itself, or CJK in names/comments) the two diverge, spans
// shift early, and split runes render as '?' garbage at the separator column.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "tools/telescope.h"
#include "ui/text.h"

namespace
{
  struct Cap
  {
    std::string lines[80];
    int count = 0;
  };
  Cap cap;

  struct SpanRec
  {
    int line = 0;
    int start = 0;
    int len = 0;
    int fg = -1;
  };
  std::vector<SpanRec> span_recs;

  int stub_handler(lua_State *)
  {
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
    cap.count = 0;
    for (int i = 1;; i++)
    {
      lua_rawgeti(L, 5, i);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      cap.lines[cap.count++] = lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    return 0;
  }
  int stub_buffer_delete(lua_State *)
  {
    return 0;
  }
  int stub_float_open(lua_State *L)
  {
    lua_pushinteger(L, 1);
    return 1;
  }
  int stub_float_close(lua_State *)
  {
    return 0;
  }
  int stub_float_set_spans(lua_State *L)
  {
    // (win, line, spans[])
    const int line = (int)lua_tointeger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 3);
    for (int i = 1; i <= n; i++)
    {
      lua_rawgeti(L, 3, i);
      if (lua_istable(L, -1))
      {
        SpanRec r;
        r.line = line;
        lua_getfield(L, -1, "start");
        r.start = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "len");
        r.len = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "fg");
        r.fg = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        span_recs.push_back(r);
      }
      lua_pop(L, 1);
    }
    return 0;
  }
  int stub_ui_set_cursor(lua_State *)
  {
    return 0;
  }
  // Mimics jot.syntax.highlight: byte offsets into the input text. Keywords
  // and //-comments are tagged; everything else is skipped by rune.
  int stub_syntax_highlight(lua_State *L)
  {
    const std::string s = luaL_checkstring(L, 2);
    lua_newtable(L);
    int n = 0;
    const auto add = [&](int start, int len, const char *kind)
    {
      n++;
      lua_newtable(L);
      lua_pushinteger(L, start);
      lua_setfield(L, -2, "start");
      lua_pushinteger(L, len);
      lua_setfield(L, -2, "len");
      lua_pushstring(L, kind);
      lua_setfield(L, -2, "kind");
      lua_rawseti(L, -2, n);
    };
    size_t i = 0;
    while (i < s.size())
    {
      const unsigned char c = (unsigned char)s[i];
      if (c == '/' && i + 1 < s.size() && s[i + 1] == '/')
      {
        add((int)i, (int)(s.size() - i), "comment");
        break;
      }
      if (c == ' ')
      {
        i++;
        continue;
      }
      if (c < 0x80 && (isalnum(c) || c == '_'))
      {
        size_t j = i;
        while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_'))
          j++;
        const std::string word = s.substr(i, j - i);
        if (word == "return" || word == "bool" || word == "int")
          add((int)i, (int)(j - i), "keyword");
        i = j;
        continue;
      }
      // Skip a multibyte rune as a unit (byte-accurate positions).
      i++;
      while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80)
        i++;
    }
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
    lua_newtable(L); // jot.syntax
    lua_pushcfunction(L, stub_syntax_highlight);
    lua_setfield(L, -2, "highlight");
    lua_setfield(L, -2, "syntax");
    lua_setglobal(L, "jot");
  }

  void push_int(lua_State *L, const char *k, int v)
  {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
  }
  void push_str(lua_State *L, const char *k, const char *v)
  {
    lua_pushstring(L, v);
    lua_setfield(L, -2, k);
  }

  // Covered bytes of a span, for exact token matching.
  std::string covered(const SpanRec &r)
  {
    if (r.line < 1 || r.line > cap.count || r.start < 0 || r.start >= (int)cap.lines[r.line - 1].size())
      return "";
    const std::string &row = cap.lines[r.line - 1];
    const int take = std::min(r.len, (int)row.size() - r.start);
    return row.substr((size_t)r.start, (size_t)take);
  }
} // namespace

TEST_CASE("Telescope Lua render: straight separator, bounded rows, byte-aligned spans")
{
  cap = Cap{};
  span_recs.clear();
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  push_stub_jot(L);
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);

  // Real geometry for a 120x32 terminal with a 1-row status line.
  const TelescopeLayout lay = telescope_layout_for(120, 32, 1, 31);
  REQUIRE(lay.valid);
  REQUIRE(lay.show_preview); // the preview pane is what exercises the spans

  lua_getfield(L, 1, "telescope");
  lua_newtable(L); // payload p
  push_int(L, "x", lay.x);
  push_int(L, "y", lay.y);
  push_int(L, "w", lay.w);
  push_int(L, "h", lay.h);
  push_int(L, "inner_x", lay.inner_x);
  push_int(L, "inner_y", lay.inner_y);
  push_int(L, "inner_w", lay.inner_w);
  push_int(L, "inner_h", lay.inner_h);
  push_int(L, "query_x", lay.query_x);
  push_int(L, "query_y", lay.query_y);
  push_int(L, "query_w", lay.query_w);
  push_int(L, "body_y", lay.body_y);
  push_int(L, "body_h", lay.body_h);
  push_int(L, "list_x", lay.list_x);
  push_int(L, "list_y", lay.list_y);
  push_int(L, "list_w", lay.list_w);
  push_int(L, "list_h", lay.list_h);
  push_int(L, "preview_x", lay.preview_x);
  push_int(L, "preview_y", lay.preview_y);
  push_int(L, "preview_w", lay.preview_w);
  push_int(L, "preview_h", lay.preview_h);
  push_int(L, "footer_y", lay.footer_y);
  lua_pushboolean(L, lay.show_preview ? 1 : 0);
  lua_setfield(L, -2, "show_preview");
  push_str(L, "query", "c");
  push_str(L, "root", "/home/josrvl/jot");
  push_str(L, "title", " Find Files ");
  push_int(L, "selected", 1);
  push_int(L, "list_scroll", 0);
  push_int(L, "result_count", 9);
  push_str(L, "focus", "results");
  lua_newtable(L); // colors (flat: panel slots + telescope slots + token kinds)
  push_int(L, "fg", 250);
  push_int(L, "bg", 0);
  push_int(L, "panel_bg", 0);
  push_int(L, "border", 240);
  push_int(L, "selection_fg", 16);
  push_int(L, "selection_bg", 17);
  push_int(L, "comment", 244);
  push_int(L, "accent", 215);
  push_int(L, "t_fg", 250);
  push_int(L, "t_bg", 0);
  push_int(L, "t_sel_fg", 16);
  push_int(L, "t_sel_bg", 17);
  push_int(L, "t_prev_fg", 250);
  push_int(L, "t_prev_bg", 0);
  push_int(L, "keyword", 210);
  push_int(L, "comment", 244);
  push_int(L, "function", 215);
  push_int(L, "string", 172);
  push_int(L, "type", 180);
  push_int(L, "number", 173);
  lua_setfield(L, -2, "colors"); // payload.colors

  // Results: realistic names incl. CJK dirs (wide glyphs shift bytes/cells).
  const char *names[] = {"api_core.cpp",
                         "api_treesitter.cpp",
                         "文件名称很长.cpp",
                         "main.h",
                         "README中文.md",
                         "数据表.txt",
                         "ui.lua",
                         "test_lua_ui_kit.cpp",
                         "telescope.cpp"};
  const char *parents[] = {"src/lua_bridge",
                           "src/lua_bridge",
                           "src/lua_bridge",
                           "src",
                           ".",
                           "src/库",
                           "src/lua/features",
                           "tests",
                           "src/tools"};
  lua_newtable(L); // results
  for (int i = 1; i <= 9; i++)
  {
    lua_newtable(L);
    push_str(L, "name", names[i - 1]);
    push_str(L, "parent_path", parents[i - 1]);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "is_directory");
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "results");

  // Preview code excerpt with CJK comments and a keyword after a CJK
  // identifier (the exact case that produced '?' garbage when spans used
  // cell origins instead of byte positions).
  lua_newtable(L); // preview
  push_str(L, "title", "api_core.cpp");
  push_str(L, "detail", "212.5 KB");
  push_int(L, "start_line", 5024);
  push_str(L, "extension", ".cpp");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "is_directory");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "skipped");
  lua_newtable(L); // lines
  const char *code[] = {"bool LuaAPI::emit_telescope(const TelescopeView &view)",
                        "{",
                        "  return emit_lua_ui(\"telescope\",",
                        "                     [&](lua_State *L, int t)",
                        "                     {",
                        "                       // 宽字符注释：数据表 库 文件 名称很长 都是汉字",
                        "                       std::string 数据; return 0;",
                        "                       lua_set_int_field(L, t, \"x\", view.x);"};
  for (int i = 1; i <= 7; i++)
  {
    lua_pushstring(L, code[i - 1]);
    lua_rawseti(L, -2, i);
  }
  lua_setfield(L, -2, "lines");
  lua_setfield(L, -2, "preview"); // payload.preview

  REQUIRE(lua_pcall(L, 1, 1, 0) == LUA_OK);
  REQUIRE(lua_toboolean(L, -1));
  lua_pop(L, 1);

  const int inner_w = lay.inner_w;

  // 1. Every row fits the float interior in cells (no ".." clip at border).
  for (int i = 0; i < cap.count; i++)
  {
    CAPTURE(i);
    REQUIRE(ui_cell_count(cap.lines[i]) <= inner_w);
  }

  // 2. The separator '│' is on one constant cell column across all rows.
  int sep_col = -1;
  for (int i = 0; i < cap.count; i++)
  {
    const std::string &row = cap.lines[i];
    for (size_t j = 0; j + 2 < row.size();)
    {
      if ((unsigned char)row[j] == 0xE2 && (unsigned char)row[j + 1] == 0x94
          && (unsigned char)row[j + 2] == 0x82)
      {
        const int col = ui_cell_count(row.substr(0, j));
        if (sep_col == -1)
          sep_col = col;
        else
          REQUIRE(col == sep_col);
        break;
      }
      const int len = ui_utf8_char_len(row, (int)j);
      if (len <= 0)
      {
        j++;
        continue;
      }
      j += (size_t)len;
    }
  }
  REQUIRE(sep_col != -1);

  // 3. Every span starts on a rune boundary and points inside its row
  //    (huge-len background spans are clamped by the renderer).
  for (const auto &r : span_recs)
  {
    if (r.line < 1 || r.line > cap.count)
      continue;
    const std::string &row = cap.lines[r.line - 1];
    const bool huge = r.len > 1000;
    REQUIRE(r.start >= 0);
    REQUIRE(r.start <= (int)row.size());
    if (!huge)
      REQUIRE(r.start + r.len <= (int)row.size());
    if (r.start > 0 && r.start < (int)row.size())
    {
      REQUIRE(((unsigned char)row[r.start] & 0xC0) != 0x80); // not mid-rune
    }
  }

  // 4. Keyword spans cover exactly their token (byte origin = append point).
  bool saw_bool = false;
  bool saw_return = false;
  bool saw_int = false;
  for (const auto &r : span_recs)
  {
    if (r.fg != 210)
      continue;
    const std::string c = covered(r);
    if (c == "bool")
      saw_bool = true;
    if (c == "return")
      saw_return = true;
    if (c == "int")
      saw_int = true;
  }
  REQUIRE(saw_bool);
  REQUIRE(saw_return);
  REQUIRE(saw_int);
  REQUIRE(span_recs.size() >= 4); // bg + text spans per row

  lua_close(L);
}
