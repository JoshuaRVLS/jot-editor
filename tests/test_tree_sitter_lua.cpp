#include "tree_sitter/install.h"
#include "tree_sitter/manager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  // Native bindings that drive a real TreeSitterManager from the real bundled
  // init.lua (no Editor needed), mirroring lua_bridge/api_treesitter.cpp.
  TreeSitterManager &boot_mgr(lua_State *L)
  {
    return *static_cast<TreeSitterManager *>(lua_touserdata(L, lua_upvalueindex(1)));
  }

  int boot_register_language(lua_State *L)
  {
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
    bool ok = boot_mgr(L).register_language(luaL_checkstring(L, 1),
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

  int boot_disable_language(lua_State *L)
  {
    boot_mgr(L).disable_language(luaL_checkstring(L, 1));
    return 0;
  }

  int boot_language_for_extension(lua_State *L)
  {
    lua_pushstring(L, boot_mgr(L).language_for_extension(luaL_optstring(L, 1, "")).c_str());
    return 1;
  }

  int boot_status(lua_State *L)
  {
    lua_newtable(L);
    return 1;
  }
  int boot_void(lua_State *)
  {
    return 0;
  }

  int boot_set_query(lua_State *L)
  {
    std::string error;
    if (!boot_mgr(L).set_query_source(luaL_checkstring(L, 1), luaL_checkstring(L, 2), error))
    {
      return luaL_error(L, "%s", error.c_str()), 0;
    }
    lua_pushboolean(L, 1);
    return 1;
  }

  void boot_bind(lua_State *L, TreeSitterManager *manager, const char *name, lua_CFunction fn)
  {
    lua_pushlightuserdata(L, manager);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
  }
} // namespace

TEST_CASE("Real Lua runtime boot keeps every registry language registered")
{
  TreeSitterManager manager;
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);

  lua_newtable(L); // jot
  lua_newtable(L); // jot.treesitter
  lua_newtable(L); // native
  boot_bind(L, &manager, "register_language", boot_register_language);
  boot_bind(L, &manager, "disable_language", boot_disable_language);
  boot_bind(L, &manager, "language_for_extension", boot_language_for_extension);
  boot_bind(L, &manager, "status", boot_status);
  boot_bind(L, &manager, "parser", boot_void);
  boot_bind(L, &manager, "parse", boot_void);
  boot_bind(L, &manager, "query", boot_void);
  boot_bind(L, &manager, "captures", boot_void);
  boot_bind(L, &manager, "set_query", boot_set_query);
  boot_bind(L, &manager, "set_capture_color", boot_void);
  boot_bind(L, &manager, "install_command", boot_void);
  boot_bind(L, &manager, "reload", boot_void);
  boot_bind(L, &manager, "delete_parser", boot_void);
  boot_bind(L, &manager, "delete_tree", boot_void);
  boot_bind(L, &manager, "delete_query", boot_void);
  lua_setfield(L, -2, "native");
  lua_pushstring(L, JOT_LUA_SOURCE_DIR "/treesitter");
  lua_setfield(L, -2, "runtime_path");
  lua_pushcfunction(L, boot_void);
  lua_setfield(L, -2, "execute");
  lua_setfield(L, -2, "treesitter");
  lua_setglobal(L, "jot");

  std::string script = "local f,e=loadfile('" JOT_LUA_SOURCE_DIR "/treesitter/init.lua"
                       "'); "
                       "assert(f,e); assert(f())";
  int result = luaL_dostring(L, script.c_str());
  INFO(lua_tostring(L, -1));
  REQUIRE(result == LUA_OK);

  // Every registry language must survive startup. If any bundled query file
  // fails to resolve or compile against an installed parser, queries.lua
  // disables that language and :tsinstall/:tsstatus break.
  const auto names = manager.language_names();
  INFO("registered languages: " << names.size());
  for (const auto &name : names)
    INFO("  " << name);
  REQUIRE(names.size() == 62);
  REQUIRE(std::find(names.begin(), names.end(), "cpp") != names.end());

  const auto status = manager.runtime_status_for_language("cpp");
  INFO("cpp parser_message: " << status.parser_message);
  REQUIRE(status.has_language);
  REQUIRE(status.parser_message != "unsupported language");
  lua_close(L);
}

