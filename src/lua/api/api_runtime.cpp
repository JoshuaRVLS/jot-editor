// LuaAPI runtime lifecycle: state cleanup, config and plugin loading,
// callback dispatch, the UI-kit runtime bootstrap, and process-memory
// reporting.
#include "core/editor.h"
#include "core/host_api.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

#include "lua/embedded_lua.h"
#include "lua/lua_loader.h"
#include <chrono>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <psapi.h>
#include <windows.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace
{
  // The user's Lua directory: $JOT_CONFIG_HOME when set (which is what the
  // probes pin), else the platform config dir. Shared by the plugin scan, the
  // config file and the module path, so the three can never disagree about
  // where "the user's Lua" lives.
  fs::path lua_config_root()
  {
    const char *env = getenv("JOT_CONFIG_HOME");
    if (env && *env)
    {
      return env;
    }
    const char *app = getenv("APPDATA");
    if (app && *app)
    {
      return fs::path(app) / "jot";
    }
    const char *home = getenv("HOME");
    if (home)
    {
      return fs::path(home) / ".config" / "jot";
    }
    return {};
  }
} // namespace

void LuaAPI::setup_lua_module_path(lua_State *L)
{
  // package.path for the user's own files. Without this a Lua feature is one
  // file, because `require` resolves against Lua's built-in paths (the process
  // CWD and LUA_PATH), never the editor's config dir -- so a feature that wants
  // a module tree had to be compiled into the binary. The config dir and its
  // lua/ and plugins/ subdirectories come first, so a user file of the same
  // name always wins over a bundled one.
  const fs::path root = lua_config_root();
  std::string prefix;
  if (!root.empty())
  {
    for (const fs::path &dir : {root, root / "lua", root / "plugins"})
    {
      prefix += (dir / "?.lua").string() + ";";
      prefix += (dir / "?" / "init.lua").string() + ";";
    }
  }
  lua_getglobal(L, "package");
  if (!lua_istable(L, -1))
  {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, "path");
  const char *existing = lua_tostring(L, -1);
  const std::string path = prefix + (existing ? existing : "");
  lua_pop(L, 1);
  lua_pushlstring(L, path.data(), path.size());
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
}

