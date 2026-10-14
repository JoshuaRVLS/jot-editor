// LuaAPI plugin surface: commands, keymaps, autocmds, plugin panels and
// pickers - everything user scripts register and the editor dispatches.
#include "core/editor.h"
#include "core/host_api.h"
#include "core/pattern.h"
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

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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

  // The next chord step of `key` at or past `pos`, in place of the list
  // keymap_steps builds: the prefix check runs this over every registered keymap
  // on every keystroke, and the vectors were most of what a keystroke allocated.
  // Empty steps are skipped the same way keymap_steps skips them.
  bool next_keymap_step(const std::string &key, std::size_t &pos, std::string_view &step)
  {
    while (pos <= key.size())
    {
      const std::size_t space = key.find(' ', pos);
      const std::size_t end = (space == std::string::npos) ? key.size() : space;
      if (end > pos)
      {
        step = std::string_view(key).substr(pos, end - pos);
        pos = end + 1;
        return true;
      }
      if (space == std::string::npos)
      {
        return false;
      }
      pos = space + 1;
    }
    return false;
  }
} // namespace
bool LuaAPI::plugin_keymap_is_prefix(const std::string &chord, const std::string &m)
{
  std::size_t chord_pos = 0;
  std::string_view chord_step;
  if (!next_keymap_step(chord, chord_pos, chord_step))
  {
    return false;
  }
  for (const auto &x : plugin_keymaps)
  {
    if (x.mode != "global" && x.mode != m)
    {
      continue;
    }
    // Walk the typed chord against the registered one step by step: every typed
    // step has to equal the registered step in its place, and the registered
    // chord has to carry at least one step past the typed one. That is the same
    // test as keymap_steps compared like for like, with neither list built.
    std::size_t key_pos = 0;
    std::string_view key_step;
    bool match = true;
    chord_pos = 0;
    while (next_keymap_step(chord, chord_pos, chord_step))
    {
      if (!next_keymap_step(x.key, key_pos, key_step) || key_step != chord_step)
      {
        match = false;
        break;
      }
    }
    // A registered chord that is exactly the typed one is a leaf, not a group.
    if (match && next_keymap_step(x.key, key_pos, key_step))
    {
      return true;
    }
  }
  return false;
}

std::vector<PluginKeymapChild> LuaAPI::plugin_keymap_children(const std::string &chord_path,
                                                              const std::string &m)
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
    auto it = std::find_if(
        out.begin(), out.end(), [&next](const PluginKeymapChild &c) { return c.key == next; });
    if (it != out.end())
    {
      it->group = it->group || deeper;
    }
    else
    {
      out.push_back({next, x.detail, deeper});
    }
  }
  std::sort(out.begin(),
            out.end(),
            [](const PluginKeymapChild &a, const PluginKeymapChild &b) { return a.key < b.key; });
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
  dispatch_autocmd(e, f, b, e == "BufChange" ? &last_edit_ : nullptr);
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
    dispatch_autocmd(pending.event,
                     pending.filepath,
                     pending.buffer,
                     pending.event == "BufChange" ? &pending.delta : nullptr);
  }
  flushing_autocmds_ = false;
}

void LuaAPI::dispatch_autocmd(const std::string &event,
                              const std::string &filepath,
                              int buffer,
                              const LuaEditDelta *edit)
{
  // Copy the hooks out first. A handler may register, drop or fire autocmds of
  // its own, which would invalidate an iterator over the live vector, and a
  // one-shot hook is unregistered before it runs so that a re-fire from inside
  // its own callback cannot reach it a second time. Its callback ref is only
  // freed after the call, because the call is what needs it.
  std::vector<PluginAutocmd> hooks;
  for (auto it = plugin_autocmds.begin(); it != plugin_autocmds.end();)
  {
    if (it->event != event || !jot_pattern::path_match(it->pattern, filepath))
    {
      ++it;
      continue;
    }
    hooks.push_back(*it);
    if (!it->once)
    {
      ++it;
      continue;
    }
    it = plugin_autocmds.erase(it);
  }
  for (const auto &hook : hooks)
  {
    call_callback_event(hook.callback, event, filepath, buffer, edit);
    if (hook.once)
    {
      drop_callback(hook.callback);
    }
  }
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
      drop_callback(it->callback);
    }
    it = plugin_keymaps.erase(it);
  }
}

void LuaAPI::drop_callback(const std::string &callback)
{
  auto cb = lua_callbacks.find(callback);
  if (cb == lua_callbacks.end())
  {
    return;
  }
  if (lua_state)
  {
    luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, cb->second);
  }
  lua_callbacks.erase(cb);
}
void LuaAPI::register_autocmd(const std::string &e,
                              const std::string &c,
                              const std::string &pattern,
                              bool once)
{
  plugin_autocmds.push_back({e, c, pattern, once});
}

