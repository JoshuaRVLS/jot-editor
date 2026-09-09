// Lua host bindings: Mark set/get/jump/delete/list bindings.

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_mark_set(lua_State *L)
  {
    api(L).set_mark(L);
    return 1;
  }
  int l_mark_get(lua_State *L)
  {
    api(L).push_mark(L);
    return 1;
  }
  int l_mark_jump(lua_State *L)
  {
    api(L).jump_mark(L);
    return 1;
  }
  int l_mark_del(lua_State *L)
  {
    api(L).delete_mark(L);
    return 1;
  }
  int l_mark_list(lua_State *L)
  {
    api(L).push_mark_list(L);
    return 1;
  }
} // namespace lua_bind