TEST_CASE("Lua Tree-sitter registry maps extensions")
{
  TreeSitterManager manager;
  manager.register_language("cpp", {".cpp"});
  REQUIRE(manager.language_for_extension(".cpp") == "cpp");
  REQUIRE(manager.language_for_extension("cpp") == "cpp");
  REQUIRE(manager.language_for_extension("unknown") == "");
  REQUIRE(manager.register_language("Test Language", {".test"}));
  REQUIRE(manager.language_for_extension(".test") == "test_language");
}

TEST_CASE("Tree-sitter install metadata follows Lua language registration")
{
  TreeSitterInstall::clear_languages();
  {
    TreeSitterManager manager;
    manager.register_language("zig",
                              {".zig"},
                              "",
                              "https://github.com/tree-sitter-grammars/tree-sitter-zig",
                              "",
                              "tree_sitter_zig",
                              {"libtree-sitter-zig.so"},
                              "");
    const auto &languages = TreeSitterInstall::supported_languages();
    REQUIRE(std::find(languages.begin(), languages.end(), "zig") != languages.end());
    auto command = TreeSitterInstall::command_for_language("zig");
#ifndef _WIN32
    REQUIRE(command.supported);
    REQUIRE(command.command.find("tree-sitter-zig") != std::string::npos);
#else
    // Source installs run a POSIX shell build script, so they are deliberately
    // unavailable on Windows (parsers load from DLLs instead). The command must
    // report that, not claim support.
    REQUIRE_FALSE(command.supported);
    REQUIRE_FALSE(command.message.empty());
#endif
  }
  TreeSitterInstall::clear_languages();
  REQUIRE(TreeSitterInstall::supported_languages().empty());
}

TEST_CASE("Lua Tree-sitter handles reject invalid lifecycle operations")
{
  TreeSitterManager manager;
  REQUIRE_FALSE(manager.delete_parser_handle(42));
  REQUIRE_FALSE(manager.delete_tree_handle(42));
  REQUIRE_FALSE(manager.delete_query_handle(42));
  std::string error;
  REQUIRE_FALSE(manager.set_query_source(".unknown", "(", error));
  REQUIRE_FALSE(error.empty());
  REQUIRE(manager.parse_handle(42, "text") == 0);

#ifdef JOT_TREESITTER
  if (manager.status(".cpp").parser_loaded)
  {
    TreeSitterHandle parser = manager.create_parser_handle(".cpp");
    REQUIRE(parser != 0);
    TreeSitterHandle tree = manager.parse_handle(parser, "int value = 1;");
    REQUIRE(tree != 0);
    TreeSitterHandle query = manager.compile_query_handle(".cpp", "(");
    REQUIRE(query == 0); // malformed query must not leak a native handle
    query = manager.compile_query_handle(".cpp", "(identifier) @variable");
    REQUIRE(query != 0);
    REQUIRE_FALSE(manager.captures_for_handles(query, tree).empty());
    REQUIRE(manager.delete_query_handle(query));
    // The compile failure must name the offending construct (error kind and
    // byte offset) instead of a generic message, so bundled-query / grammar
    // version mismatches are self-explanatory at startup.
    std::string query_error;
    REQUIRE_FALSE(
        manager.set_query_source(".cpp", "(totally_bogus_node) @variable", query_error));
    REQUIRE(query_error.find("unknown node type") != std::string::npos);
    REQUIRE(query_error.find("at byte") != std::string::npos);
    REQUIRE(manager.delete_tree_handle(tree));
    REQUIRE(manager.delete_parser_handle(parser));
  }
#endif
}

TEST_CASE("Lua Tree-sitter registration accepts query overrides")
{
  TreeSitterManager manager;
  REQUIRE(manager.register_language("example", {".example"}, ""));
  REQUIRE(manager.status(".example").language_id == "example");
  REQUIRE(manager.status("example").language_id == "example");
}

TEST_CASE("Lua query source is accepted before a parser is installed")
{
  TreeSitterManager manager;
  REQUIRE(manager.register_language("cpp", {".cpp"}, ""));
  std::string error;
  // Regression: startup used to fail here when the parser library was not
  // installed yet, which disabled the language and made :tsinstall/:tsstatus
  // list nothing.
  REQUIRE(manager.set_query_source(".cpp", "(identifier) @variable", error));
  REQUIRE(manager.status(".cpp").language_id == "cpp");
  REQUIRE_FALSE(manager.set_query_source(".unknown", "(identifier) @variable", error));
  REQUIRE_FALSE(error.empty());
}
