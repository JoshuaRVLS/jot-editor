// Lua host bindings: Viewport, file tree, and buffer reveal bindings.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_viewport_info(lua_State *L)
  {
    api(L).push_viewport_info(L);
    return 1;
  }
  int l_viewport_line_at(lua_State *L)
  {
    api(L).push_viewport_line_at(L);
    return 1;
  }
  int l_viewport_scroll_top(lua_State *L)
  {
    api(L).viewport_scroll_top_from_lua(L);
    return 0;
  }
  int l_viewport_scroll_lines(lua_State *L)
  {
    api(L).viewport_scroll_lines_from_lua(L);
    return 0;
  }
  int l_viewport_scroll_col(lua_State *L)
  {
    api(L).viewport_scroll_col_from_lua(L);
    return 0;
  }
  int l_viewport_reveal(lua_State *L)
  {
    api(L).viewport_reveal_from_lua(L);
    return 0;
  }
  int l_filetree_root(lua_State *L)
  {
    api(L).push_filetree_root(L);
    return 1;
  }
  int l_filetree_tree(lua_State *L)
  {
    api(L).push_filetree_tree(L);
    return 1;
  }
  int l_filetree_children(lua_State *L)
  {
    api(L).push_filetree_children(L);
    return 1;
  }
} // namespace lua_bind
