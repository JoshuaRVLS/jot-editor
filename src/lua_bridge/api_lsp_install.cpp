// Lua LSP installer bridge. The installer itself is Lua (registry +
// per-manager scripts in src/lua/lsp/), mirroring mason.nvim's architecture;
// this file is the thin native host: it loads the Lua module at boot, calls
// back into it for install/remove plans, and exposes jot.lsp.list /
// installed / root to plugins.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "lua_bridge/lua_loader.h"
#include "tools/lsp/install.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace
{
  LuaAPI &api(lua_State *L)
  {
    void *ud = lua_touserdata(L, lua_upvalueindex(1));
    if (ud)
      return *static_cast<LuaAPI *>(ud);
    if (g_lua_active_api)
      return *g_lua_active_api;
    luaL_error(L, "LSP installer API not available");
    return *reinterpret_cast<LuaAPI *>(uintptr_t(1)); // unreachable
  }

  // Calls jot.lsp.installer.<fn>(arg) and reads the string fields key_a / key_b
  // from the returned table. Returns false on any failure (unknown server,
  // missing module, raised error).
  bool call_installer_fn(lua_State *L,
                         const char *fn,
                         const std::string &arg,
                         const char *key_a,
                         std::string *out_a,
                         const char *key_b,
                         std::string *out_b,
                         const char *key_c,
                         std::string *out_c)
  {
    if (!L)
      return false;
    const int base = lua_gettop(L);
    lua_getglobal(L, "jot");
    if (lua_isnil(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    lua_getfield(L, -1, "lsp");
    if (lua_isnil(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    lua_getfield(L, -1, "installer");
    if (lua_isnil(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    lua_getfield(L, -1, fn);
    lua_remove(L, -2); // installer
    lua_remove(L, -2); // lsp
    lua_remove(L, -2); // jot
    if (!lua_isfunction(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    lua_pushlstring(L, arg.data(), arg.size());
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
      std::cerr << "[lsp-install] " << fn << " failed: " << lua_tostring(L, -1) << "\n";
      lua_settop(L, base);
      return false;
    }
    if (!lua_istable(L, -1))
    {
      lua_settop(L, base);
      return false;
    }
    bool ok = true;
    auto read_field = [&](const char *key, std::string *out)
    {
      if (!out)
        return;
      lua_getfield(L, -1, key);
      if (lua_isstring(L, -1))
      {
        *out = lua_tostring(L, -1);
      }
      else
      {
        ok = false;
      }
      lua_pop(L, 1);
    };
    read_field(key_a, out_a);
    read_field(key_b, out_b);
    read_field(key_c, out_c);
    lua_settop(L, base);
    return ok;
  }
} // namespace

bool LuaAPI::lsp_install_plan(const std::string &name,
                               std::string *id,
                               std::string *script,
                               std::string *message)
{
  return call_installer_fn(static_cast<lua_State *>(lua_state),
                           "plan_install",
                           name,
                           "id",
                           id,
                           "script",
                           script,
                           "message",
                           message);
}

bool LuaAPI::lsp_remove_plan(const std::string &name,
                              std::string *id,
                              std::string *script,
                              std::string *message)
{
  return call_installer_fn(static_cast<lua_State *>(lua_state),
                           "plan_remove",
                           name,
                           "id",
                           id,
                           "script",
                           script,
                           "message",
                           message);
}

bool LuaAPI::lsp_install_list(std::vector<LspServerSpec> *out)
{
  out->clear();
  if (!lua_state)
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int base = lua_gettop(L);
  lua_getglobal(L, "jot");
  if (lua_isnil(L, -1))
  {
    lua_settop(L, base);
    return false;
  }
  lua_getfield(L, -1, "lsp");
  if (lua_isnil(L, -1))
  {
    lua_settop(L, base);
    return false;
  }
  lua_getfield(L, -1, "installer");
  if (lua_isnil(L, -1))
  {
    lua_settop(L, base);
    return false;
  }
  lua_getfield(L, -1, "list");
  lua_remove(L, -2); // installer
  lua_remove(L, -2); // lsp
  lua_remove(L, -2); // jot
  if (!lua_isfunction(L, -1))
  {
    lua_settop(L, base);
    return false;
  }
  if (lua_pcall(L, 0, 1, 0) != LUA_OK)
  {
    std::cerr << "[lsp-install] list failed: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, base);
    return false;
  }
  if (!lua_istable(L, -1))
  {
    lua_settop(L, base);
    return false;
  }
  lua_pushnil(L);
  while (lua_next(L, -2) != 0)
  {
    if (lua_type(L, -2) == LUA_TNUMBER && lua_istable(L, -1))
    {
      LspServerSpec spec;
      lua_getfield(L, -1, "id");
      if (lua_isstring(L, -1))
        spec.id = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "display");
      if (lua_isstring(L, -1))
        spec.display = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, -1, "detail");
      if (lua_isstring(L, -1))
        spec.detail = lua_tostring(L, -1);
      lua_pop(L, 1);
      if (!spec.id.empty())
        out->push_back(std::move(spec));
    }
    lua_pop(L, 1);
  }
  lua_settop(L, base);
  return true;
}

namespace
{
  int l_lsp_install_list(lua_State *L)
  {
    LuaAPI &a = api(L);
    std::vector<LspServerSpec> servers;
    a.lsp_install_list(&servers);
    lua_newtable(L);
    int i = 1;
    for (const auto &spec : servers)
    {
      lua_newtable(L);
      lua_push_str_field(L, "id", spec.id);
      lua_push_str_field(L, "display", spec.display);
      lua_push_str_field(L, "detail", spec.detail);
      lua_push_bool_field(L, "installed", LspInstall::is_installed(spec.id));
      lua_rawseti(L, -2, i++);
    }
    return 1;
  }

  int l_lsp_installed(lua_State *L)
  {
    const char *name = lua_tostring(L, 1);
    if (!name)
    {
      lua_pushboolean(L, 0);
      return 1;
    }
    lua_pushboolean(L, LspInstall::is_installed(name) ? 1 : 0);
    return 1;
  }

  int l_lsp_root(lua_State *L)
  {
    lua_pushstring(L, LspInstall::install_root().c_str());
    return 1;
  }

  void bind(lua_State *L, LuaAPI *a, const char *name, lua_CFunction fn)
  {
    lua_pushlightuserdata(L, a);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
  }
} // namespace

void LuaAPI::register_lsp_install_api(lua_State *L)
{
  lua_getglobal(L, "jot");
  if (lua_isnil(L, -1))
  {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "jot");
  }
  lua_getfield(L, -1, "lsp");
  if (lua_isnil(L, -1))
  {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "lsp");
  }
  bind(L, this, "list", l_lsp_install_list);
  bind(L, this, "installed", l_lsp_installed);
  bind(L, this, "root", l_lsp_root);
  lua_pop(L, 2);
}

bool LuaAPI::load_lsp_installer(lua_State *L)
{
  if (!L)
    return false;
  // User config dir -> install data dir -> developer source dir, then the
  // embedded copy materialized into the user config dir. The installer Lua
  // dofiles its siblings relative to its own directory.
  const std::filesystem::path path = jot_lua_resolve_path("lsp/install.lua");
  if (path.empty())
    return false;
  lua_pushstring(L, path.parent_path().string().c_str());
  lua_setglobal(L, "jot_lsp_lua_root");
  lua_pushstring(L, LspInstall::install_root().c_str());
  lua_setglobal(L, "jot_lsp_root");
  lua_pushstring(L, LspInstall::platform_tag().c_str());
  lua_setglobal(L, "jot_lsp_platform");
  const int top = lua_gettop(L);
  if (luaL_loadfile(L, path.string().c_str()) || lua_pcall(L, 0, 0, 0))
  {
    std::cerr << "LSP installer runtime failed: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    return false;
  }
  return true;
}