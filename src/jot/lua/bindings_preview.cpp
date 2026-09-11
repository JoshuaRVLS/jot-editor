// Lua host bindings: the jot.preview namespace (markdown preview transport).
// Thin shims only — the LuaAPI methods in api_preview.cpp own the behavior;
// api_bindings.cpp assembles these into the jot.preview table.
#include "jot/lua/api.h"
#include "jot/lua/api_internal.h"
#include "jot/lua/bindings_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace lua_bind;

namespace lua_bind
{
  // jot.preview.start{ port = 0, host = "127.0.0.1" } -> true, port | nil, err
  int l_preview_start(lua_State *L)
  {
    api(L).preview_start_from_lua(L);
    return 2;
  }

  int l_preview_stop(lua_State *L)
  {
    api(L).preview_stop_from_lua(L);
    return 0;
  }

  // -> { running = bool, port = int, clients = int }
  int l_preview_status(lua_State *L)
  {
    api(L).preview_status_from_lua(L);
    return 1;
  }

  int l_preview_set_page(lua_State *L)
  {
    api(L).preview_set_page_from_lua(L);
    return 0;
  }

  int l_preview_page(lua_State *L)
  {
    api(L).preview_page_from_lua(L);
    return 1;
  }

  int l_preview_set_content(lua_State *L)
  {
    api(L).preview_set_content_from_lua(L);
    return 0;
  }

  int l_preview_notify(lua_State *L)
  {
    api(L).preview_notify_from_lua(L);
    return 0;
  }

  int l_preview_sync(lua_State *L)
  {
    api(L).preview_sync_from_lua(L);
    return 0;
  }

  int l_preview_take_scroll(lua_State *L)
  {
    api(L).preview_take_scroll_from_lua(L);
    return 1;
  }
} // namespace lua_bind
