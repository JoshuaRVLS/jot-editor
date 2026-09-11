// Lua host bindings: LSP bindings (clients, requests, diagnostics, completion, hover UI, install).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_lsp_clients(lua_State *L)
  {
    api(L).push_lsp_clients(L);
    return 1;
  }
  int l_lsp_request_hover(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 0);
    return 0;
  }
  int l_lsp_request_definition(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 1);
    return 0;
  }
  int l_lsp_request_symbols(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 2);
    return 0;
  }
  int l_lsp_diagnostics(lua_State *L)
  {
    api(L).push_lsp_diagnostics(L);
    return 1;
  }
  int l_lsp_results(lua_State *L)
  {
    api(L).push_lsp_last_results(L);
    return 1;
  }
  int l_lsp_completions(lua_State *L)
  {
    api(L).push_lsp_completions(L);
    return 1;
  }
  int l_lsp_request_completion(lua_State *L)
  {
    api(L).lsp_request_from_lua(L, 3);
    return 0;
  }
  int l_lsp_disabled(lua_State *L)
  {
    api(L).push_lsp_disabled(L);
    return 1;
  }
  int l_lsp_set_enabled(lua_State *L)
  {
    api(L).lsp_set_enabled_from_lua(L);
    return 0;
  }
  int l_lsp_install(lua_State *L)
  {
    api(L).lsp_install_from_lua(L);
    return 1;
  }
  int l_lsp_remove(lua_State *L)
  {
    api(L).lsp_remove_from_lua(L);
    return 1;
  }
  int l_lsp_restart_all(lua_State *L)
  {
    api(L).lsp_restart_all_from_lua(L);
    return 0;
  }
  int l_lsp_accept_completion(lua_State *L)
  {
    api(L).lsp_accept_completion_from_lua(L);
    return 1;
  }
  int l_lsp_register_snippet_handler(lua_State *L)
  {
    // jot.lsp.register_snippet_handler(fn) claims LSP snippet completions;
    // nil clears it and restores the built-in plain-text expansion.
    if (lua_isnoneornil(L, 1))
    {
      api(L).set_lsp_snippet_handler_ref(LUA_NOREF);
    }
    else
    {
      luaL_checktype(L, 1, LUA_TFUNCTION);
      lua_pushvalue(L, 1);
      api(L).set_lsp_snippet_handler_ref(luaL_ref(L, LUA_REGISTRYINDEX));
    }
    return 0;
  }
  int l_lsp_hover_ui(lua_State *L)
  {
    // jot.lsp.hover_ui(fn) registers a Lua hover renderer; nil clears it.
    if (lua_isnoneornil(L, 1))
    {
      api(L).set_lsp_hover_ui_handler(L, 0);
    }
    else
    {
      luaL_checktype(L, 1, LUA_TFUNCTION);
      api(L).set_lsp_hover_ui_handler(L, 1);
    }
    return 0;
  }
} // namespace lua_bind