void LuaAPI::register_autocmd_from_lua(lua_State *L)
{
  // jot.autocmd(event, fn [, opts]), where opts is {pattern = "*.py",
  // once = true}. A plain string in that place is the pattern, the same
  // shorthand jot.events.subscribe takes for the same option.
  //
  // This is the callable table's __call, so the table arrives as argument 1
  // (`autocmd(...)`, `jot.autocmd(...)` and `jot.autocmd:...` alike), and the
  // event and the function follow it.
  const int self = lua_istable(L, 1) ? 1 : 0;
  luaL_checktype(L, 2 + self, LUA_TFUNCTION);
  std::string pattern;
  bool once = false;
  if (lua_isstring(L, 3 + self))
  {
    pattern = lua_tostring(L, 3 + self);
  }
  else if (lua_istable(L, 3 + self))
  {
    pattern = table_string(L, 3 + self, "pattern", "");
    once = table_bool(L, 3 + self, "once", false);
  }
  lua_pushvalue(L, 2 + self);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string id = "lua." + std::to_string(ref);
  lua_callbacks[id] = ref;
  register_autocmd(luaL_optstring(L, 1 + self, ""), id, pattern, once);
}

void LuaAPI::autocmd_exec_from_lua(lua_State *L)
{
  // The hooks for one event name, called now, with the optional table as the
  // payload. This is the other half of a named event: a plugin registers
  // autocmd("User build", fn) and fires it itself, so the name does not have
  // to exist in the editor for the hook to be reachable.
  const std::string event = luaL_checkstring(L, 1);
  if (!lua_isnoneornil(L, 2))
  {
    luaL_checktype(L, 2, LUA_TTABLE);
  }
  if (!lua_initialized || !editor || !editor->event_loop_.is_main_thread())
  {
    return;
  }
  lua_State *S = static_cast<lua_State *>(lua_state);
  const int data = lua_isnoneornil(L, 2) ? 0 : 2;
  // Same snapshot-then-run shape as dispatch_autocmd, with the payload built
  // here instead of by the native event it came from.
  std::vector<PluginAutocmd> hooks;
  for (auto it = plugin_autocmds.begin(); it != plugin_autocmds.end();)
  {
    if (it->event != event)
    {
      ++it;
      continue;
    }
    hooks.push_back(*it);
    if (!it->once)
    {
      ++it;
      continue;
    }
    it = plugin_autocmds.erase(it);
  }
  for (const auto &hook : hooks)
  {
    const int top = lua_gettop(S);
    auto cb = lua_callbacks.find(hook.callback);
    if (cb == lua_callbacks.end())
    {
      continue;
    }
    lua_rawgeti(S, LUA_REGISTRYINDEX, cb->second);
    lua_newtable(S);
    lua_pushstring(S, event.c_str());
    lua_setfield(S, -2, "event");
    if (data != 0)
    {
      copy_event_fields(S, data);
    }
    if (!payload_wanted(S, hook.pattern))
    {
      lua_settop(S, top);
      continue;
    }
    if (lua_pcall(S, 1, 0, 0) != LUA_OK)
    {
      std::cerr << "Lua event callback error: " << lua_tostring(S, -1) << "\n";
    }
    lua_settop(S, top);
    if (hook.once)
    {
      drop_callback(hook.callback);
    }
  }
}

