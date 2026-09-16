// LuaAPI plugin surface: commands, keymaps, autocmds, plugin panels and
// pickers — everything user scripts register and the editor dispatches.
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

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
void LuaAPI::set_update_handler_ref(int ref)
{
  auto it = lua_callbacks.find("update.cmd");
  if (it != lua_callbacks.end())
  {
    if (lua_state)
    {
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, it->second);
    }
    lua_callbacks.erase(it);
  }
  lua_callbacks["update.cmd"] = ref;
}
bool LuaAPI::run_update_command(const std::string &arg)
{
  return call_callback_string("update.cmd", arg);
}
bool LuaAPI::restart_editor(bool force)
{
  return editor && editor->restart_editor(force);
}
bool LuaAPI::run_plugin_keymap(const std::string &k, const std::string &m)
{
  for (auto &x : plugin_keymaps)
  {
    if (x.key != key_name(k) || (x.mode != "global" && x.mode != m))
      continue;
    // A keymap registered with neither a callback nor a command has no
    // action (e.g. a which-key group header carrying only a detail/title):
    // it must not consume the keystroke, or the group it labels would never
    // open. Keep scanning so a real action on the same key still wins.
    if (x.callback.empty() && x.command.empty())
      continue;
    if (!x.command.empty() && editor)
    {
      if (x.command[0] == ':')
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

namespace
{
  // Splits a canonical keymap key into its chord steps, e.g.
  // "Ctrl+T N" -> {"Ctrl+T", "N"}. Keys without a space are a single step.
  std::vector<std::string> keymap_steps(const std::string &key)
  {
    std::vector<std::string> steps;
    size_t start = 0;
    while (start <= key.size())
    {
      const size_t sp = key.find(' ', start);
      const size_t end = (sp == std::string::npos) ? key.size() : sp;
      if (end > start)
      {
        steps.push_back(key.substr(start, end - start));
      }
      start = end + 1;
    }
    return steps;
  }
} // namespace
bool LuaAPI::plugin_keymap_is_prefix(const std::string &chord, const std::string &m)
{
  const auto prefix = keymap_steps(chord);
  if (prefix.empty())
    return false;
  for (const auto &x : plugin_keymaps)
  {
    if (x.mode != "global" && x.mode != m)
      continue;
    const auto steps = keymap_steps(x.key);
    if (steps.size() <= prefix.size())
      continue;
    bool match = true;
    for (size_t i = 0; i < prefix.size(); i++)
    {
      if (steps[i] != prefix[i])
      {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

std::vector<PluginKeymapChild>
LuaAPI::plugin_keymap_children(const std::string &chord_path, const std::string &m)
{
  const auto prefix = keymap_steps(chord_path);
  std::vector<PluginKeymapChild> out;
  for (const auto &x : plugin_keymaps)
  {
    if (x.mode != "global" && x.mode != m)
      continue;
    const auto steps = keymap_steps(x.key);
    if (steps.size() <= prefix.size())
      continue;
    bool match = true;
    for (size_t i = 0; i < prefix.size(); i++)
    {
      if (steps[i] != prefix[i])
      {
        match = false;
        break;
      }
    }
    if (!match)
      continue;
    const std::string &next = steps[prefix.size()];
    const bool deeper = steps.size() > prefix.size() + 1;
    auto it = std::find_if(out.begin(), out.end(), [&next](const PluginKeymapChild &c)
                           { return c.key == next; });
    if (it != out.end())
    {
      it->group = it->group || deeper;
    }
    else
    {
      out.push_back({next, x.detail, deeper});
    }
  }
  std::sort(out.begin(), out.end(),
            [](const PluginKeymapChild &a, const PluginKeymapChild &b)
            { return a.key < b.key; });
  return out;
}

std::string LuaAPI::plugin_keymap_group_title(const std::string &chord_path, const std::string &m)
{
  // The title of a group is the description registered on its bare prefix key:
  // "Alt+D" at the top level, "Alt+D a" for a nested menu. Compared step by step
  // so a nested prefix matches too -- matching single-chord keys only left every
  // submenu untitled.
  const std::vector<std::string> want = keymap_steps(chord_path);
  if (want.empty())
  {
    return "";
  }
  for (const auto &x : plugin_keymaps)
  {
    if (x.mode != "global" && x.mode != m)
      continue;
    if (x.detail.empty())
      continue;
    const std::vector<std::string> have = keymap_steps(x.key);
    if (have.size() != want.size())
      continue;
    if (std::equal(have.begin(), have.end(), want.begin()))
      return x.detail;
  }
  return "";
}

bool LuaAPI::is_rapid_autocmd(const std::string &event)
{
  // Events that can fire many times within a single event-loop drain (once
  // per keystroke) and whose Lua handlers only need the latest state.
  return event == "BufChange" || event == "CursorMoved";
}

void LuaAPI::fire_autocmd(const std::string &e, const std::string &f, int b)
{
  if (is_rapid_autocmd(e) && !flushing_autocmds_)
  {
    // Coalesce: keep the newest payload for this event name and dispatch
    // once at the next flush. The edit delta is captured now because
    // on_buffer_change resets last_edit_ immediately after firing.
    for (auto &pending : pending_autocmds_)
    {
      if (pending.event == e)
      {
        pending.filepath = f;
        pending.buffer = b;
        if (e == "BufChange")
        {
          pending.delta = last_edit_;
        }
        return;
      }
    }
    PendingAutocmd pending;
    pending.event = e;
    pending.filepath = f;
    pending.buffer = b;
    if (e == "BufChange")
    {
      pending.delta = last_edit_;
    }
    pending_autocmds_.push_back(std::move(pending));
    return;
  }
  // Non-rapid events must not jump ahead of coalesced ones, so drain the
  // queue first, then dispatch immediately (which also covers events fired
  // from inside a Lua callback while the queue is being flushed).
  flush_pending_autocmds();
  for (auto &x : plugin_autocmds)
    if (x.event == e)
      call_callback_event(x.callback, e, f, b, e == "BufChange" ? &last_edit_ : nullptr);
}

void LuaAPI::flush_pending_autocmds()
{
  if (pending_autocmds_.empty() || flushing_autocmds_)
  {
    return;
  }
  flushing_autocmds_ = true;
  std::vector<PendingAutocmd> queue = std::move(pending_autocmds_);
  for (const auto &pending : queue)
  {
    for (auto &x : plugin_autocmds)
    {
      if (x.event == pending.event)
      {
        call_callback_event(x.callback,
                            pending.event,
                            pending.filepath,
                            pending.buffer,
                            pending.event == "BufChange" ? &pending.delta : nullptr);
      }
    }
  }
  flushing_autocmds_ = false;
}
void LuaAPI::register_keymap(const std::string &k,
                             const std::string &c,
                             const std::string &cmd,
                             const std::string &d,
                             const std::string &m)
{
  plugin_keymaps.push_back({key_name(k), c, cmd, d, m});
}
void LuaAPI::remove_keymap(const std::string &key, const std::string &mode)
{
  const std::string k = key_name(key);
  for (auto it = plugin_keymaps.begin(); it != plugin_keymaps.end();)
  {
    if (it->key != k || (!mode.empty() && it->mode != mode))
    {
      ++it;
      continue;
    }
    if (!it->callback.empty())
    {
      auto cb = lua_callbacks.find(it->callback);
      if (cb != lua_callbacks.end())
      {
        if (lua_state)
        {
          luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, cb->second);
        }
        lua_callbacks.erase(cb);
      }
    }
    it = plugin_keymaps.erase(it);
  }
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

