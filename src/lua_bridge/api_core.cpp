#include "editor.h"
#include "features/syntax_highlighter.h"
#include "features/tree_sitter/manager.h"
#include "host_api.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "lua_bridge/embedded_lua.h"
#include "lua_bridge/lua_loader.h"
#include "tools/symbols/index.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <sys/wait.h>
#endif

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace fs = std::filesystem;

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
                                 int buffer)
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
  if (event == "BufChange" && last_edit_.valid)
  {
    lua_pushinteger(L, last_edit_.start_line);
    lua_setfield(L, -2, "edit_start_line");
    lua_pushinteger(L, last_edit_.start_col);
    lua_setfield(L, -2, "edit_start_col");
    lua_pushinteger(L, last_edit_.end_line);
    lua_setfield(L, -2, "edit_end_line");
    lua_pushinteger(L, last_edit_.end_col);
    lua_setfield(L, -2, "edit_end_col");
    lua_pushstring(L, last_edit_.inserted.c_str());
    lua_setfield(L, -2, "edit_inserted");
    lua_pushstring(L, last_edit_.removed.c_str());
    lua_setfield(L, -2, "edit_removed");
    lua_pushboolean(L, last_edit_.multiline);
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
void LuaAPI::on_buffer_open(const std::string &f)
{
  if (editor)
    editor->notify_lsp_open(f);
  fire_autocmd("BufOpen", f, -1);
  emit_buffer_event("buffer.open", f);
}
static void lua_split_edit_lines(const std::string &text, std::vector<std::string> &out)
{
  size_t start = 0;
  for (size_t i = 0; i < text.size(); i++)
  {
    if (text[i] == '\n')
    {
      out.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  out.push_back(text.substr(start));
}

static size_t lua_common_prefix_bytes(const std::string &a, const std::string &b)
{
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n && a[i] == b[i])
    i++;
  return i;
}

static size_t lua_common_suffix_bytes(const std::string &a, const std::string &b)
{
  const size_t na = a.size(), nb = b.size();
  size_t i = 0;
  while (i < na && i < nb && a[na - 1 - i] == b[nb - 1 - i])
    i++;
  return i;
}

// Joins lines[first..last] (inclusive). The first line loses its leading
// cut_left bytes; the last line is truncated to its first end_col bytes.
static std::string lua_join_edit_range(const std::vector<std::string> &lines,
                                       size_t first,
                                       size_t last,
                                       size_t cut_left,
                                       size_t end_col)
{
  std::string out;
  if (first > last || first >= lines.size())
    return out;
  for (size_t i = first; i <= last && i < lines.size(); i++)
  {
    const std::string &line = lines[i];
    size_t from = 0;
    size_t to = line.size();
    if (i == first)
      from = std::min(cut_left, line.size());
    if (i == last)
    {
      to = std::min(end_col, line.size());
      if (i == first && to < from)
        to = from;
    }
    if (i > first)
      out += '\n';
    if (to > from)
      out.append(line, from, to - from);
  }
  return out;
}

// Computes a best-effort line/column delta between two versions of a buffer
// using common-prefix/common-suffix line matching. Exact for single-line
// typing/paste/delete edits and block inserts; approximations suffice for
// anything more exotic (undo spans, merges).
static LuaEditDelta compute_edit_delta(const std::string &prev, const std::string &now)
{
  LuaEditDelta d;
  if (prev == now)
    return d;
  std::vector<std::string> a, b;
  lua_split_edit_lines(prev, a);
  lua_split_edit_lines(now, b);
  size_t ai = 0, bi = 0;
  while (ai < a.size() && bi < b.size() && a[ai] == b[bi])
  {
    ai++;
    bi++;
  }
  size_t aend = a.size(), bend = b.size();
  while (aend > ai && bend > bi && a[aend - 1] == b[bend - 1])
  {
    aend--;
    bend--;
  }
  if (ai == aend && bi == bend)
    return d;

  const std::string &old_first = ai < a.size() ? a[ai] : std::string();
  const std::string &new_first = bi < b.size() ? b[bi] : std::string();
  const size_t p = lua_common_prefix_bytes(old_first, new_first);
  const bool old_has = aend > ai;
  const bool new_has = bend > bi;
  size_t suffix = 0;
  if (old_has && new_has)
  {
    suffix = lua_common_suffix_bytes(a[aend - 1], b[bend - 1]);
    // Single-line changes must not double-count the common middle.
    if (aend - ai == 1 && bend - bi == 1)
    {
      suffix = std::min(suffix, std::min(a[ai].size(), b[bi].size()) - p);
    }
  }
  const size_t old_end = old_has && a[aend - 1].size() >= suffix ? a[aend - 1].size() - suffix : 0;
  const size_t new_end = new_has && b[bend - 1].size() >= suffix ? b[bend - 1].size() - suffix : 0;

  d.start_line = (int)bi + 1;
  d.start_col = (int)p + 1;
  d.multiline = (aend - ai > 1) || (bend - bi > 1);
  if (new_has)
  {
    d.end_line = (int)bend;       // exclusive end line (1-based, one past last)
    d.end_col = (int)new_end + 1; // exclusive column on the last line
    d.inserted = lua_join_edit_range(b, bi, bend - 1, p, new_end);
  }
  else
  {
    d.end_line = d.start_line;
    d.end_col = d.start_col;
  }
  if (old_has)
  {
    d.removed = lua_join_edit_range(a, ai, aend - 1, p, old_end);
  }
  if (d.inserted.find('\n') != std::string::npos || d.removed.find('\n') != std::string::npos)
  {
    d.multiline = true;
  }
  d.valid = true;
  return d;
}

void LuaAPI::on_buffer_change(const std::string &f, const std::string &)
{
  last_edit_ = LuaEditDelta{};
  if (editor)
  {
    editor->notify_lsp_change(f);
    // The persistent Outline panel rebuilds lazily from this dirty flag.
    editor->note_outline_edit();
  }
  int index = -1;
  if (editor)
  {
    for (size_t i = 0; i < editor->buffers.size(); i++)
    {
      if (editor->buffers[i].filepath == f)
      {
        index = (int)i;
        break;
      }
    }
    if (index < 0 && !f.empty())
    {
      std::error_code ec;
      for (size_t i = 0; i < editor->buffers.size() && index < 0; i++)
      {
        const std::string &other = editor->buffers[i].filepath;
        if (!other.empty() && fs::exists(other, ec) && fs::exists(f, ec)
            && fs::equivalent(other, f, ec))
        {
          index = (int)i;
        }
      }
    }
    if (index < 0 && f.empty() && editor->current_buffer >= 0
        && editor->current_buffer < (int)editor->buffers.size())
    {
      index = editor->current_buffer;
    }
  }
  // Compute a rich edit delta only when a plugin actually listens to
  // BufChange, and skip huge/lazy buffers to keep the typing hot path cheap.
  if (index >= 0 && index < (int)editor->buffers.size())
  {
    bool has_listener = false;
    for (const auto &x : plugin_autocmds)
    {
      if (x.event == "BufChange")
      {
        has_listener = true;
        break;
      }
    }
    if (has_listener)
    {
      FileBuffer &buf = editor->buffers[(size_t)index];
      if (!buf.is_lazy() && buf.line_count() <= 100000)
      {
        std::string cur;
        for (size_t i = 0; i < buf.lines.size(); i++)
        {
          if (i)
            cur += '\n';
          cur += buf.lines[i];
        }
        // Unnamed buffers share an empty filepath; key them by index.
        const std::string key = f.empty() ? ("\x01" + std::to_string(index)) : f;
        auto it = edit_snapshots_.find(key);
        if (it != edit_snapshots_.end())
        {
          last_edit_ = compute_edit_delta(it->second, cur);
        }
        edit_snapshots_[key] = std::move(cur);
        while (edit_snapshots_.size() > 256)
        {
          edit_snapshots_.erase(edit_snapshots_.begin());
        }
      }
    }
  }
  fire_autocmd("BufChange", f, index);
  last_edit_ = LuaEditDelta{};
}
void LuaAPI::on_buffer_save(const std::string &f)
{
  if (editor)
    editor->notify_lsp_save(f);
  fire_autocmd("BufSave", f, -1);
  emit_buffer_event("buffer.save", f);
}
void LuaAPI::register_command(const std::string &n, const std::string &c, const std::string &d)
{
  if (n.empty() || c.empty())
    return;
  for (auto &x : plugin_commands)
    if (x.name == lower(n))
    {
      x.callback = c;
      x.detail = d;
      return;
    }
  plugin_commands.push_back({lower(n), c, d});
}
bool LuaAPI::run_plugin_command(const std::string &n, const std::string &a)
{
  for (auto &x : plugin_commands)
    if (x.name == n)
    {
      call_callback_string(x.callback, a);
      return true;
    }
  return false;
}
bool LuaAPI::run_plugin_keymap(const std::string &k, const std::string &m)
{
  for (auto &x : plugin_keymaps)
    if (x.key == key_name(k) && (x.mode == "global" || x.mode == m))
    {
      if (!x.command.empty() && editor)
      {
        if (!x.command.empty() && x.command[0] == ':')
          editor->execute_ex_command(x.command);
        else if (editor->host_api)
          editor->host_api->io.execute_command(x.command);
      }
      else
        call_callback_string(x.callback, "");
      return true;
    }
  return false;
}
void LuaAPI::fire_autocmd(const std::string &e, const std::string &f, int b)
{
  for (auto &x : plugin_autocmds)
    if (x.event == e)
      call_callback_event(x.callback, e, f, b);
}
void LuaAPI::register_keymap(const std::string &k,
                             const std::string &c,
                             const std::string &cmd,
                             const std::string &d,
                             const std::string &m)
{
  plugin_keymaps.push_back({key_name(k), c, cmd, d, m});
}
void LuaAPI::register_autocmd(const std::string &e, const std::string &c)
{
  plugin_autocmds.push_back({e, c});
}
void LuaAPI::register_panel(const std::string &n, const std::string &c, const std::string &t)
{
  plugin_panels.push_back({n, c, t});
}
bool LuaAPI::run_plugin_callback(const std::string &c, const std::string &a)
{
  return call_callback_string(c, a);
}
std::string LuaAPI::get_current_buffer()
{
  return editor && editor->host_api ? editor->host_api->core.buffer_content() : "";
}
void LuaAPI::set_current_buffer(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_buffer_content(s);
}
std::string LuaAPI::get_selection()
{
  return editor && editor->host_api ? editor->host_api->core.selected_text() : "";
}
void LuaAPI::replace_selection(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.replace_selection(s);
}
void LuaAPI::insert_text(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->core.insert_text(s);
}
std::pair<int, int> LuaAPI::get_cursor()
{
  return editor && editor->host_api ? editor->host_api->core.cursor() : std::pair<int, int>{0, 0};
}
void LuaAPI::set_cursor(int l, int c)
{
  if (editor && editor->host_api)
    editor->host_api->core.set_cursor(l, c);
}
std::string LuaAPI::current_file()
{
  return editor && editor->host_api ? editor->host_api->core.current_file() : "";
}
void LuaAPI::open_file(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.open_file(s);
}
void LuaAPI::save_current_file()
{
  if (editor && editor->host_api)
    editor->host_api->io.save_current_file();
}
void LuaAPI::execute_command(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.execute_command(s);
}
void LuaAPI::run_job(const std::string &a, const std::string &b, const std::string &c)
{
  if (editor && editor->host_api)
    editor->host_api->io.run_job(a, b, c);
}

bool LuaAPI::run_job_capture(const std::string &command,
                             const std::string &cwd,
                             const std::string &callback)
{
  if (!editor || !editor->task_queue_ || command.empty())
    return false;
  std::string full = command;
  if (!cwd.empty())
    full = "cd " + lua_shell_quote(cwd) + " && " + command;
  return editor->task_queue_->submit_val<std::pair<std::string, int>>(
      [full]() { return lua_capture_shell(full); },
      [this, callback](std::pair<std::string, int> res)
      {
        if (!editor || !editor->running)
          return;
        deliver_job_result(callback, res.first, res.second);
      });
}

bool LuaAPI::deliver_job_result(const std::string &callback,
                                const std::string &output,
                                int exit_code)
{
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
  {
    return false;
  }
  auto it = lua_callbacks.find(callback);
  if (it == lua_callbacks.end())
    return false;
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  int ref = it->second;
  lua_callbacks.erase(it);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  lua_newtable(L);
  lua_pushstring(L, output.c_str());
  lua_setfield(L, -2, "output");
  lua_pushinteger(L, exit_code);
  lua_setfield(L, -2, "exit_code");
  lua_pushboolean(L, exit_code == 0);
  lua_setfield(L, -2, "ok");
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua job callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  return true;
}

int LuaAPI::resolve_buffer_arg(lua_State *L, int arg_index)
{
  if (!editor || editor->buffers.empty())
    return -1;
  int idx = -1;
  if (arg_index <= lua_gettop(L) && !lua_isnil(L, arg_index))
  {
    if (lua_isnumber(L, arg_index))
    {
      idx = (int)lua_tointeger(L, arg_index) - 1;
    }
    else if (lua_isstring(L, arg_index))
    {
      std::string path = lua_tostring(L, arg_index);
      for (size_t i = 0; i < editor->buffers.size(); i++)
      {
        if (editor->buffers[i].filepath == path)
        {
          idx = (int)i;
          break;
        }
      }
    }
  }
  else
  {
    idx = editor->current_buffer;
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
    return -1;
  return idx;
}

void LuaAPI::push_diagnostics(lua_State *L)
{
  int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  int n = 1;
  for (const auto &d : buf.diagnostics)
  {
    lua_newtable(L);
    lua_pushinteger(L, d.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, d.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, d.end_line + 1);
    lua_setfield(L, -2, "end_line");
    lua_pushinteger(L, d.end_col + 1);
    lua_setfield(L, -2, "end_col");
    lua_pushinteger(L, d.severity);
    lua_setfield(L, -2, "severity");
    lua_pushstring(L, lua_diag_severity_name(d.severity));
    lua_setfield(L, -2, "severity_name");
    lua_pushstring(L, d.message.c_str());
    lua_setfield(L, -2, "message");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::set_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int t = lua_type(L, 2);
  if (t != LUA_TSTRING && t != LUA_TNUMBER && t != LUA_TBOOLEAN)
  {
    luaL_error(L, "set_var value must be a string, number, or boolean");
    return;
  }
  lua_pushvalue(L, 2);
  const char *s = lua_tostring(L, -1);
  std::string value = s ? s : "";
  lua_pop(L, 1);
  int idx = resolve_buffer_arg(L, 3);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars[name] = std::move(value);
}

void LuaAPI::push_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  auto it = editor->buffers[(size_t)idx].lua_vars.find(name);
  if (it == editor->buffers[(size_t)idx].lua_vars.end())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, it->second.c_str());
}

void LuaAPI::delete_buffer_var(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  int idx = resolve_buffer_arg(L, 2);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].lua_vars.erase(name);
}

void LuaAPI::set_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (!editor || editor->buffers.empty() || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    lua_pushboolean(L, 0);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
  if (std::islower((unsigned char)c))
  {
    buf.marks[c] = buf.cursor;
    lua_pushboolean(L, 1);
    return;
  }
  if (std::isupper((unsigned char)c))
  {
    if (buf.filepath.empty())
    {
      lua_pushboolean(L, 0);
      return;
    }
    GlobalMark gm;
    gm.filepath = buf.filepath;
    gm.line = buf.cursor.y;
    gm.col = buf.cursor.x;
    editor->global_marks[c] = gm;
    lua_pushboolean(L, 1);
    return;
  }
  lua_pushboolean(L, 0);
}

void LuaAPI::push_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushnil(L);
    return;
  }
  char c = s[0];
  if (std::islower((unsigned char)c))
  {
    if (!editor || editor->buffers.empty() || editor->current_buffer < 0
        || editor->current_buffer >= (int)editor->buffers.size())
    {
      lua_pushnil(L);
      return;
    }
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    auto it = buf.marks.find(c);
    if (it == buf.marks.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_newtable(L);
    lua_pushinteger(L, editor->current_buffer + 1);
    lua_setfield(L, -2, "buffer");
    lua_pushstring(L, buf.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, it->second.y + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, it->second.x + 1);
    lua_setfield(L, -2, "col");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "global");
    return;
  }
  if (std::isupper((unsigned char)c) && editor)
  {
    auto it = editor->global_marks.find(c);
    if (it == editor->global_marks.end())
    {
      lua_pushnil(L);
      return;
    }
    lua_newtable(L);
    lua_pushstring(L, it->second.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, it->second.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, it->second.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "global");
    return;
  }
  lua_pushnil(L);
}

