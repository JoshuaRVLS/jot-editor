// Lua event bus + native timers: subscribe/unsubscribe/broadcast, the typed
// emit helpers (git.refreshed, buffer events, theme.switched) and the native
// event-loop timer runtime with Lua callbacks. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

void LuaAPI::events_subscribe_from_lua(lua_State *L)
{
  const std::string name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  EventBusSubscriber sub;
  sub.ref = ref;
  sub.key = "ev." + name + "." + std::to_string(next_event_sub_id_++);
  event_subscribers_[name].push_back(std::move(sub));
  lua_pushstring(L, event_subscribers_[name].back().key.c_str());
}

void LuaAPI::events_unsubscribe_from_lua(lua_State *L)
{
  const std::string name = luaL_checkstring(L, 1);
  const std::string key = luaL_checkstring(L, 2);
  auto it = event_subscribers_.find(name);
  if (it == event_subscribers_.end())
    return;
  auto &vec = it->second;
  for (auto i = vec.begin(); i != vec.end(); ++i)
  {
    if (i->key == key)
    {
      if (lua_state)
      {
        luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, i->ref);
      }
      vec.erase(i);
      break;
    }
  }
}

void LuaAPI::emit_event_bus(const std::string &name, const std::function<void(lua_State *)> &build)
{
  if (!lua_initialized || !editor || !is_main_thread())
    return;
  auto it = event_subscribers_.find(name);
  if (it == event_subscribers_.end() || it->second.empty())
    return;
  lua_State *L = static_cast<lua_State *>(lua_state);
  const int top = lua_gettop(L);
  // Copy so a subscriber can unsubscribe itself mid-dispatch.
  const std::vector<EventBusSubscriber> subs = it->second;
  for (const auto &sub : subs)
  {
    const int frame_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, sub.ref);
    lua_newtable(L);
    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "event");
    build(L);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
      std::cerr << "Lua event bus error (" << name << "): " << lua_tostring(L, -1) << "\n";
    }
    lua_settop(L, frame_top);
  }
  lua_settop(L, top);
}

void LuaAPI::emit_git_refreshed()
{
  if (!editor || !has_event_subscribers("git.refreshed"))
    return;
  emit_event_bus("git.refreshed",
                 [&](lua_State *L)
                 {
                   lua_push_str_field(L, "root", editor->git_root);
                   lua_push_str_field(L, "branch", editor->git_branch);
                   lua_push_int_field(L, "dirty", editor->git_dirty_count);
                   lua_push_int_field(L, "staged", editor->git_staged_count);
                   lua_push_int_field(L, "unstaged", editor->git_unstaged_count);
                   lua_push_int_field(L, "untracked", editor->git_untracked_count);
                   lua_push_int_field(L, "deleted", editor->git_deleted_count);
                   lua_push_int_field(L, "renamed", editor->git_renamed_count);
                   lua_push_int_field(L, "conflict", editor->git_conflict_count);
                   lua_push_int_field(L, "files", (long long)editor->git_file_status.size());
                 });
}

void LuaAPI::emit_buffer_event(const std::string &event, const std::string &path)
{
  if (!has_event_subscribers(event))
    return;
  emit_event_bus(event, [&](lua_State *L) { lua_push_str_field(L, "path", path); });
}

void LuaAPI::emit_theme_switched(const std::string &name)
{
  if (!has_event_subscribers("theme.switched"))
    return;
  emit_event_bus("theme.switched", [&](lua_State *L) { lua_push_str_field(L, "name", name); });
}

void LuaAPI::timer_set_from_lua(lua_State *L, bool repeat)
{
  const int ms = (int)luaL_checkinteger(L, 1);
  if (!editor || ms < 0 || !lua_isfunction(L, 2))
  {
    lua_pushinteger(L, 0);
    return;
  }
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  const std::string key = "timer." + std::to_string(ref);
  lua_callbacks[key] = ref;
  LuaAPI *self = this;
  uint64_t timer_id = 0;
  try
  {
    timer_id = editor->event_loop_.set_timer(ms, repeat, [self, ref]() { self->fire_timer(ref); });
  }
  catch (...)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    lua_callbacks.erase(key);
    lua_pushinteger(L, 0);
    return;
  }
  timer_entries_[timer_id] = LuaTimerEntry{ref, repeat};
  lua_pushinteger(L, (lua_Integer)timer_id);
}

void LuaAPI::timer_clear_from_lua(lua_State *L)
{
  const uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
  auto it = timer_entries_.find(id);
  if (it == timer_entries_.end())
    return;
  if (editor)
    editor->event_loop_.cancel_timer(id);
  if (lua_state && it->second.ref >= 0)
  {
    const std::string key = "timer." + std::to_string(it->second.ref);
    auto cb = lua_callbacks.find(key);
    if (cb != lua_callbacks.end())
    {
      luaL_unref(static_cast<lua_State *>(lua_state), LUA_REGISTRYINDEX, cb->second);
      lua_callbacks.erase(cb);
    }
  }
  timer_entries_.erase(it);
}

void LuaAPI::cancel_all_timers()
{
  if (editor)
  {
    for (const auto &kv : timer_entries_)
    {
      editor->event_loop_.cancel_timer(kv.first);
    }
  }
  timer_entries_.clear();
}

void LuaAPI::fire_timer(int ref)
{
  if (!lua_initialized || !lua_state)
    return;
  lua_State *L = static_cast<lua_State *>(lua_state);
  uint64_t timer_id = 0;
  bool repeat = false;
  for (const auto &kv : timer_entries_)
  {
    if (kv.second.ref == ref)
    {
      timer_id = kv.first;
      repeat = kv.second.repeat;
      break;
    }
  }
  if (timer_id == 0)
    return;
  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (lua_pcall(L, 0, 0, 0) != LUA_OK)
  {
    std::cerr << "Lua timer callback error: " << lua_tostring(L, -1) << "\n";
  }
  lua_settop(L, top);
  if (!repeat)
  {
    // Re-check: the callback may have cleared this timer itself.
    auto it = timer_entries_.find(timer_id);
    if (it == timer_entries_.end())
      return;
    if (editor)
      editor->event_loop_.cancel_timer(timer_id);
    const std::string key = "timer." + std::to_string(ref);
    auto cb = lua_callbacks.find(key);
    if (cb != lua_callbacks.end())
    {
      luaL_unref(L, LUA_REGISTRYINDEX, cb->second);
      lua_callbacks.erase(cb);
    }
    timer_entries_.erase(it);
  }
}
