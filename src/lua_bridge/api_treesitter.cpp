#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/lua_loader.h"
#include "tree_sitter/install.h"
#include "tree_sitter/manager.h"

#include <cstdint>
#include <filesystem>
#include <iostream>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace
{
  namespace fs = std::filesystem;
  LuaAPI &api(lua_State *L)
  {
    return *static_cast<LuaAPI *>(lua_touserdata(L, lua_upvalueindex(1)));
  }

  TreeSitterManager &manager(lua_State *L)
  {
    return api(L).tree_sitter_manager();
  }

  void check_main_thread(lua_State *L)
  {
    if (!api(L).is_main_thread())
      luaL_error(L, "Tree-sitter API requires main thread");
  }

  TreeSitterHandle handle(lua_State *L, int index)
  {
    lua_Integer value = luaL_checkinteger(L, index);
    if (value <= 0)
      luaL_error(L, "invalid Tree-sitter handle");
    return static_cast<TreeSitterHandle>(value);
  }

  int l_language_for_extension(lua_State *L)
  {
    check_main_thread(L);
    lua_pushstring(L, manager(L).language_for_extension(luaL_checkstring(L, 1)).c_str());
    return 1;
  }

  int l_status(lua_State *L)
  {
    check_main_thread(L);
    auto status = manager(L).status(luaL_optstring(L, 1, ""));
    lua_newtable(L);
    lua_pushboolean(L, status.has_language);
    lua_setfield(L, -2, "has_language");
    lua_pushboolean(L, status.parser_loaded);
    lua_setfield(L, -2, "parser_loaded");
    lua_pushboolean(L, status.query_loaded);
    lua_setfield(L, -2, "query_loaded");
    lua_pushboolean(L, status.used_runtime_query);
    lua_setfield(L, -2, "used_runtime_query");
    lua_pushboolean(L, status.used_builtin_query);
    lua_setfield(L, -2, "used_builtin_query");
    lua_pushstring(L, status.language_id.c_str());
    lua_setfield(L, -2, "language");
    lua_pushstring(L, status.parser_message.c_str());
    lua_setfield(L, -2, "parser_message");
    lua_pushstring(L, status.query_message.c_str());
    lua_setfield(L, -2, "query_message");
    return 1;
  }

  int l_register_language(lua_State *L)
  {
    check_main_thread(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    std::vector<std::string> extensions;
    for (lua_Integer i = 1;; ++i)
    {
      lua_rawgeti(L, 2, i);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      extensions.emplace_back(luaL_checkstring(L, -1));
      lua_pop(L, 1);
    }
    std::vector<std::string> libraries;
    if (lua_istable(L, 7))
    {
      for (lua_Integer i = 1;; ++i)
      {
        lua_rawgeti(L, 7, i);
        if (lua_isnil(L, -1))
        {
          lua_pop(L, 1);
          break;
        }
        libraries.emplace_back(luaL_checkstring(L, -1));
        lua_pop(L, 1);
      }
    }
    bool ok = manager(L).register_language(luaL_checkstring(L, 1),
                                           extensions,
                                           luaL_optstring(L, 3, ""),
                                           luaL_optstring(L, 4, ""),
                                           luaL_optstring(L, 5, ""),
                                           luaL_optstring(L, 6, ""),
                                           libraries,
                                           luaL_optstring(L, 8, ""));
    lua_pushboolean(L, ok);
    return 1;
  }

  int l_disable_language(lua_State *L)
  {
    check_main_thread(L);
    manager(L).disable_language(luaL_checkstring(L, 1));
    return 0;
  }

  int l_install_command(lua_State *L)
  {
    check_main_thread(L);
    auto result =
        TreeSitterInstall::command_for_language(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
    lua_newtable(L);
    lua_pushboolean(L, result.supported);
    lua_setfield(L, -2, "supported");
    lua_pushstring(L, result.language.c_str());
    lua_setfield(L, -2, "language");
    lua_pushstring(L, result.command.c_str());
    lua_setfield(L, -2, "command");
    lua_pushstring(L, result.message.c_str());
    lua_setfield(L, -2, "message");
    return 1;
  }

  int l_parser(lua_State *L)
  {
    check_main_thread(L);
    TreeSitterHandle h = manager(L).create_parser_handle(luaL_checkstring(L, 1));
    if (!h)
      return luaL_error(L, "unable to create parser"), 0;
    lua_pushinteger(L, static_cast<lua_Integer>(h));
    return 1;
  }

  int l_parser_delete(lua_State *L)
  {
    check_main_thread(L);
    lua_pushboolean(L, manager(L).delete_parser_handle(handle(L, 1)));
    return 1;
  }

  int l_parse(lua_State *L)
  {
    check_main_thread(L);
    TreeSitterHandle old = lua_isnoneornil(L, 3) ? 0 : handle(L, 3);
    TreeSitterHandle h = manager(L).parse_handle(handle(L, 1), luaL_checkstring(L, 2), old);
    if (!h)
      return luaL_error(L, "invalid parser or tree handle"), 0;
    lua_pushinteger(L, static_cast<lua_Integer>(h));
    return 1;
  }

  int l_tree_delete(lua_State *L)
  {
    check_main_thread(L);
    lua_pushboolean(L, manager(L).delete_tree_handle(handle(L, 1)));
    return 1;
  }

  int l_query(lua_State *L)
  {
    check_main_thread(L);
    TreeSitterHandle h =
        manager(L).compile_query_handle(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
    if (!h)
      return luaL_error(L, "query compilation failed"), 0;
    lua_pushinteger(L, static_cast<lua_Integer>(h));
    return 1;
  }

  int l_query_delete(lua_State *L)
  {
    check_main_thread(L);
    lua_pushboolean(L, manager(L).delete_query_handle(handle(L, 1)));
    return 1;
  }

  int l_captures(lua_State *L)
  {
    check_main_thread(L);
    auto captures =
        manager(L).captures_for_handles(handle(L, 1),
                                        handle(L, 2),
                                        static_cast<uint32_t>(luaL_optinteger(L, 3, 0)),
                                        static_cast<uint32_t>(luaL_optinteger(L, 4, UINT32_MAX)));
    lua_newtable(L);
    int index = 1;
    for (const auto &capture : captures)
    {
      lua_newtable(L);
      lua_pushstring(L, capture.name.c_str());
      lua_setfield(L, -2, "name");
      lua_pushinteger(L, capture.start_byte);
      lua_setfield(L, -2, "start_byte");
      lua_pushinteger(L, capture.end_byte);
      lua_setfield(L, -2, "end_byte");
      lua_pushinteger(L, capture.start_row);
      lua_setfield(L, -2, "start_row");
      lua_pushinteger(L, capture.start_column);
      lua_setfield(L, -2, "start_column");
      lua_pushinteger(L, capture.end_row);
      lua_setfield(L, -2, "end_row");
      lua_pushinteger(L, capture.end_column);
      lua_setfield(L, -2, "end_column");
      lua_rawseti(L, -2, index++);
    }
    return 1;
  }

  int l_set_query(lua_State *L)
  {
    check_main_thread(L);
    std::string error;
    bool ok = manager(L).set_query_source(luaL_checkstring(L, 1), luaL_checkstring(L, 2), error);
    if (!ok)
      return luaL_error(L, "%s", error.c_str()), 0;
    lua_pushboolean(L, 1);
    return 1;
  }
  int l_set_capture_color(lua_State *L)
  {
    check_main_thread(L);
    tree_sitter_set_capture_color(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2));
    return 0;
  }
  int l_reload(lua_State *L)
  {
    check_main_thread(L);
    manager(L).reload();
    return 0;
  }

  void bind(lua_State *L, LuaAPI *a, const char *name, lua_CFunction fn)
  {
    lua_pushlightuserdata(L, a);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
  }
} // namespace

TreeSitterManager &LuaAPI::tree_sitter_manager()
{
  return editor->ts_manager_;
}
bool LuaAPI::is_main_thread() const
{
  return editor && editor->event_loop_.is_main_thread();
}

void LuaAPI::register_treesitter_api(lua_State *L)
{
  lua_getglobal(L, "jot");
  if (lua_isnil(L, -1))
  {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "jot");
  }
  lua_getfield(L, -1, "treesitter");
  if (lua_isnil(L, -1))
  {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "treesitter");
  }
  lua_newtable(L);
  bind(L, this, "language_for_extension", l_language_for_extension);
  bind(L, this, "status", l_status);
  bind(L, this, "register_language", l_register_language);
  bind(L, this, "disable_language", l_disable_language);
  bind(L, this, "install_command", l_install_command);
  bind(L, this, "parser", l_parser);
  bind(L, this, "delete_parser", l_parser_delete);
  bind(L, this, "parse", l_parse);
  bind(L, this, "delete_tree", l_tree_delete);
  bind(L, this, "query", l_query);
  bind(L, this, "delete_query", l_query_delete);
  bind(L, this, "captures", l_captures);
  bind(L, this, "set_query", l_set_query);
  bind(L, this, "set_capture_color", l_set_capture_color);
  bind(L, this, "reload", l_reload);
  lua_setfield(L, -2, "native");
  lua_pop(L, 2);
}

