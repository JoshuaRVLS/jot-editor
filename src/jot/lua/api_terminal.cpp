// LuaAPI integrated-terminal surface: listing, writing, closing,
// activating, and spawning terminal sessions.
#include "editor.h"
#include "host_api.h"
#include "jot/lua/api.h"
#include "jot/lua/api_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

#include <string>
#include <vector>

void LuaAPI::push_terminal_list(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (size_t i = 0; i < editor->integrated_terminals.size(); i++)
  {
    IntegratedTerminal *term = editor->integrated_terminals[i].get();
    if (!term)
      continue;
    lua_newtable(L);
    lua_push_int_field(L, "index", (long long)i + 1);
    lua_push_str_field(L, "label", term->get_label());
    lua_push_bool_field(L, "active", term->is_active());
    lua_push_bool_field(L, "current", (int)i == editor->current_integrated_terminal);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::terminal_write_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string text = luaL_checkstring(L, 1);
  int resolved = -1;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
  {
    resolved = (int)lua_tointeger(L, 2) - 1;
  }
  IntegratedTerminal *term = editor->get_integrated_terminal(resolved);
  lua_pushboolean(L, term ? term->send_text(text) : false);
}

void LuaAPI::terminal_close_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int resolved = editor->current_integrated_terminal;
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isnumber(L, 1))
  {
    resolved = (int)lua_tointeger(L, 1) - 1;
  }
  if (resolved < 0 || resolved >= (int)editor->integrated_terminals.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->close_integrated_terminal(resolved);
  lua_pushboolean(L, 1);
}

void LuaAPI::terminal_activate_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int resolved = editor->current_integrated_terminal;
  if (lua_gettop(L) >= 1 && !lua_isnil(L, 1) && lua_isnumber(L, 1))
  {
    resolved = (int)lua_tointeger(L, 1) - 1;
  }
  if (resolved < 0 || resolved >= (int)editor->integrated_terminals.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->activate_integrated_terminal(resolved, true);
  lua_pushboolean(L, 1);
}

void LuaAPI::terminal_spawn_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushinteger(L, 0);
    return;
  }
  const std::string label = luaL_optstring(L, 1, "");
  const std::string cwd = luaL_optstring(L, 2, "");
  const size_t before = editor->integrated_terminals.size();
  editor->create_integrated_terminal(label, cwd);
  if (editor->integrated_terminals.size() == before)
  {
    lua_pushinteger(L, 0); // shell failed to open
    return;
  }
  lua_pushinteger(L, editor->current_integrated_terminal + 1);
}

