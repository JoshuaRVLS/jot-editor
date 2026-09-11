// LuaAPI runtime lifecycle: state cleanup, config and plugin loading,
// callback dispatch, the UI-kit runtime bootstrap, and process-memory
// reporting.
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

#include "jot/lua/embedded_lua.h"
#include "jot/lua/lua_loader.h"
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
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

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
  fs::path root;
  const char *env = getenv("JOT_CONFIG_HOME");
  if (env && *env)
    root = env;
  else
  {
    const char *home = getenv("HOME");
    const char *app = getenv("APPDATA");
    if (app && *app)
      root = fs::path(app) / "jot";
    else if (home)
      root = fs::path(home) / ".config" / "jot";
  }
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
  fs::path root;
  const char *env = getenv("JOT_CONFIG_HOME");
  if (env && *env)
    root = env;
  else
  {
    const char *home = getenv("HOME");
    const char *app = getenv("APPDATA");
    if (app && *app)
      root = fs::path(app) / "jot";
    else if (home)
      root = fs::path(home) / ".config" / "jot";
  }
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
void LuaAPI::reload_plugins()
{
  load_plugins();
  // Re-register the bundled inline-diagnostics autocmd, which
  // clear_runtime_state() inside load_plugins() wipes (same reason it is
  // loaded after load_plugins() at boot).
  if (lua_initialized)
  {
    jot_lua::load_bundled_lua_file(static_cast<lua_State *>(lua_state),
                                   "features/decorations.lua",
                                   "Decorations");
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
  // Linux /proc/self/statm: "size resident shared text lib data dt" — the
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
    // Fallback: peak RSS (KB on Linux/macOS) — close enough when statm is
    // unavailable (some sandboxes hide /proc).
    struct rusage usage
    {
    };
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
  // pre-load each module into package.loaded["jot_md.*"], then run init.lua —
  // the only file that executes, registering commands, autocmds and keymaps.
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

// Recursively converts one native FileNode (and its children) into a Lua
// table — the exact tree the explorer sidebar renders.

