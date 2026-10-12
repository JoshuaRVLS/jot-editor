// Lua host bindings: the raw layer (jot.screen, jot.input, jot.term). Thin
// argument parsing over the LuaAPI methods in api_screen.cpp; the cell model,
// the dispatch order and the frame's byte stream live there and in
// ui/ui.cpp, events/terminal_events.cpp and render/frame.cpp.
#include "core/editor.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"
#include "lua/bindings/bindings_internal.h"

namespace lua_bind
{
  namespace
  {
    // The optional style table every paint call takes: colours in either form
    // the rest of the API accepts (an xterm-256 index or "#rrggbb"), and the
    // cell attributes. An absent or non-table argument leaves every field at
    // its unset value, which means the theme's colours and no attributes.
    LuaAPI::ScreenStyle style_at(lua_State *L, int index)
    {
      LuaAPI::ScreenStyle style;
      if (!lua_istable(L, index))
      {
        return style;
      }
      style.fg = jot_lua::table_color(L, index, "fg");
      style.bg = jot_lua::table_color(L, index, "bg");
      style.underline = jot_lua::table_int(L, index, "underline", 0);
      style.underline_fg = jot_lua::table_color(L, index, "underline_fg");
      style.bold = jot_lua::table_bool(L, index, "bold", false);
      style.italic = jot_lua::table_bool(L, index, "italic", false);
      style.dim = jot_lua::table_bool(L, index, "dim", false);
      style.reverse = jot_lua::table_bool(L, index, "reverse", false);
      return style;
    }
  } // namespace

  int l_screen_write(lua_State *L)
  {
    const int x = (int)luaL_checkinteger(L, 1);
    const int y = (int)luaL_checkinteger(L, 2);
    std::string text = luaL_optstring(L, 3, " ");
    lua_pushinteger(L, api(L).screen_write(x, y, text, style_at(L, 4)));
    return 1;
  }
  int l_screen_fill(lua_State *L)
  {
    const int x = (int)luaL_checkinteger(L, 1);
    const int y = (int)luaL_checkinteger(L, 2);
    const int w = (int)luaL_checkinteger(L, 3);
    const int h = (int)luaL_checkinteger(L, 4);
    // fill(x, y, w, h, opts) fills with spaces; fill(x, y, w, h, "#", opts)
    // fills with the glyph. A table in the fifth slot is the style, so the
    // common case does not spell out the space.
    std::string glyph = " ";
    int style_index = 5;
    if (lua_isstring(L, 5))
    {
      glyph = lua_tostring(L, 5);
      style_index = 6;
    }
    lua_pushinteger(L, api(L).screen_fill(x, y, w, h, glyph, style_at(L, style_index)));
    return 1;
  }
  int l_screen_cell(lua_State *L)
  {
    api(L).screen_cell_to_lua(L, (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2));
    return 1;
  }
  int l_screen_text(lua_State *L)
  {
    api(L).screen_text_to_lua(
        L, (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 1;
  }
  int l_screen_size(lua_State *L)
  {
    api(L).screen_size_to_lua(L);
    return 2;
  }
  int l_screen_on_paint(lua_State *L)
  {
    api(L).set_paint_hook(L, 1);
    lua_pushboolean(L, api(L).has_paint_hook());
    return 1;
  }
  int l_screen_redraw(lua_State *L)
  {
    api(L).screen_redraw();
    return 0;
  }
  int l_input_on_key(lua_State *L)
  {
    api(L).set_raw_key_hook(L, 1);
    return 0;
  }
  int l_input_on_mouse(lua_State *L)
  {
    api(L).set_raw_mouse_hook(L, 1);
    return 0;
  }
  int l_term_write(lua_State *L)
  {
    // Length-carrying read: a protocol payload can hold a NUL (a graphics
    // escape's data is arbitrary bytes), and a C string would cut it there.
    std::size_t len = 0;
    const char *bytes = luaL_checklstring(L, 1, &len);
    lua_pushboolean(L, api(L).term_write(std::string(bytes, len)));
    return 1;
  }
  int l_term_info(lua_State *L)
  {
    lua_newtable(L);
    api(L).term_info_to_lua(L);
    return 1;
  }
} // namespace lua_bind
