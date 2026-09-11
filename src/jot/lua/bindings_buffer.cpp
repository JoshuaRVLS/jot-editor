// Lua host bindings: Buffer queries and buffer-local variables, diagnostics, and marks.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_buffer_list(lua_State *L)
  {
    auto &a = api(L);
    lua_newtable(L);
    int n = 1;
    for (const auto &b : a.host().core.list_buffers())
    {
      lua_newtable(L);
      lua_pushinteger(L, b.index + 1);
      lua_setfield(L, -2, "index");
      lua_pushstring(L, b.filepath.c_str());
      lua_setfield(L, -2, "path");
      lua_pushboolean(L, b.modified);
      lua_setfield(L, -2, "modified");
      lua_pushboolean(L, b.active);
      lua_setfield(L, -2, "active");
      lua_pushboolean(L, b.preview);
      lua_setfield(L, -2, "preview");
      lua_rawseti(L, -2, n++);
    }
    return 1;
  }
  int l_switch_buffer(lua_State *L)
  {
    const bool ok = api(L).host().core.switch_buffer((int)luaL_checkinteger(L, 1) - 1);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
  }
  int l_buf_current(lua_State *L)
  {
    api(L).push_buffer_current(L);
    return 1;
  }
  int l_buf_count(lua_State *L)
  {
    api(L).push_buffer_count(L);
    return 1;
  }
  int l_buf_text(lua_State *L)
  {
    api(L).push_buffer_text(L);
    return 1;
  }
  int l_buf_meta(lua_State *L)
  {
    api(L).push_buffer_meta(L);
    return 1;
  }
  int l_buf_selection(lua_State *L)
  {
    api(L).push_buffer_selection(L);
    return 1;
  }
  int l_buf_bookmarks(lua_State *L)
  {
    api(L).push_buffer_bookmarks(L);
    return 1;
  }
  int l_buf_folds(lua_State *L)
  {
    api(L).push_buffer_folds(L);
    return 1;
  }
  int l_buf_tokens(lua_State *L)
  {
    api(L).push_buffer_tokens(L);
    return 1;
  }
  int l_buf_lines(lua_State *L)
  {
    api(L).push_buffer_lines(L);
    return 1;
  }
  int l_buf_filetype(lua_State *L)
  {
    api(L).push_buffer_filetype(L);
    return 1;
  }
  int l_buf_get_line(lua_State *L)
  {
    api(L).push_buffer_get_line(L);
    return 1;
  }
  int l_buf_select(lua_State *L)
  {
    api(L).buffer_select_from_lua(L);
    return 0;
  }
  int l_buf_clear_selection(lua_State *L)
  {
    api(L).buffer_clear_selection_from_lua(L);
    return 0;
  }
  int l_buf_apply_edit(lua_State *L)
  {
    api(L).apply_buffer_edit_from_lua(L);
    return 1;
  }
  int l_buf_set_var(lua_State *L)
  {
    api(L).set_buffer_var(L);
    return 0;
  }
  int l_buf_get_var(lua_State *L)
  {
    api(L).push_buffer_var(L);
    return 1;
  }
  int l_buf_del_var(lua_State *L)
  {
    api(L).delete_buffer_var(L);
    return 0;
  }
  int l_diagnostics_get(lua_State *L)
  {
    api(L).push_diagnostics(L);
    return 1;
  }
} // namespace lua_bind