bool LuaAPI::load_treesitter_runtime(lua_State *L)
{
  register_treesitter_api(L);
  // User config dir -> install data dir -> developer source dir, then the
  // embedded copy materialized into the user config dir. The treesitter Lua
  // uses dofile relative to runtime_path, so it needs real files on disk.
  const fs::path path = jot_lua_resolve_path("treesitter/init.lua");
  if (path.empty())
    return false;
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "treesitter");
  lua_pushstring(L, path.parent_path().string().c_str());
  lua_setfield(L, -2, "runtime_path");
  lua_pop(L, 2);
  int top = lua_gettop(L);
  if (luaL_loadfile(L, path.string().c_str()) || lua_pcall(L, 0, 0, 0))
  {
    std::cerr << "Tree-sitter Lua runtime failed: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    return false;
  }
  return true;
}

bool LuaAPI::reload_treesitter_runtime()
{
  if (!lua_state)
    return false;
  // An explicit reload compiles synchronously so its results and errors are
  // immediate; never let it feed the background queue.
  tree_sitter_manager().set_deferred_compile_mode(false);
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "treesitter");
  lua_getfield(L, -1, "reload");
  bool ok = lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) == LUA_OK;
  if (!ok && lua_gettop(L) > top)
  {
    std::cerr << "Tree-sitter Lua reload failed: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return ok;
}