void LuaAPI::push_autocmd_list(lua_State *L)
{
  // Rows in registration order, with the file filter and the one-shot flag
  // spelled out, so a plugin can see what is live instead of guessing why a
  // hook did or did not run.
  const std::string only = luaL_optstring(L, 1, "");
  lua_newtable(L);
  int row = 0;
  for (const auto &hook : plugin_autocmds)
  {
    if (!only.empty() && hook.event != only)
    {
      continue;
    }
    lua_newtable(L);
    lua_pushstring(L, hook.event.c_str());
    lua_setfield(L, -2, "event");
    lua_pushstring(L, hook.pattern.c_str());
    lua_setfield(L, -2, "pattern");
    lua_pushboolean(L, hook.once ? 1 : 0);
    lua_setfield(L, -2, "once");
    lua_rawseti(L, -2, ++row);
  }
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
void LuaAPI::show_panel(const std::string &s, bool focus)
{
  if (editor && editor->host_api)
    editor->host_api->io.show_plugin_panel(s, focus);
}
void LuaAPI::close_plugin_panel()
{
  if (!editor)
    return;
  editor->active_plugin_panel.clear();
  editor->close_right_panel_tab(Editor::RIGHT_PANEL_PLUGIN);
  editor->needs_redraw = true;
}
std::vector<PluginPanelRow>
LuaAPI::plugin_panel_rows(const std::string &name, const std::string &action, int index)
{
  for (auto &p : plugin_panels)
    if (p.name == name)
    {
      auto i = lua_callbacks.find(p.callback);
      if (i == lua_callbacks.end())
        return {};
      lua_State *L = static_cast<lua_State *>(lua_state);
      const int top = lua_gettop(L);
      lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
      lua_pushstring(L, name.c_str());
      if (action.empty())
      {
        lua_pushnil(L);
      }
      else
      {
        lua_newtable(L);
        lua_pushstring(L, action.c_str());
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, index);
        lua_setfield(L, -2, "index");
      }
      if (lua_pcall(L, 2, 1, 0))
      {
        std::cerr << "Lua plugin panel error: " << lua_tostring(L, -1) << "\n";
        lua_settop(L, top);
        return {};
      }
      std::vector<PluginPanelRow> out;
      if (lua_istable(L, -1))
      {
        const int rows = lua_gettop(L);
        const size_t count = lua_rawlen(L, rows);
        for (size_t r = 1; r <= count; r++)
        {
          lua_rawgeti(L, rows, (int)r);
          PluginPanelRow row;
          if (lua_istable(L, -1))
          {
            row.text = table_string(L, -1, "text", "");
            row.detail = table_string(L, -1, "detail", "");
            row.kind = table_string(L, -1, "kind", "");
            row.action = table_string(L, -1, "action", "");
            row.icon = table_string(L, -1, "icon", "");
            row.icon_fg = table_color(L, -1, "icon_fg");
            row.fg = table_color(L, -1, "fg");
            row.bold = table_bool(L, -1, "bold", false);
            row.selected = table_bool(L, -1, "selected", false);
          }
          else if (lua_isstring(L, -1))
          {
            row.text = lua_tostring(L, -1);
          }
          lua_pop(L, 1);
          out.push_back(std::move(row));
        }
      }
      lua_settop(L, top);
      return out;
    }
  return {};
}

bool LuaAPI::plugin_panel_key(
    const std::string &name, const std::string &key, bool ctrl, bool shift, bool alt)
{
  if (!lua_state)
    return false;
  for (auto &p : plugin_panels)
    if (p.name == name)
    {
      auto i = lua_callbacks.find(p.callback);
      if (i == lua_callbacks.end())
        return false;
      lua_State *L = static_cast<lua_State *>(lua_state);
      const int top = lua_gettop(L);
      lua_rawgeti(L, LUA_REGISTRYINDEX, i->second);
      lua_pushstring(L, name.c_str());
      lua_newtable(L);
      lua_pushstring(L, "key");
      lua_setfield(L, -2, "action");
      lua_pushstring(L, key.c_str());
      lua_setfield(L, -2, "key");
      lua_pushboolean(L, ctrl ? 1 : 0);
      lua_setfield(L, -2, "ctrl");
      lua_pushboolean(L, shift ? 1 : 0);
      lua_setfield(L, -2, "shift");
      lua_pushboolean(L, alt ? 1 : 0);
      lua_setfield(L, -2, "alt");
      if (lua_pcall(L, 2, 1, 0))
      {
        std::cerr << "Lua plugin panel error: " << lua_tostring(L, -1) << "\n";
        lua_settop(L, top);
        return false;
      }
      // A key is answered with a boolean: only an explicit true means the
      // panel used it. Rows are what a panel returns for a paint, so a panel
      // that has no interest in keys (LeetCode) returns its rows here and the
      // key carries on to whatever it would have reached anyway.
      const bool consumed = lua_isboolean(L, -1) != 0 && lua_toboolean(L, -1) != 0;
      lua_settop(L, top);
      return consumed;
    }
  return false;
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
std::vector<PluginPickerRow> LuaAPI::plugin_picker_items(const std::string &callback)
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
  std::vector<PluginPickerRow> out;
  if (lua_istable(L, -1))
  {
    lua_pushnil(L);
    while (lua_next(L, -2))
    {
      // A row is a plain string (the label stands for itself) or a table with
      // `label`, `value` (what the select callback receives instead of the
      // display text), `detail` (right-aligned secondary text; empty shows
      // none) and `fg` (the row's own colour).
      PluginPickerRow row;
      if (lua_istable(L, -1))
      {
        row.label = table_string(L, -1, "label", "");
        row.value = table_string(L, -1, "value", "");
        row.detail = table_string(L, -1, "detail", "");
        row.fg = table_color(L, -1, "fg");
      }
      else if (lua_isstring(L, -1))
      {
        row.label = lua_tostring(L, -1);
      }
      out.push_back(std::move(row));
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
// same native capabilities the UI uses - no C++ rebuild needed for features.