void LuaAPI::cleanup()
{
  if (!lua_state)
    return;
  cancel_all_timers();
  clear_floats();
  if (lsp_hover_ui_ref_ != LUA_NOREF)
  {
    luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, lsp_hover_ui_ref_);
    lsp_hover_ui_ref_ = LUA_NOREF;
  }
  if (lsp_snippet_handler_ref_ != LUA_NOREF)
  {
    luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, lsp_snippet_handler_ref_);
    lsp_snippet_handler_ref_ = LUA_NOREF;
  }
  // The raw layer's callbacks and the policy providers: unref before the state
  // goes away.
  clear_raw_hooks();
  clear_lua_policies();
  for (auto &[name, ref] : lua_ui_handlers_)
  {
    (void)name;
    if (ref >= 0)
    {
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, ref);
    }
  }
  lua_ui_handlers_.clear();
  lua_close(static_cast<lua_State *>(lua_state));
  lua_state = nullptr;
  lua_callbacks.clear();
  lua_initialized = false;
  timer_entries_.clear();
}
void LuaAPI::clear_runtime_state()
{
  plugin_commands.clear();
  plugin_keymaps.clear();
  plugin_autocmds.clear();
  plugin_panels.clear();
  status_segments_.clear();
  plugin_load_status.clear();
  cancel_all_timers();
  clear_floats();
  if (lua_state)
  {
    for (auto &x : lua_callbacks)
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, x.second);
    for (auto &kv : event_subscribers_)
      for (auto &s : kv.second)
        luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, s.ref);
  }
  lua_callbacks.clear();
  event_subscribers_.clear();
  pending_lsp_hover.clear();
  pending_lsp_definition.clear();
  pending_lsp_symbols.clear();
  pending_lsp_completion.clear();
  pending_debugger_stack.clear();
  pending_debugger_variables.clear();
  pending_debugger_threads.clear();
  pending_debugger_session = -1;
  edit_snapshots_.clear();
  last_edit_ = LuaEditDelta{};
  if (lsp_snippet_handler_ref_ != LUA_NOREF && lua_state)
  {
    luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, lsp_snippet_handler_ref_);
    lsp_snippet_handler_ref_ = LUA_NOREF;
  }
  // A plugin reload drops the raw layer's callbacks and the policy providers
  // with everything else: a painter or key hook left over from the previous run
  // would keep running against a state that no longer defines it, and a policy
  // provider would keep answering for a module that is gone (the bundled one
  // registers again as it reloads, so the fallback covers the gap).
  clear_raw_hooks();
  clear_lua_policies();
}
bool LuaAPI::load_script_path(const std::string &module, const std::string &path)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  if (luaL_loadfile(L, path.c_str()) || lua_pcall(L, 0, 0, 0))
  {
    std::string e = lua_tostring(L, -1);
    lua_pop(L, 1);
    plugin_load_status.push_back({module, path, false, e});
    if (editor)
      editor->set_message("Plugin failed: " + module);
    lua_settop(L, top);
    return false;
  }
  plugin_load_status.push_back({module, path, true, ""});
  return true;
}
void LuaAPI::load_config_file()
{
  if (!lua_initialized)
    return;
  const fs::path root = lua_config_root();
  if (root.empty())
    return;
  fs::path cfg = root / "config.lua";
  if (fs::exists(cfg))
    load_script_path("jot_config", cfg.string());
}
void LuaAPI::load_plugins()
{
  if (!lua_initialized)
    return;
  const fs::path root = lua_config_root();
  if (root.empty())
    return;
  fs::create_directories(root / "plugins");
  clear_runtime_state();
  load_config_file();
  fs::path init = root / "init.lua";
  if (fs::exists(init))
    load_script_path("jot_init", init.string());
  std::vector<fs::path> files;
  for (auto &e : fs::directory_iterator(root / "plugins"))
  {
    if (e.is_regular_file() && e.path().extension() == ".lua")
      files.push_back(e.path());
    else if (e.is_directory() && fs::exists(e.path() / "plugin.lua"))
      files.push_back(e.path() / "plugin.lua");
  }
  std::sort(files.begin(), files.end());
  for (auto &p : files)
    load_script_path("jot_plugin_" + p.stem().string(), p.string());
  if (editor)
    editor->apply_config_live();
  fire_autocmd("EditorEnter");
}
void LuaAPI::lua_dir_to_lua(lua_State *L)
{
  lua_pushstring(L, lua_config_root().string().c_str());
}

void LuaAPI::lua_path_to_lua(lua_State *L)
{
  lua_getglobal(L, "package");
  if (!lua_istable(L, -1))
  {
    lua_settop(L, 0);
    lua_pushstring(L, "");
    return;
  }
  lua_getfield(L, -1, "path");
  const char *path = lua_tostring(L, -1);
  lua_pushstring(L, path ? path : "");
  // Return the string, dropping `package` below it.
  lua_replace(L, -3);
  lua_settop(L, 1);
}

