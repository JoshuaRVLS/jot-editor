// Lua host bindings: Event bus and timer bindings.

#include "core/editor.h"
#include "lua/api/api.h"
#include "lua/bindings/bindings_internal.h"

namespace lua_bind
{

  int l_events_subscribe(lua_State *L)
  {
    api(L).events_subscribe_from_lua(L);
    return 1;
  }
  int l_events_unsubscribe(lua_State *L)
  {
    api(L).events_unsubscribe_from_lua(L);
    return 0;
  }
  int l_events_emit(lua_State *L)
  {
    api(L).events_emit_from_lua(L);
    return 1;
  }
  int l_events_list(lua_State *L)
  {
    api(L).push_events_list(L);
    return 1;
  }
  int l_timer_set_timeout(lua_State *L)
  {
    api(L).timer_set_from_lua(L, false);
    return 1;
  }
  int l_timer_set_interval(lua_State *L)
  {
    api(L).timer_set_from_lua(L, true);
    return 1;
  }
  int l_timer_clear(lua_State *L)
  {
    api(L).timer_clear_from_lua(L);
    return 0;
  }
} // namespace lua_bind
