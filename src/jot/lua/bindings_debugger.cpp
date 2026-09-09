// Lua host bindings: Debugger bindings (state, breakpoints, stack/variables/threads requests, output scrolling).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_dbg_configs(lua_State *L)
  {
    api(L).push_debugger_configs(L);
    return 1;
  }
  int l_dbg_run_config(lua_State *L)
  {
    api(L).run_debugger_config_from_lua(L);
    return 1;
  }
  int l_dbg_state(lua_State *L)
  {
    api(L).push_debugger_state(L);
    return 1;
  }
  int l_dbg_breakpoints(lua_State *L)
  {
    api(L).push_debugger_breakpoints(L);
    return 1;
  }
  int l_dbg_toggle_bp(lua_State *L)
  {
    api(L).debugger_toggle_breakpoint_from_lua(L);
    return 1;
  }
  int l_dbg_has_bp(lua_State *L)
  {
    api(L).debugger_has_breakpoint_from_lua(L);
    return 1;
  }
  int l_dbg_request_stack(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 0);
    return 0;
  }
  int l_dbg_request_variables(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 1);
    return 0;
  }
  int l_dbg_request_threads(lua_State *L)
  {
    api(L).debugger_request_from_lua(L, 2);
    return 0;
  }
  int l_dbg_scroll_output(lua_State *L)
  {
    api(L).debugger_scroll_output_from_lua(L);
    return 1;
  }
  int l_dbg_cycle_thread(lua_State *L)
  {
    api(L).debugger_cycle_thread_from_lua(L);
    return 1;
  }
  int l_dbg_cycle_frame(lua_State *L)
  {
    api(L).debugger_cycle_frame_from_lua(L);
    return 1;
  }
} // namespace lua_bind
