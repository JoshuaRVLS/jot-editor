// Lua bridge: named policy providers (jot.policy) and the bible reference
// policy built on them.
//
// The boundary this file implements is the one the whole Lua-first direction
// rests on: C++ owns mechanism (network, JSON, rendering, the buffers), Lua
// owns policy (what a reference is, what it is called, which books exist). A
// provider is a Lua table registered under a name, and the native call sites
// ask it first, falling back to their own implementation when it is absent or
// answers with something the native side cannot accept.
//
// The first provider is bible.reference, ported from src/extras/bible.cpp into
// runtime/lua/features/bible/reference.lua. The contract is deliberately narrow:
// the book travels as the API's own id ("JHN"), never as a pointer or an index,
// so the native canon stays the authority on which books exist and a provider
// cannot introduce a book the reader cannot fetch.
#include "core/editor.h"
#include "extras/bible.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace
{
  // The call a provider gets for a reference it did not parse itself: the same
  // fields, as a table.
  void push_reference(lua_State *L, const LuaAPI::BibleRefView &ref)
  {
    lua_newtable(L);
    lua_pushlstring(L, ref.book_id.data(), ref.book_id.size());
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, ref.chapter);
    lua_setfield(L, -2, "chapter");
    lua_pushboolean(L, ref.has_verse);
    lua_setfield(L, -2, "has_verse");
    lua_pushinteger(L, ref.verse_first);
    lua_setfield(L, -2, "verse_first");
    lua_pushinteger(L, ref.verse_last);
    lua_setfield(L, -2, "verse_last");
  }

  // Reads the fields back. False when the shape is wrong, which the callers
  // treat as "this provider did not answer" and hand to the native path.
  bool read_reference(lua_State *L, int index, LuaAPI::BibleRefView &out)
  {
    if (!lua_istable(L, index))
    {
      return false;
    }
    const std::string id = jot_lua::table_string(L, index, "id", "");
    if (id.empty())
    {
      return false;
    }
    out.book_id = id;
    out.chapter = jot_lua::table_int(L, index, "chapter", 0);
    out.has_verse = jot_lua::table_bool(L, index, "has_verse", false);
    out.verse_first = jot_lua::table_int(L, index, "verse_first", 0);
    out.verse_last = jot_lua::table_int(L, index, "verse_last", 0);
    return out.chapter > 0;
  }
} // namespace

void LuaAPI::set_lua_policy(lua_State *L, const char *name, int index)
{
  if (!name || !*name)
  {
    return;
  }
  auto existing = lua_policies_.find(name);
  if (existing != lua_policies_.end())
  {
    if (existing->second >= 0)
    {
      luaL_unref(L, LUA_REGISTRYINDEX, existing->second);
    }
    lua_policies_.erase(existing);
  }
  if (lua_isnoneornil(L, index))
  {
    return; // unregistering
  }
  if (!lua_istable(L, index))
  {
    luaL_error(L, "jot.policy.set: expected a table of functions");
    return;
  }
  lua_pushvalue(L, index);
  lua_policies_[name] = luaL_ref(L, LUA_REGISTRYINDEX);
}

bool LuaAPI::has_lua_policy(const char *name) const
{
  return name && lua_policies_.find(name) != lua_policies_.end();
}

int LuaAPI::push_lua_policy_table(const char *name)
{
  if (!lua_state)
  {
    return 0;
  }
  auto it = lua_policies_.find(name);
  if (it == lua_policies_.end() || it->second < 0)
  {
    return 0;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  if (!lua_istable(L, -1))
  {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

bool LuaAPI::lua_policy_has_entry(const char *name, const std::string &key)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  // The readers below all follow the same rule: a table is read on the main
  // thread only, and anything missing answers like "no provider at all".
  if (!L || !editor || !editor->event_loop_.is_main_thread() || !push_lua_policy_table(name))
  {
    return false;
  }
  const int top = lua_gettop(L);
  lua_getfield(L, -1, key.c_str());
  const bool found = lua_istable(L, -1);
  lua_settop(L, top - 1);
  return found;
}

bool LuaAPI::lua_policy_has_field(const char *name, const std::string &key, const char *field)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !editor || !editor->event_loop_.is_main_thread() || !push_lua_policy_table(name))
  {
    return false;
  }
  const int top = lua_gettop(L);
  lua_getfield(L, -1, key.c_str());
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top - 1);
    return false;
  }
  lua_getfield(L, -1, field);
  const bool present = !lua_isnil(L, -1);
  lua_settop(L, top - 1);
  return present;
}