void LuaAPI::lua_reload_from_lua(lua_State *L)
{
  const char *name = luaL_optstring(L, 1, "");
  if (!lua_state)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "Lua runtime is not initialized");
    return;
  }
  if (!name || !*name)
  {
    // No module named: the whole user runtime, init.lua and every plugin file,
    // re-runs. That is the same work as :reloadplugins, reachable from a
    // keymap, an autocmd or another module. The second return is nil: there is
    // no single module value to hand back.
    reload_plugins();
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return;
  }
  lua_State *S = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(S);
  // Drop the module and anything loaded under it from package.loaded, so the
  // next require re-runs the file instead of handing back the cached table.
  // A module that was never required is simply absent, which is not an error.
  const std::string prefix = std::string(name) + ".";
  std::vector<std::string> stale;
  lua_getglobal(S, "package");
  if (lua_istable(S, -1))
  {
    lua_getfield(S, -1, "loaded");
    if (lua_istable(S, -1))
    {
      lua_pushnil(S);
      while (lua_next(S, -2))
      {
        if (lua_type(S, -2) == LUA_TSTRING)
        {
          const std::string key = lua_tostring(S, -2);
          if (key == name || key.compare(0, prefix.size(), prefix) == 0)
          {
            stale.push_back(key);
          }
        }
        lua_pop(S, 1);
      }
      for (const std::string &key : stale)
      {
        lua_pushnil(S);
        lua_setfield(S, -2, key.c_str());
      }
    }
  }
  lua_settop(S, top);
  lua_getglobal(L, "require");
  if (!lua_isfunction(L, -1))
  {
    lua_settop(L, top);
    lua_pushboolean(L, 0);
    lua_pushstring(L, "require is not available");
    return;
  }
  lua_pushstring(L, name);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK)
  {
    // false, error: a module that throws while reloading reports what it threw
    // instead of leaving the script with a bare failure.
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return;
  }
  // true, the module's value: the file ran again (stale.size() entries were
  // dropped from package.loaded to make that happen).
  lua_pushboolean(L, 1);
  lua_insert(L, -2);
}

void LuaAPI::reload_plugins()
{
  load_plugins();
  // Re-register the bundled inline-diagnostics autocmd, which
  // clear_runtime_state() inside load_plugins() wipes (same reason it is
  // loaded after load_plugins() at boot).
  if (lua_initialized)
  {
    jot_lua::load_bundled_lua_file(
        static_cast<lua_State *>(lua_state), "features/decorations.lua", "Decorations");
    jot_lua::load_bundled_lua_file(
        static_cast<lua_State *>(lua_state), "features/todo_comments.lua", "TodoComments");
    // The bundled policy providers register here for the same reason: the
    // clear_runtime_state() above dropped them, and a reload must leave the
    // session where a fresh boot would be. User config has already re-run, so a
    // provider it owns is in place first and keeps the name.
    load_bible_runtime(static_cast<lua_State *>(lua_state));
    load_settings_runtime(static_cast<lua_State *>(lua_state));
  }
  fire_autocmd("PluginReload");
  if (editor)
    editor->set_message("Reloaded " + std::to_string(plugin_load_status.size())
                        + " plugin file(s)");
}
bool LuaAPI::call_callback_string(const std::string &id, const std::string &arg)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
    return false;
  auto it = lua_callbacks.find(id);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  lua_pushstring(L, arg.c_str());
  bool ok = lua_pcall(L, 1, 0, 0) == LUA_OK;
  if (!ok)
  {
    std::cerr << "Lua callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return ok;
}
bool LuaAPI::call_callback_event(const std::string &id,
                                 const std::string &event,
                                 const std::string &filepath,
                                 int buffer,
                                 const LuaEditDelta *edit)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
    return false;
  auto it = lua_callbacks.find(id);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  lua_newtable(L);
  lua_pushstring(L, event.c_str());
  lua_setfield(L, -2, "event");
  lua_pushstring(L, filepath.c_str());
  lua_setfield(L, -2, "path");
  if (buffer < 0)
    lua_pushnil(L);
  else
    lua_pushinteger(L, buffer + 1);
  lua_setfield(L, -2, "buffer");
  if (buffer >= 0 && buffer < (int)editor->buffers.size())
  {
    const Cursor pos = editor->buffers[(size_t)buffer].cursor;
    lua_pushinteger(L, pos.y + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, pos.x + 1);
    lua_setfield(L, -2, "column");
  }
  if (event == "BufChange" && edit && edit->valid)
  {
    lua_pushinteger(L, edit->start_line);
    lua_setfield(L, -2, "edit_start_line");
    lua_pushinteger(L, edit->start_col);
    lua_setfield(L, -2, "edit_start_col");
    lua_pushinteger(L, edit->end_line);
    lua_setfield(L, -2, "edit_end_line");
    lua_pushinteger(L, edit->end_col);
    lua_setfield(L, -2, "edit_end_col");
    lua_pushstring(L, edit->inserted.c_str());
    lua_setfield(L, -2, "edit_inserted");
    lua_pushstring(L, edit->removed.c_str());
    lua_setfield(L, -2, "edit_removed");
    lua_pushboolean(L, edit->multiline);
    lua_setfield(L, -2, "edit_multiline");
  }
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua event callback error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}
long long LuaAPI::process_memory_bytes()
{
  // The status line polls this every frame; re-read at most once a second.
  const auto now = std::chrono::steady_clock::now();
  if (cached_process_memory_bytes_ >= 0
      && now - last_process_memory_read_ < std::chrono::seconds(1))
  {
    return cached_process_memory_bytes_;
  }

  long long resident = -1;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
  {
    resident = (long long)counters.WorkingSetSize;
  }
#else
  // Linux /proc/self/statm: "size resident shared text lib data dt" - the
  // second field is the resident set in pages.
  std::ifstream statm("/proc/self/statm");
  if (statm)
  {
    long long total_pages = 0;
    long long resident_pages = 0;
    statm >> total_pages >> resident_pages;
    const long page_size = sysconf(_SC_PAGESIZE);
    if (resident_pages > 0 && page_size > 0)
    {
      resident = resident_pages * (long long)page_size;
    }
  }
  if (resident < 0)
  {
    // Fallback: peak RSS (KB on Linux/macOS) - close enough when statm is
    // unavailable (some sandboxes hide /proc).
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0)
    {
      resident = (long long)usage.ru_maxrss * 1024;
    }
  }
