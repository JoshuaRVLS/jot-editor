// Lua config + editor-info payloads: typed config get/set/unset/has, the
// config keys/path pushers, the current-theme pusher and the editor status
// summary. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <algorithm>
#include <cctype>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

void LuaAPI::lua_config_get(lua_State *L, int kind)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const std::string key = luaL_optstring(L, 1, "");
  const bool has_default = lua_gettop(L) >= 2 && !lua_isnil(L, 2);
  if (!has_default && !editor->config.has(key))
  {
    lua_pushnil(L);
    return;
  }
  switch (kind)
  {
  case 1:
  { // number
    const double def = has_default ? lua_tonumber(L, 2) : 0.0;
    lua_pushnumber(L, editor->config.get_double(key, def));
    return;
  }
  case 2:
  { // boolean
    const bool def = has_default ? lua_toboolean(L, 2) : false;
    lua_pushboolean(L, editor->config.get_bool(key, def));
    return;
  }
  default:
  { // string
    const std::string def = has_default ? (lua_tostring(L, 2) ? lua_tostring(L, 2) : "") : "";
    lua_pushstring(L, editor->config.get(key, def).c_str());
    return;
  }
  }
}

void LuaAPI::config_set_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const char *key = luaL_checkstring(L, 1);
  std::string value;
  if (lua_isboolean(L, 2))
  {
    value = lua_toboolean(L, 2) ? "true" : "false";
  }
  else if (lua_isnumber(L, 2))
  {
    const double n = lua_tonumber(L, 2);
    if (n == (double)(long long)n)
    {
      value = std::to_string((long long)n);
    }
    else
    {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%g", n);
      value = buf;
    }
  }
  else
  {
    value = luaL_checkstring(L, 2);
  }
  editor->config.set(key, value);
  // Live config: every setting is re-applied to editor state immediately, so
  // config.set() takes effect without a restart (and config.lua / init.lua
  // are full config sources).
  editor->apply_config_live();
  editor->config.save();
  if (has_event_subscribers("config.changed"))
  {
    const std::string k = key;
    const std::string v = value;
    emit_event_bus("config.changed",
                   [&](lua_State *L)
                   {
                     lua_push_str_field(L, "key", k);
                     lua_push_str_field(L, "value", v);
                   });
  }
}

void LuaAPI::config_unset_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const std::string key = luaL_checkstring(L, 1);
  editor->config.unset(key);
  editor->apply_config_live();
  editor->config.save();
  if (has_event_subscribers("config.changed"))
  {
    const std::string k = key;
    emit_event_bus("config.changed",
                   [&](lua_State *L)
                   {
                     lua_push_str_field(L, "key", k);
                     lua_push_bool_field(L, "removed", true);
                   });
  }
}

void LuaAPI::config_has_from_lua(lua_State *L)
{
  lua_pushboolean(L, editor && editor->config.has(luaL_optstring(L, 1, "")));
}

void LuaAPI::push_config_keys(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const auto &k : editor->config.keys())
  {
    lua_pushstring(L, k.c_str());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_config_path(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->config.path().c_str());
}

void LuaAPI::push_theme_current(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->config.get("color_scheme", "dark").c_str());
}

void LuaAPI::push_editor_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_str_field(L, "theme", editor->config.get("color_scheme", "dark"));
  lua_push_str_field(L, "path", current_file());
  lua_push_int_field(L, "buffers", (long long)editor->buffers.size());
  int line = 1, column = 1, line_count = 0;
  std::string word;
  bool modified = false;
  bool has_buffer =
      editor->current_buffer >= 0 && editor->current_buffer < (int)editor->buffers.size();
  if (has_buffer)
  {
    FileBuffer &buf = editor->buffers[(size_t)editor->current_buffer];
    modified = buf.modified;
    line = buf.cursor.y + 1;
    column = buf.cursor.x + 1;
    line_count = (int)buf.line_count();
    lua_push_int_field(L, "buffer", (long long)editor->current_buffer + 1);
    // Plain identifier under the cursor (word chars: alnum / underscore).
    if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count())
    {
      const std::string &text = buf.line(buf.cursor.y);
      const int x = std::clamp(buf.cursor.x, 0, (int)text.size());
      auto is_word_char = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
      int start = x;
      while (start > 0 && is_word_char(text[(size_t)start - 1]))
        start--;
      int end = x;
      while (end < (int)text.size() && is_word_char(text[(size_t)end]))
        end++;
      if (start < end)
      {
        word = text.substr((size_t)start, (size_t)(end - start));
      }
    }
  }
  lua_push_int_field(L, "line", line);
  lua_push_int_field(L, "column", column);
  lua_push_int_field(L, "line_count", line_count);
  lua_push_str_field(L, "word", word);
  lua_push_bool_field(L, "modified", modified);
}
