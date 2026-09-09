// LuaAPI status-line surface: custom status segments registered by
// plugins and rendered in order.
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