bool LuaAPI::flush_deferred_treesitter_queries()
{
  if (treesitter_queries_flushed_)
  {
    // Already recorded at boot (see Editor::initialize_lua_runtime); the
    // event-loop poller only needs to tick the background compile now.
    return true;
  }
  treesitter_queries_flushed_ = true;
  if (!lua_state)
    return false;
  tree_sitter_manager().set_deferred_compile_mode(true);
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_getglobal(L, "jot");
  lua_getfield(L, -1, "treesitter");
  lua_getfield(L, -1, "load_queries");
  bool ok = lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) == LUA_OK;
  if (!ok && lua_gettop(L) > top && lua_isstring(L, -1))
  {
    std::cerr << "Tree-sitter deferred query load failed: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  tree_sitter_manager().start_deferred_compile();
  return ok;
}

bool LuaAPI::tick_deferred_treesitter_compile()
{
  if (!lua_state)
    return false;
  TreeSitterManager &manager = tree_sitter_manager();
  switch (manager.deferred_compile_state())
  {
    case TreeSitterManager::DeferredCompileState::Compiling:
      return true; // worker still busy; keep polling
    case TreeSitterManager::DeferredCompileState::Idle:
      // Nothing queued (or the load never ran); stop the poller and make sure
      // deferred mode cannot swallow later set_query calls.
      manager.set_deferred_compile_mode(false);
      return false;
    case TreeSitterManager::DeferredCompileState::Done:
      break;
  }

  std::vector<std::string> failures = manager.finish_deferred_compile();
  manager.set_deferred_compile_mode(false);
  // The freshly installed queries may differ from the ones the first paint
  // compiled synchronously (e.g. a stale runtime query package vs the bundled
  // one); repaint so the per-line syntax cache re-runs against them right
  // away instead of waiting for the next edit or save.
  if (editor)
  {
    editor->needs_redraw = true;
  }
  if (!failures.empty())
  {
    // Surface the failures exactly like a synchronous boot load would, so the
    // message line / stderr warning still appear (just after first paint).
    lua_State *L = static_cast<lua_State *>(lua_state);
    int top = lua_gettop(L);
    lua_getglobal(L, "jot");
    lua_getfield(L, -1, "treesitter");
    lua_getfield(L, -1, "report_query_failures");
    if (lua_isfunction(L, -1))
    {
      lua_newtable(L);
      int index = 1;
      for (const auto &failure : failures)
      {
        lua_pushstring(L, failure.c_str());
        lua_rawseti(L, -2, index++);
      }
      if (lua_pcall(L, 1, 0, 0) != LUA_OK && lua_isstring(L, -1))
      {
        std::cerr << "Tree-sitter query failure report failed: " << lua_tostring(L, -1) << "\n";
      }
    }
    lua_settop(L, top);
  }
  return false;
}

void LuaAPI::tick_deferred_treesitter_parses()
{
#ifdef JOT_TREESITTER
  if (editor)
  {
    editor->install_finished_parses();
  }
#else
  (void)0;
#endif
}