void LuaAPI::delete_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (std::islower((unsigned char)c) && editor && editor->current_buffer >= 0
      && editor->current_buffer < (int)editor->buffers.size())
  {
    lua_pushboolean(L, editor->buffers[(size_t)editor->current_buffer].marks.erase(c) > 0);
    return;
  }
  if (std::isupper((unsigned char)c) && editor)
  {
    lua_pushboolean(L, editor->global_marks.erase(c) > 0);
    return;
  }
  lua_pushboolean(L, 0);
}

void LuaAPI::jump_mark(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  if (!s || s[1] != '\0' || !std::isalpha((unsigned char)s[0]))
  {
    lua_pushboolean(L, 0);
    return;
  }
  char c = s[0];
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  int target_line = -1, target_col = 0;
  std::string target_path;
  if (std::islower((unsigned char)c))
  {
    if (editor->current_buffer < 0 || editor->current_buffer >= (int)editor->buffers.size())
    {
      lua_pushboolean(L, 0);
      return;
    }
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    auto it = buf.marks.find(c);
    if (it == buf.marks.end())
    {
      lua_pushboolean(L, 0);
      return;
    }
    target_line = it->second.y;
    target_col = it->second.x;
    target_path = buf.filepath;
  }
  else if (std::isupper((unsigned char)c))
  {
    auto it = editor->global_marks.find(c);
    if (it == editor->global_marks.end())
    {
      lua_pushboolean(L, 0);
      return;
    }
    target_line = it->second.line;
    target_col = it->second.col;
    target_path = it->second.filepath;
  }
  else
  {
    lua_pushboolean(L, 0);
    return;
  }
  // Ensure the owning buffer is open and current.
  int idx = -1;
  for (size_t i = 0; i < editor->buffers.size(); i++)
  {
    if (editor->buffers[i].filepath == target_path)
    {
      idx = (int)i;
      break;
    }
  }
  if (idx < 0)
  {
    if (target_path.empty())
    {
      lua_pushboolean(L, 0);
      return;
    }
    editor->open_file(target_path);
    for (size_t i = 0; i < editor->buffers.size(); i++)
    {
      if (editor->buffers[i].filepath == target_path)
      {
        idx = (int)i;
        break;
      }
    }
  }
  if (idx < 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  if (editor->current_buffer != idx)
  {
    editor->host_api->core.switch_buffer(idx);
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.line_count() == 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  buf.cursor.y = std::clamp(target_line, 0, (int)buf.line_count() - 1);
  buf.cursor.x = std::clamp(target_col, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  editor->clear_selection();
  editor->ensure_cursor_visible();
  editor->needs_redraw = true;
  lua_pushboolean(L, 1);
}

void LuaAPI::push_mark_list(lua_State *L)
{
  lua_newtable(L);
  int n = 1;
  if (!editor)
    return;
  if (editor->current_buffer >= 0 && editor->current_buffer < (int)editor->buffers.size())
  {
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    for (const auto &kv : buf.marks)
    {
      lua_newtable(L);
      lua_pushlstring(L, &kv.first, 1);
      lua_setfield(L, -2, "name");
      lua_pushboolean(L, 0);
      lua_setfield(L, -2, "global");
      lua_pushinteger(L, editor->current_buffer + 1);
      lua_setfield(L, -2, "buffer");
      lua_pushstring(L, buf.filepath.c_str());
      lua_setfield(L, -2, "path");
      lua_pushinteger(L, kv.second.y + 1);
      lua_setfield(L, -2, "line");
      lua_pushinteger(L, kv.second.x + 1);
      lua_setfield(L, -2, "col");
      lua_rawseti(L, -2, n++);
    }
  }
  for (const auto &kv : editor->global_marks)
  {
    lua_newtable(L);
    lua_pushlstring(L, &kv.first, 1);
    lua_setfield(L, -2, "name");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "global");
    lua_pushstring(L, kv.second.filepath.c_str());
    lua_setfield(L, -2, "path");
    lua_pushinteger(L, kv.second.line + 1);
    lua_setfield(L, -2, "line");
    lua_pushinteger(L, kv.second.col + 1);
    lua_setfield(L, -2, "col");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::register_status_segment(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_getfield(L, 2, "text");
  if (!lua_isfunction(L, -1))
  {
    luaL_error(L, "status segment requires a 'text' function");
    return;
  }
  PluginStatusSegment seg;
  seg.name = name;
  lua_pushvalue(L, -1);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  seg.callback = "status." + std::to_string(ref);
  lua_callbacks[seg.callback] = ref;
  lua_pop(L, 1); // pop the fetched 'text' value (consumed by luaL_ref)
  seg.side = table_string(L, 2, "side", "right");
  seg.priority = table_int(L, 2, "priority", 50);
  seg.fg = table_int(L, 2, "fg", -1);
  for (auto it = status_segments_.begin(); it != status_segments_.end(); ++it)
  {
    if (it->name != seg.name)
      continue;
    auto old = lua_callbacks.find(it->callback);
    if (old != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, old->second);
      lua_callbacks.erase(old);
    }
    status_segments_.erase(it);
    break;
  }
  status_segments_.push_back(std::move(seg));
}

void LuaAPI::unregister_status_segment(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  for (auto it = status_segments_.begin(); it != status_segments_.end(); ++it)
  {
    if (it->name != name)
      continue;
    auto old = lua_callbacks.find(it->callback);
    if (old != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, old->second);
      lua_callbacks.erase(old);
    }
    status_segments_.erase(it);
    break;
  }
}

std::vector<RenderedStatusSegment> LuaAPI::render_status_segments()
{
  std::vector<RenderedStatusSegment> out;
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread()
      || status_segments_.empty())
  {
    return out;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  int top = lua_gettop(L);
  for (const auto &seg : status_segments_)
  {
    auto it = lua_callbacks.find(seg.callback);
    if (it == lua_callbacks.end())
      continue;
    lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
    lua_pushstring(L, seg.name.c_str());
    if (lua_pcall(L, 1, 2, 0) != LUA_OK)
    {
      std::cerr << "Lua status segment error (" << seg.name << "): " << lua_tostring(L, -1) << "\n";
      lua_settop(L, top);
      continue;
    }
    RenderedStatusSegment rs;
    rs.side = seg.side;
    rs.priority = seg.priority;
    rs.fg = seg.fg;
    const char *text = lua_tostring(L, -2);
    if (text)
      rs.text = text;
    if (lua_isnumber(L, -1))
    {
      int f = (int)lua_tointeger(L, -1);
      if (f >= 0 && f <= 255)
        rs.fg = f;
    }
    lua_settop(L, top);
    if (!rs.text.empty())
      out.push_back(std::move(rs));
  }
  lua_settop(L, top);
  return out;
}
void LuaAPI::show_picker(const std::string &a, const std::string &b, const std::string &c)
{
  if (editor && editor->host_api)
    editor->host_api->io.show_plugin_picker(a, b, c);
}
void LuaAPI::show_panel(const std::string &s)
{
  if (editor && editor->host_api)
    editor->host_api->io.show_plugin_panel(s);
}
std::vector<std::string> LuaAPI::plugin_panel_lines(const std::string &name)
{
  for (auto &p : plugin_panels)
    if (p.name == name)
    {
      auto i = lua_callbacks.find(p.callback);
      if (i == lua_callbacks.end())
        return {};
      lua_State *L = static_cast<lua_State *>(lua_state);
      lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
      lua_pushstring(L, name.c_str());
      if (lua_pcall(L, 1, 1, 0))
      {
        lua_pop(L, 1);
        return {};
      }
      std::vector<std::string> out;
      if (lua_istable(L, -1))
      {
        lua_pushnil(L);
        while (lua_next(L, -2))
        {
          out.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);
      return out;
    }
  return {};
}
std::vector<std::string> LuaAPI::plugin_picker_items(const std::string &callback)
{
  auto i = lua_callbacks.find(callback);
  if (i == lua_callbacks.end())
    return {};
  lua_State *L = static_cast<lua_State *>(lua_state);
  lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
  lua_pushstring(L, "");
  if (lua_pcall(L, 1, 1, 0))
  {
    lua_pop(L, 1);
    return {};
  }
  std::vector<std::string> out;
  if (lua_istable(L, -1))
  {
    lua_pushnil(L);
    while (lua_next(L, -2))
    {
      out.push_back(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  return out;
}

// ---------------------------------------------------------------------------
// Extended native surface (jot.config / jot.git / jot.tasks / jot.symbols /
// jot.debugger / jot.editor / jot.theme). Every function below reads or acts
// on live Editor state directly, so Lua plugins get first-class access to the
// same native capabilities the UI uses — no C++ rebuild needed for features.

void LuaAPI::push_task_list(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  editor->load_terminal_tasks();
  int n = 1;
  for (const auto &task : editor->terminal_tasks)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", task.name);
    lua_push_str_field(L, "command", task.command);
    lua_push_str_field(L, "cwd", task.cwd);
    lua_push_str_field(L, "source", task.source_kind);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::run_task_from_lua(lua_State *L)
{
  const char *name = luaL_optstring(L, 1, "");
  const bool force_new = lua_toboolean(L, 2);
  lua_pushboolean(L, editor && editor->run_terminal_task(name, force_new));
}

void LuaAPI::rerun_task_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->rerun_last_terminal_task());
}

void LuaAPI::push_symbols(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy() || buf.filepath.empty() || buf.line_count() == 0)
    return;
  const std::vector<SymbolMatch> symbols =
      SymbolIndex::extract_document_symbols(buf.lines, buf.filepath);
  int n = 1;
  for (const auto &s : symbols)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", s.name);
    lua_push_str_field(L, "kind", s.kind);
    lua_push_str_field(L, "detail", s.detail);
    lua_push_int_field(L, "line", (long long)s.line + 1);
    lua_push_int_field(L, "column", (long long)s.column + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_debugger_configs(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  editor->load_debugger_configs();
  int n = 1;
  for (const auto &cfg : editor->debugger_configs)
  {
    lua_newtable(L);
    lua_push_str_field(L, "name", cfg.name);
    lua_push_str_field(L, "program", cfg.program);
    lua_push_str_field(L, "adapter", cfg.adapter);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::run_debugger_config_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->run_debugger_config(luaL_checkstring(L, 1)));
}

void LuaAPI::push_buffer_current(lua_State *L)
{
  if (!editor || editor->current_buffer < 0
      || editor->current_buffer >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushinteger(L, editor->current_buffer + 1);
}

void LuaAPI::push_buffer_count(lua_State *L)
{
  lua_pushinteger(L, editor ? (long long)editor->buffers.size() : 0);
}

void LuaAPI::push_buffer_text(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  std::string text;
  for (size_t i = 0; i < buf.lines.size(); i++)
  {
    if (i)
      text += '\n';
    text += buf.lines[i];
  }
  lua_pushlstring(L, text.data(), text.size());
}

void LuaAPI::push_buffer_meta(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_str_field(L, "path", buf.filepath);
  std::string name;
  if (!buf.filepath.empty())
  {
    name = fs::path(buf.filepath).filename().string();
  }
  lua_push_str_field(L, "name", name);
  lua_push_bool_field(L, "modified", buf.modified);
  lua_push_int_field(L, "line_count", (long long)buf.line_count());
}

void LuaAPI::push_buffer_selection(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_newtable(L);
  lua_push_bool_field(L, "active", buf.selection.active);
  lua_push_int_field(L, "start_line", (long long)buf.selection.start.y + 1);
  lua_push_int_field(L, "start_col", (long long)buf.selection.start.x + 1);
  lua_push_int_field(L, "end_line", (long long)buf.selection.end.y + 1);
  lua_push_int_field(L, "end_col", (long long)buf.selection.end.x + 1);
}

void LuaAPI::push_buffer_bookmarks(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const int line : buf.bookmarks)
  {
    lua_pushinteger(L, line + 1);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_buffer_folds(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  int n = 1;
  for (const FoldRange &r : buf.fold_ranges)
  {
    lua_newtable(L);
    lua_push_int_field(L, "start", (long long)r.start_line + 1);
    lua_push_int_field(L, "end", (long long)r.end_line + 1);
    lua_push_bool_field(L, "collapsed", r.collapsed);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::clipboard_copy_from_lua(lua_State *L)
{
  if (editor)
    editor->copy();
}
void LuaAPI::clipboard_cut_from_lua(lua_State *L)
{
  if (editor)
    editor->cut();
}
void LuaAPI::clipboard_paste_from_lua(lua_State *L)
{
  if (editor)
    editor->paste();
}

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

void LuaAPI::push_workspace_path(lua_State *L)
{
  if (!editor || editor->root_dir.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->root_dir.c_str());
}

void LuaAPI::push_recent_files(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_files)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_recent_workspaces(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &p : editor->recent_workspaces)
  {
    lua_pushstring(L, p.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::popup_from_lua(lua_State *L)
{
  if (!editor)
    return;
  editor->show_popup(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
}

// Canonical token-type names, indexed by the shared TreeSitterTokenKind ids
// used by both the regex highlighter and the tree-sitter query engine.
static const char *const kLuaTokenNames[] = {"none",                   // 0
                                             "keyword",                // 1
                                             "string",                 // 2
                                             "comment",                // 3
                                             "number",                 // 4
                                             "type",                   // 5
                                             "function",               // 6
                                             "variable",               // 7
                                             "constant",               // 8
                                             "builtin",                // 9
                                             "operator",               // 10
                                             "punctuation",            // 11
                                             "tag",                    // 12
                                             "attribute",              // 13
                                             "namespace",              // 14
                                             "module",                 // 15
                                             "parameter",              // 16
                                             "field",                  // 17
                                             "keyword.control",        // 18
                                             "keyword.storage",        // 19
                                             "keyword.preproc",        // 20
                                             "function.method",        // 21
                                             "function.constructor",   // 22
                                             "type.builtin",           // 23
                                             "constant.macro",         // 24
                                             "string.escape",          // 25
                                             "punctuation.bracket",    // 26
                                             "punctuation.delimiter"}; // 27

// Resolve a token id to the active theme color, mirroring the renderer's own
// mapping so Lua callers see the exact highlight color on screen.
static int lua_syntax_fg(const Theme &theme, int token)
{
  switch (token)
  {
  case 1:
    return theme.fg_keyword;
  case 2:
    return theme.fg_string;
  case 3:
    return theme.fg_comment;
  case 4:
    return theme.fg_number;
  case 5:
    return theme.fg_type;
  case 6:
    return theme.fg_function;
  case 7:
    return theme.fg_variable;
  case 8:
    return theme.fg_constant;
  case 9:
    return theme.fg_builtin;
  case 10:
    return theme.fg_operator;
  case 11:
    return theme.fg_punctuation;
  case 12:
    return theme.fg_tag;
  case 13:
    return theme.fg_attribute;
  case 14:
    return theme.fg_namespace;
  case 15:
    return theme.fg_module;
  case 16:
    return theme.fg_parameter;
  case 17:
    return theme.fg_field;
  case 18:
    return theme.fg_keyword_control;
  case 19:
    return theme.fg_keyword_storage;
  case 20:
    return theme.fg_keyword_preproc;
  case 21:
    return theme.fg_function_method;
  case 22:
    return theme.fg_function_constructor;
  case 23:
    return theme.fg_type_builtin;
  case 24:
    return theme.fg_constant_macro;
  case 25:
    return theme.fg_string_escape;
  case 26:
    return theme.fg_punctuation_bracket;
  case 27:
    return theme.fg_punctuation_delimiter;
  default:
    return theme.fg_command;
  }
}

void LuaAPI::push_search_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_bool_field(L, "visible", editor->show_search);
  lua_push_str_field(L, "query", editor->search_query);
  lua_push_bool_field(L, "case_sensitive", editor->search_case_sensitive);
  lua_push_bool_field(L, "whole_word", editor->search_whole_word);
  lua_push_bool_field(L, "regex", editor->search_regex);
  lua_push_bool_field(L, "replace", editor->search_replace_visible);
  lua_push_str_field(L, "replace_text", editor->search_replace_text);
  lua_push_bool_field(L, "scoped", editor->search_scoped_to_selection);
  lua_push_int_field(L, "result_count", (long long)editor->search_results.size());
  lua_push_int_field(L,
                     "result_index",
                     editor->search_result_index >= 0 ? (long long)editor->search_result_index + 1
                                                      : 0);
}

void LuaAPI::push_search_matches(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const SearchMatch &m : editor->search_results)
  {
    lua_newtable(L);
    lua_push_int_field(L, "line", (long long)m.line + 1);
    lua_push_int_field(L, "column", (long long)m.col + 1);
    lua_push_int_field(L, "len", m.len);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_picker_active(lua_State *L)
{
  lua_pushboolean(L, editor && editor->show_quick_pick);
}

void LuaAPI::push_picker_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  lua_push_str_field(L, "title", editor->quick_pick_title);
  lua_push_str_field(L, "query", editor->quick_pick_query);
  lua_push_int_field(L, "selected", (long long)editor->quick_pick_selected + 1);
  lua_push_int_field(L, "visible", (long long)editor->quick_pick_items.size());
  lua_push_int_field(L, "total", (long long)editor->quick_pick_all_items.size());
}

void LuaAPI::push_picker_items(lua_State *L)
{
  lua_newtable(L);
  if (!editor || !editor->show_quick_pick)
    return;
  int n = 1;
  for (const QuickPickItem &item : editor->quick_pick_items)
  {
    lua_newtable(L);
    lua_push_str_field(L, "label", item.label);
    lua_push_str_field(L, "detail", item.detail);
    lua_push_str_field(L, "preview", item.preview);
    lua_push_str_field(L, "filepath", item.filepath);
    lua_push_int_field(L, "line", item.line >= 0 ? (long long)item.line + 1 : 0);
    lua_push_int_field(L, "column", item.col >= 0 ? (long long)item.col + 1 : 0);
    lua_push_int_field(L, "severity", item.severity);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::picker_accept_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->accept_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::picker_close_from_lua(lua_State *L)
{
  if (!editor || !editor->show_quick_pick)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->close_quick_pick();
  lua_pushboolean(L, 1);
}

void LuaAPI::push_buffer_tokens(lua_State *L)
{
  if (!editor)
  {
    lua_newtable(L);
    return;
  }
  // Read args BEFORE pushing the result table: lua_newtable shifts the stack,
  // so the optional buffer-id argument would otherwise be masked by the table
  // itself (never nil), making resolve_buffer_arg fail and this always return
  // an empty token list.
  const int line_no = (int)luaL_checkinteger(L, 1) - 1;
  int idx = editor->current_buffer;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
  {
    idx = resolve_buffer_arg(L, 2);
  }
  if (idx < 0 || idx >= (int)editor->buffers.size())
  {
    lua_newtable(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line_no < 0 || line_no >= (int)buf.line_count())
  {
    lua_newtable(L);
    return;
  }
  lua_newtable(L);
  const auto &colors = editor->get_line_syntax_colors(buf, line_no);
  const std::string &text = buf.line(line_no);
  const size_t n_colors = colors.size();
  int n = 1;
  size_t i = 0;
  while (i < n_colors)
  {
    const int token = colors[i].first == 1 ? colors[i].second : 0;
    size_t j = i + 1;
    while (j < n_colors)
    {
      const int next = colors[j].first == 1 ? colors[j].second : 0;
      if (next != token)
        break;
      j++;
    }
    if (token > 0 && token < 28)
    {
      const size_t span_end = std::min(j, (size_t)std::max(0, (int)text.size()));
      if (i < span_end)
      {
        lua_newtable(L);
        lua_push_int_field(L, "start", (long long)i);
        lua_push_int_field(L, "end", (long long)span_end);
        lua_push_int_field(L, "token", token);
        lua_push_str_field(L, "name", kLuaTokenNames[token]);
        lua_push_str_field(L, "text", text.substr(i, span_end - i));
        lua_push_int_field(L, "fg", lua_syntax_fg(editor->theme, token));
        lua_rawseti(L, -2, n++);
      }
    }
    i = j;
  }
}

// Runs a one-shot Lua callback (registered under `id` in `callbacks`) with a
// single table argument built by `build`. Returns true when the callback
// existed and ran.

bool LuaAPI::load_ui_kit_runtime(lua_State *L)
{
  return load_bundled_lua_file(L, "features/ui.lua", "UI kit");
}

// Recursively converts one native FileNode (and its children) into a Lua
// table — the exact tree the explorer sidebar renders.

void LuaAPI::push_debugger_state(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  const size_t sessions =
      std::min(editor->debugger_sessions.size(), editor->debugger_session_state.size());
  for (size_t i = 0; i < sessions; i++)
  {
    const DebuggerSessionState &st = editor->debugger_session_state[i];
    lua_newtable(L);
    lua_push_str_field(L, "name", st.name);
    lua_push_str_field(L, "adapter", st.adapter);
    lua_push_str_field(L, "program", st.program);
    lua_push_bool_field(L, "running", st.running);
    lua_push_bool_field(L, "stopped", st.stopped);
    lua_push_int_field(L, "active_thread_id", st.active_thread_id);
    lua_push_int_field(L, "active_frame_id", st.active_frame_id);
    lua_push_str_field(L, "output", st.output);
    lua_push_str_field(L, "last_error", st.last_error);
    lua_newtable(L);
    int tn = 1;
    for (const DebuggerThread &th : st.threads)
    {
      lua_newtable(L);
      lua_push_int_field(L, "id", th.id);
      lua_push_str_field(L, "name", th.name);
      lua_newtable(L);
      int fn = 1;
      for (const DebuggerFrame &fr : th.frames)
      {
        lua_newtable(L);
        lua_push_int_field(L, "id", fr.id);
        lua_push_str_field(L, "name", fr.name);
        lua_push_str_field(L, "path", fr.filepath);
        lua_push_int_field(L, "line", (long long)fr.line + 1);
        lua_push_int_field(L, "column", (long long)fr.column + 1);
        lua_rawseti(L, -2, fn++);
      }
      lua_setfield(L, -2, "frames");
      lua_rawseti(L, -2, tn++);
    }
    lua_setfield(L, -2, "threads");
    lua_newtable(L);
    int vn = 1;
    for (const DebuggerVariable &v : st.variables)
    {
      lua_newtable(L);
      lua_push_str_field(L, "name", v.name);
      lua_push_str_field(L, "value", v.value);
      lua_push_str_field(L, "type", v.type);
      lua_push_int_field(L, "variables_reference", v.variables_reference);
      lua_rawseti(L, -2, vn++);
    }
    lua_setfield(L, -2, "variables");
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_debugger_breakpoints(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &kv : editor->debugger_breakpoints)
  {
    for (const DebuggerBreakpoint &bp : kv.second)
    {
      lua_newtable(L);
      lua_push_str_field(L, "path", bp.filepath);
      lua_push_int_field(L, "line", (long long)bp.line + 1);
      lua_push_bool_field(L, "verified", bp.verified);
      lua_rawseti(L, -2, n++);
    }
  }
}

void LuaAPI::debugger_toggle_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  if (line < 0)
  {
    lua_pushboolean(L, 0);
    return;
  }
  editor->toggle_debugger_breakpoint(path, line);
  lua_pushboolean(L, editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::debugger_has_breakpoint_from_lua(lua_State *L)
{
  if (!editor)
  {
    lua_pushboolean(L, 0);
    return;
  }
  const std::string path = luaL_checkstring(L, 1);
  const int line = (int)luaL_checkinteger(L, 2) - 1;
  lua_pushboolean(L, line >= 0 && editor->has_debugger_breakpoint(path, line));
}

void LuaAPI::push_theme_palette(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const Theme &t = editor->theme;
  lua_newtable(L);
  auto color = [&](const char *slot, int fg, int bg)
  {
    lua_newtable(L);
    lua_push_int_field(L, "fg", fg);
    if (bg >= 0)
      lua_push_int_field(L, "bg", bg);
    lua_setfield(L, -2, slot);
  };
  auto flag = [&](const char *slot, bool v) { lua_push_bool_field(L, slot, v); };
  color("default", t.fg_default, t.bg_default);
  color("keyword", t.fg_keyword, t.bg_keyword);
  color("string", t.fg_string, t.bg_string);
  color("comment", t.fg_comment, t.bg_comment);
  color("number", t.fg_number, t.bg_number);
  color("function", t.fg_function, t.bg_function);
  color("type", t.fg_type, t.bg_type);
  color("variable", t.fg_variable, t.bg_variable);
  color("constant", t.fg_constant, t.bg_constant);
  color("builtin", t.fg_builtin, t.bg_builtin);
  color("operator", t.fg_operator, t.bg_operator);
  color("punctuation", t.fg_punctuation, t.bg_punctuation);
  color("tag", t.fg_tag, t.bg_tag);
  color("attribute", t.fg_attribute, t.bg_attribute);
  color("namespace", t.fg_namespace, t.bg_namespace);
  color("module", t.fg_module, t.bg_module);
  color("parameter", t.fg_parameter, t.bg_parameter);
  color("field", t.fg_field, t.bg_field);
  color("keyword_control", t.fg_keyword_control, t.bg_keyword_control);
  color("keyword_storage", t.fg_keyword_storage, t.bg_keyword_storage);
  color("keyword_preproc", t.fg_keyword_preproc, t.bg_keyword_preproc);
  color("function_method", t.fg_function_method, t.bg_function_method);
  color("function_constructor", t.fg_function_constructor, t.bg_function_constructor);
  color("type_builtin", t.fg_type_builtin, t.bg_type_builtin);
  color("constant_macro", t.fg_constant_macro, t.bg_constant_macro);
  color("string_escape", t.fg_string_escape, t.bg_string_escape);
  color("punctuation_bracket", t.fg_punctuation_bracket, t.bg_punctuation_bracket);
  color("punctuation_delimiter", t.fg_punctuation_delimiter, t.bg_punctuation_delimiter);
  color("panel_border", t.fg_panel_border, t.bg_panel_border);
  color("selection", t.fg_selection, t.bg_selection);
  color("line_num", t.fg_line_num, t.bg_line_num);
  color("cursor", t.fg_cursor, t.bg_cursor);
  color("status", t.fg_status, t.bg_status);
  color("status_message", t.fg_status_message, -1);
  color("status_logo", t.fg_status_logo, t.bg_status_logo);
  color("status_file", t.fg_status_file, t.bg_status_file);
  color("status_info", t.fg_status_info, t.bg_status_info);
  color("status_warning", t.fg_status_warning, t.bg_status_warning);
  color("status_error", t.fg_status_error, t.bg_status_error);
  color("status_muted", t.fg_status_muted, t.bg_status_muted);
  color("command", t.fg_command, t.bg_command);
  color("search_match", t.fg_search_match, t.bg_search_match);
  color("minimap", t.fg_minimap, t.bg_minimap);
  color("sidebar", t.fg_sidebar, t.bg_sidebar);
  color("sidebar_directory", t.fg_sidebar_directory, -1);
  color("sidebar_selected", t.fg_sidebar_selected, t.bg_sidebar_selected);
  color(
      "sidebar_selected_inactive", t.fg_sidebar_selected_inactive, t.bg_sidebar_selected_inactive);
  color("sidebar_border", t.fg_sidebar_border, -1);
  color("tab_active", t.fg_tab_active, t.bg_tab_active);
  color("tab_inactive", t.fg_tab_inactive, t.bg_tab_inactive);
  color("tab_close", t.fg_tab_close, -1);
  color("tab_separator", t.fg_tab_separator, -1);
  color("active_border", t.fg_active_border, t.bg_active_border);
  color("image_border", t.fg_image_border, t.bg_image_border);
  color("diagnostic_error", t.fg_diagnostic_error, -1);
  color("diagnostic_warning", t.fg_diagnostic_warning, -1);
  color("diagnostic_info", t.fg_diagnostic_info, -1);
  color("diagnostic_hint", t.fg_diagnostic_hint, -1);
  color("bracket1", t.fg_bracket1, -1);
  color("bracket2", t.fg_bracket2, -1);
  color("bracket3", t.fg_bracket3, -1);
  color("bracket4", t.fg_bracket4, -1);
  color("bracket5", t.fg_bracket5, -1);
  color("bracket6", t.fg_bracket6, -1);
  color("bracket_match", t.fg_bracket_match, t.bg_bracket_match);
  color("telescope", t.fg_telescope, t.bg_telescope);
  color("telescope_selected", t.fg_telescope_selected, t.bg_telescope_selected);
  color("telescope_preview", t.fg_telescope_preview, t.bg_telescope_preview);
  color("terminal", t.fg_terminal, t.bg_terminal);
  color("terminal_tab_inactive", t.fg_terminal_tab_inactive, t.bg_terminal_tab_inactive);
  color("terminal_tab_active", t.fg_terminal_tab_active, t.bg_terminal_tab_active);
  color("terminal_tab_focused", t.fg_terminal_tab_focused, t.bg_terminal_tab_focused);
  color("terminal_tab_close", t.fg_terminal_tab_close, -1);
  color("terminal_tab_plus", t.fg_terminal_tab_plus, -1);
  color("terminal_tab_separator", t.fg_terminal_tab_separator, -1);
  color("git_modified", t.fg_git_modified, t.bg_git_modified);
  color("git_added", t.fg_git_added, t.bg_git_added);
  color("git_deleted", t.fg_git_deleted, t.bg_git_deleted);
  color("git_renamed", t.fg_git_renamed, t.bg_git_renamed);
  color("git_untracked", t.fg_git_untracked, t.bg_git_untracked);
  color("git_conflict", t.fg_git_conflict, t.bg_git_conflict);
  flag("syntax_variable_explicit", t.syntax_variable_explicit);
  flag("syntax_constant_explicit", t.syntax_constant_explicit);
  flag("syntax_builtin_explicit", t.syntax_builtin_explicit);
  flag("syntax_operator_explicit", t.syntax_operator_explicit);
  flag("syntax_punctuation_explicit", t.syntax_punctuation_explicit);
  flag("syntax_tag_explicit", t.syntax_tag_explicit);
  flag("syntax_attribute_explicit", t.syntax_attribute_explicit);
  flag("syntax_namespace_explicit", t.syntax_namespace_explicit);
  flag("syntax_module_explicit", t.syntax_module_explicit);
  flag("syntax_parameter_explicit", t.syntax_parameter_explicit);
  flag("syntax_field_explicit", t.syntax_field_explicit);
  flag("syntax_keyword_control_explicit", t.syntax_keyword_control_explicit);
  flag("syntax_keyword_storage_explicit", t.syntax_keyword_storage_explicit);
  flag("syntax_keyword_preproc_explicit", t.syntax_keyword_preproc_explicit);
  flag("syntax_function_method_explicit", t.syntax_function_method_explicit);
  flag("syntax_function_constructor_explicit", t.syntax_function_constructor_explicit);
  flag("syntax_type_builtin_explicit", t.syntax_type_builtin_explicit);
  flag("syntax_constant_macro_explicit", t.syntax_constant_macro_explicit);
  flag("syntax_string_escape_explicit", t.syntax_string_escape_explicit);
  flag("syntax_punctuation_bracket_explicit", t.syntax_punctuation_bracket_explicit);
  flag("syntax_punctuation_delimiter_explicit", t.syntax_punctuation_delimiter_explicit);
}

void LuaAPI::push_buffer_lines(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (buf.is_lazy())
  {
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  int n = 1;
  for (const std::string &line : buf.lines)
  {
    lua_pushlstring(L, line.data(), line.size());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_clipboard_text(lua_State *L)
{
  lua_pushstring(L, editor ? editor->clipboard.c_str() : "");
}

void LuaAPI::emit_debugger_state_changed()
{
  if (!editor || !has_event_subscribers("debugger.state_changed"))
    return;
  std::string sig;
  for (const auto &st : editor->debugger_session_state)
  {
    sig += st.name + "|" + (st.running ? "1" : "0") + (st.stopped ? "1" : "0") + "|"
           + std::to_string(st.active_thread_id) + "|" + std::to_string(st.active_frame_id) + "|"
           + std::to_string(st.threads.size()) + "|" + std::to_string(st.variables.size()) + ";";
  }
  if (sig == last_debugger_sig_)
    return;
  last_debugger_sig_ = sig;
  emit_event_bus("debugger.state_changed",
                 [this](lua_State *L)
                 {
                   lua_newtable(L);
                   int n = 1;
                   for (const auto &st : editor->debugger_session_state)
                   {
                     lua_newtable(L);
                     lua_push_str_field(L, "name", st.name);
                     lua_push_str_field(L, "adapter", st.adapter);
                     lua_push_bool_field(L, "running", st.running);
                     lua_push_bool_field(L, "stopped", st.stopped);
                     lua_push_int_field(L, "active_thread_id", st.active_thread_id);
                     lua_push_int_field(L, "active_frame_id", st.active_frame_id);
                     lua_rawseti(L, -2, n++);
                   }
                   lua_setfield(L, -2, "sessions");
                 });
}

// ---------------------------------------------------------------------------
// One-shot debugger requests (jot.debugger.request_stack / request_variables
// / request_threads)
// ---------------------------------------------------------------------------

void LuaAPI::debugger_request_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int target = editor->current_debugger_session;
  if (lua_isnumber(L, 2))
    target = (int)luaL_checkinteger(L, 2) - 1;
  if (target < 0 || target >= (int)editor->debugger_sessions.size()
      || !editor->debugger_sessions[(size_t)target])
  {
    return; // no live session for the target: a sink would never deliver
  }
  DebuggerClient *client = editor->debugger_sessions[(size_t)target].get();
  DebuggerSessionState empty;
  const DebuggerSessionState &st = target < (int)editor->debugger_session_state.size()
                                       ? editor->debugger_session_state[(size_t)target]
                                       : empty;
  lua_pushvalue(L, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "dbg." + std::to_string(kind) + "." + std::to_string(ref);
  lua_callbacks[id] = ref;
  pending_debugger_session = target;
  switch (kind)
  {
  case 0:
    pending_debugger_stack = id;
    if (st.active_thread_id > 0)
      client->stack_trace(st.active_thread_id);
    break;
  case 1:
    pending_debugger_variables = id;
    if (st.active_frame_id > 0)
      client->scopes(st.active_frame_id);
    break;
  default:
    pending_debugger_threads = id;
    client->threads();
    break;
  }
}

bool LuaAPI::try_deliver_debugger_stack(int session, const std::vector<DebuggerFrame> &frames)
{
  if (pending_debugger_stack.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_stack;
  pending_debugger_stack.clear();
  pending_debugger_session = -1;
  int thread_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    thread_id = editor->debugger_session_state[(size_t)session].active_thread_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "thread_id", thread_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerFrame &f : frames)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", f.id);
                                  lua_push_str_field(L, "name", f.name);
                                  lua_push_str_field(L, "path", f.filepath);
                                  lua_push_int_field(L, "line", (long long)f.line + 1);
                                  lua_push_int_field(L, "column", (long long)f.column + 1);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "frames");
                              });
}

bool LuaAPI::try_deliver_debugger_variables(int session,
                                            const std::vector<DebuggerVariable> &variables)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_variables;
  pending_debugger_variables.clear();
  pending_debugger_session = -1;
  int frame_id = 0;
  if (editor && session >= 0 && session < (int)editor->debugger_session_state.size())
  {
    frame_id = editor->debugger_session_state[(size_t)session].active_frame_id;
  }
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_push_int_field(L, "frame_id", frame_id);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerVariable &v : variables)
                                {
                                  lua_newtable(L);
                                  lua_push_str_field(L, "name", v.name);
                                  lua_push_str_field(L, "value", v.value);
                                  lua_push_str_field(L, "type", v.type);
                                  lua_push_int_field(
                                      L, "variables_reference", v.variables_reference);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "variables");
                              });
}

bool LuaAPI::try_deliver_debugger_threads(int session, const std::vector<DebuggerThread> &threads)
{
  if (pending_debugger_threads.empty() || pending_debugger_session != session)
  {
    return false;
  }
  const std::string id = pending_debugger_threads;
  pending_debugger_threads.clear();
  pending_debugger_session = -1;
  return lua_deliver_one_shot(id,
                              lua_callbacks,
                              lua_state,
                              [&](lua_State *L)
                              {
                                lua_newtable(L);
                                lua_push_int_field(L, "session", (long long)session + 1);
                                lua_newtable(L);
                                int n = 1;
                                for (const DebuggerThread &t : threads)
                                {
                                  lua_newtable(L);
                                  lua_push_int_field(L, "id", t.id);
                                  lua_push_str_field(L, "name", t.name);
                                  lua_rawseti(L, -2, n++);
                                }
                                lua_setfield(L, -2, "threads");
                              });
}

bool LuaAPI::debugger_chain_variables(int session, const std::vector<DebuggerVariable> &scopes)
{
  if (pending_debugger_variables.empty() || pending_debugger_session != session)
  {
    return false;
  }
  if (!editor || session < 0 || session >= (int)editor->debugger_sessions.size())
  {
    return false;
  }
  auto &client = editor->debugger_sessions[(size_t)session];
  if (!client)
    return false;
  for (const DebuggerVariable &scope : scopes)
  {
    if (scope.variables_reference > 0)
    {
      client->variables(scope.variables_reference);
      return true;
    }
  }
  // No expandable scope: deliver an empty answer so the sink cannot leak.
  try_deliver_debugger_variables(session, {});
  return false;
}

// ---------------------------------------------------------------------------
// Batch 6: jot.motion / jot.sidebar / LSP manager actions / buffer extras /
// jot.git.diff / config.changed bus event
// ---------------------------------------------------------------------------

void LuaAPI::motion_from_lua(lua_State *L, int kind)
{
  if (!editor)
    return;
  switch (kind)
  {
  case 0:
    editor->move_word_forward(false);
    break;
  case 1:
    editor->move_word_backward(false);
    break;
  case 2:
    editor->move_to_line_smart_start(false);
    break;
  case 3:
    editor->move_to_line_end(false);
    break;
  case 4:
    editor->move_to_file_start(false);
    break;
  case 5:
    editor->move_to_file_end(false);
    break;
  case 6:
    editor->jump_to_matching_bracket();
    break;
  case 7:
    editor->select_current_function();
    break;
  default:
    return;
  }
  editor->needs_redraw = true;
}

void LuaAPI::push_sidebar_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const bool git = editor->active_sidebar_view == editor->SIDEBAR_VIEW_GIT;
  lua_push_bool_field(L, "visible", editor->show_sidebar);
  lua_push_int_field(L, "width", editor->sidebar_width);
  lua_push_str_field(L, "view", git ? "git" : "explorer");
  lua_push_int_field(
      L, "selected", git ? editor->git_sidebar_selected : editor->file_tree_selected);
  lua_push_int_field(L, "scroll", git ? editor->git_sidebar_scroll : editor->file_tree_scroll);
}

void LuaAPI::sidebar_set_view_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const std::string view = luaL_checkstring(L, 1);
  editor->active_sidebar_view =
      view == "git" ? editor->SIDEBAR_VIEW_GIT : editor->SIDEBAR_VIEW_EXPLORER;
  editor->show_sidebar = true;
  editor->needs_redraw = true;
}

void LuaAPI::push_buffer_filetype(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  lua_pushstring(L, editor->get_file_extension(buf.filepath).c_str());
}

void LuaAPI::push_buffer_get_line(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const int line = (int)luaL_checkinteger(L, 1) - 1;
  const int idx = resolve_buffer_arg(L, 2);
  if (idx < 0 || line < 0)
  {
    lua_pushnil(L);
    return;
  }
  const FileBuffer &buf = editor->buffers[(size_t)idx];
  if (line >= buf.line_count())
  {
    lua_pushnil(L);
    return;
  }
  const std::string &s = buf.line(line);
  lua_pushlstring(L, s.data(), s.size());
}