#endif
  cached_process_memory_bytes_ = resident;
  last_process_memory_read_ = now;
  return resident;
}

bool LuaAPI::load_ui_kit_runtime(lua_State *L)
{
  // The kit is split into per-surface modules (features/ui/*.lua): load them
  // first into package.loaded["jot_ui.*"], then run the orchestrator file
  // features/ui.lua which requires them and registers the handlers.
  if (!jot_lua::load_ui_kit_modules(L))
  {
    return false;
  }
  return load_bundled_lua_file(L, "features/ui.lua", "UI kit");
}

bool LuaAPI::load_markdown_runtime(lua_State *L)
{
  // The markdown preview feature is a module tree (features/markdown/*.lua):
  // pre-load each module into package.loaded["jot_md.*"], then run init.lua - // the only file that
  // executes, registering commands, autocmds and keymaps.
  static const char *kModules[] = {
      "features/markdown/config.lua",
      "features/markdown/inline.lua",
      "features/markdown/toc.lua",
      "features/markdown/block.lua",
      "features/markdown/assets.lua",
      "features/markdown/template.lua",
      "features/markdown/render.lua",
      "features/markdown/browser.lua",
      "features/markdown/sync.lua",
      "features/markdown/session.lua",
  };
  for (const char *rel : kModules)
  {
    if (!jot_lua::load_bundled_lua_module(L, rel, "jot_md"))
    {
      return false;
    }
  }
  return load_bundled_lua_file(L, "features/markdown/init.lua", "Markdown preview");
}

bool LuaAPI::load_html_runtime(lua_State *L)
{
  // The HTML preview shares the markdown preview's transport and browser
  // launcher (package.loaded["jot_md.browser"], loaded by
  // load_markdown_runtime, which api_bindings.cpp calls first), so only its own
  // two modules are loaded here before init.lua runs the registrations.
  static const char *kModules[] = {
      "features/html/config.lua",
      "features/html/preview.lua",
  };
  for (const char *rel : kModules)
  {
    if (!jot_lua::load_bundled_lua_module(L, rel, "jot_html"))
    {
      return false;
    }
  }
  return load_bundled_lua_file(L, "features/html/init.lua", "HTML preview");
}