std::string LuaAPI::lua_policy_str(const char *name,
                                   const std::string &key,
                                   const char *field,
                                   const std::string &fallback)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !editor || !editor->event_loop_.is_main_thread() || !push_lua_policy_table(name))
  {
    return fallback;
  }
  const int top = lua_gettop(L);
  lua_getfield(L, -1, key.c_str());
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top - 1);
    return fallback;
  }
  lua_getfield(L, -1, field);
  size_t size = 0;
  const char *value = lua_tolstring(L, -1, &size);
  const std::string out = value ? std::string(value, size) : fallback;
  lua_settop(L, top - 1);
  return out;
}

int LuaAPI::lua_policy_int(const char *name, const std::string &key, const char *field, int fallback)
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !editor || !editor->event_loop_.is_main_thread() || !push_lua_policy_table(name))
  {
    return fallback;
  }
  const int top = lua_gettop(L);
  lua_getfield(L, -1, key.c_str());
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top - 1);
    return fallback;
  }
  lua_getfield(L, -1, field);
  const int out = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : fallback;
  lua_settop(L, top - 1);
  return out;
}

std::vector<std::string> LuaAPI::lua_policy_str_list(const char *name,
                                                     const std::string &key,
                                                     const char *field)
{
  std::vector<std::string> out;
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !editor || !editor->event_loop_.is_main_thread() || !push_lua_policy_table(name))
  {
    return out;
  }
  const int top = lua_gettop(L);
  lua_getfield(L, -1, key.c_str());
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top - 1);
    return out;
  }
  lua_getfield(L, -1, field);
  if (lua_istable(L, -1))
  {
    const int count = (int)lua_rawlen(L, -1);
    for (int i = 1; i <= count; i++)
    {
      lua_rawgeti(L, -1, i);
      size_t size = 0;
      const char *value = lua_tolstring(L, -1, &size);
      if (value)
      {
        out.emplace_back(value, size);
      }
      lua_pop(L, 1);
    }
  }
  lua_settop(L, top - 1);
  return out;
}

int LuaAPI::push_lua_policy_field(const char *name, const char *field)
{
  if (!lua_state)
  {
    return 0;
  }
  auto it = lua_policies_.find(name);
  if (it == lua_policies_.end() || it->second < 0)
  {
    return 0;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
  lua_getfield(L, -1, field);
  if (!lua_isfunction(L, -1))
  {
    lua_pop(L, 2);
    return 0;
  }
  lua_remove(L, -2); // drop the provider table, leaving its function
  return 1;
}

void LuaAPI::lua_policy_names_to_lua(lua_State *L)
{
  lua_newtable(L);
  int index = 1;
  // Sorted so a script comparing two runs sees the same order.
  std::vector<std::string> names;
  for (const auto &kv : lua_policies_)
  {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());
  for (const std::string &name : names)
  {
    lua_pushlstring(L, name.data(), name.size());
    lua_rawseti(L, -2, index++);
  }
}

void LuaAPI::clear_lua_policies()
{
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (L)
  {
    for (const auto &kv : lua_policies_)
    {
      if (kv.second >= 0)
      {
        luaL_unref(L, LUA_REGISTRYINDEX, kv.second);
      }
    }
  }
  lua_policies_.clear();
}

bool LuaAPI::bible_policy_parse(const std::string &text, BibleRefView &out)
{
  if (!editor || !editor->event_loop_.is_main_thread())
  {
    return false;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !push_lua_policy_field("bible.reference", "parse"))
  {
    return false;
  }
  const int top = lua_gettop(L);
  lua_pushlstring(L, text.data(), text.size());
  if (lua_pcall(L, 1, 1, 0) != LUA_OK)
  {
    std::cerr << "Lua bible.reference.parse error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top - 1);
    return false;
  }
  const bool ok = read_reference(L, -1, out) && !out.book_id.empty();
  lua_settop(L, top - 1);
  return ok;
}

bool LuaAPI::bible_policy_label(const BibleRefView &ref, std::string &out)
{
  if (!editor || !editor->event_loop_.is_main_thread())
  {
    return false;
  }
  lua_State *L = static_cast<lua_State *>(lua_state);
  if (!L || !push_lua_policy_field("bible.reference", "label"))
  {
    return false;
  }
  const int top = lua_gettop(L);
  push_reference(L, ref);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK)
  {
    std::cerr << "Lua bible.reference.label error: " << lua_tostring(L, -1) << "\n";
    lua_settop(L, top - 1);
    return false;
  }
  const char *value = lua_tostring(L, -1);
  const bool ok = value != nullptr && *value != '\0';
  if (ok)
  {
    out = value;
  }
  lua_settop(L, top - 1);
  return ok;
}