bool LuaAPI::load_snippet_runtime(lua_State *L)
{
  // The snippet engine is a module tree (features/snippet/*.lua): pre-load each
  // module into package.loaded["jot_snip.*"] in dependency order, then run
  // init.lua - the only file that executes, registering commands, keymaps and
  // the LSP snippet hook.
  static const char *kModules[] = {
      "features/snippet/config.lua",
      "features/snippet/doc.lua",
      "features/snippet/env.lua",
      "features/snippet/nodes.lua",
      "features/snippet/parser.lua",
      "features/snippet/store.lua",
      "features/snippet/json.lua",
      "features/snippet/session.lua",
      "features/snippet/expand.lua",
      "features/snippet/loaders.lua",
      "features/snippet/keymaps.lua",
      "features/snippet/lsp.lua",
  };
  for (const char *rel : kModules)
  {
    if (!jot_lua::load_bundled_lua_module(L, rel, "jot_snip"))
    {
      return false;
    }
  }
  return load_bundled_lua_file(L, "features/snippet/init.lua", "Snippets");
}

bool LuaAPI::load_bible_runtime(lua_State *L)
{
  // The Bible reader's reference policy (features/bible/*.lua): a module tree
  // loaded into package.loaded["jot_bible.*"], then init.lua registers it with
  // jot.policy so the native reference parser falls back to it. Nothing here
  // fetches anything -- the transport stays in C++ (extras/bible_client.cpp).
  if (!jot_lua::load_bundled_lua_module(L, "features/bible/reference.lua", "jot_bible"))
  {
    return false;
  }
  return load_bundled_lua_file(L, "features/bible/init.lua", "Bible");
}

bool LuaAPI::load_settings_runtime(lua_State *L)
{
  // The settings menu's catalog (features/settings/*.lua): labels, row types,
  // enum choices and int ranges, registered with jot.policy so the menu asks
  // Lua first. Nothing here touches the menu itself -- the rows, the search bar
  // and the editing stay native (surfaces/settings.cpp).
  if (!jot_lua::load_bundled_lua_module(L, "features/settings/catalog.lua", "jot_settings"))
  {
    return false;
  }
  return load_bundled_lua_file(L, "features/settings/init.lua", "Settings");
}

bool LuaAPI::load_leetcode_runtime(lua_State *L)
{
  static const char *kModules[] = {
      "features/leetcode/json.lua",
      "features/leetcode/config.lua",
      "features/leetcode/cache.lua",
      "features/leetcode/client.lua",
      "features/leetcode/solution.lua",
      "features/leetcode/panel.lua",
      "features/leetcode/ui.lua",
  };
  for (const char *rel : kModules)
  {
    if (!jot_lua::load_bundled_lua_module(L, rel, "jot_lc"))
      return false;
  }
  return jot_lua::load_bundled_lua_file(L, "features/leetcode/init.lua", "LeetCode");
}

bool LuaAPI::load_ai_runtime(lua_State *L)
{
  // The AI assistant is a module tree (features/ai/*.lua) like the snippet
  // engine: pre-load each module into package.loaded["jot_ai.*"], then run
  // init.lua - the only file that executes, registering the commands, the
  // keymap family and the jot.ai surface.
  static const char *kModules[] = {
      "features/ai/config.lua",
      "features/ai/json.lua",
      "features/ai/http.lua",
      "features/ai/context.lua",
      "features/ai/chat.lua",
      "features/ai/inline.lua",
  };
  for (const char *rel : kModules)
  {
    if (!jot_lua::load_bundled_lua_module(L, rel, "jot_ai"))
    {
      return false;
    }
  }
  return load_bundled_lua_file(L, "features/ai/init.lua", "AI assistant");
}

// Recursively converts one native FileNode (and its children) into a Lua
// table - the exact tree the explorer sidebar renders.
