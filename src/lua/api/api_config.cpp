// Lua config + editor-info payloads: typed config get/set/unset/has, the
// config keys/path pushers, the current-theme pusher and the editor status
// summary. Split out of api_core.cpp.
#include "core/editor.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"

#include "core/options.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace
{
  // Which option a call names and where it lands. The indexes are 0-based here,
  // as the editor stores buffers and panes; the Lua API reports them 1-based,
  // and -1 means "the one in front".
  struct OptionTarget
  {
    std::string key;
    jot_options::Scope scope = jot_options::Scope::Global;
    int buffer = -1;
    int pane = -1;
    // The call asked for the value in force rather than for one scope's own:
    // a local value when the buffer or the window holds one, else the global
    // one, which is what makes an unset local read as the session's value
    // instead of as nil.
    bool effective = false;
  };

  // The store's spelling of a Lua value, the same conversion config.set has
  // always done: a boolean is "true"/"false", a number keeps its digits.
  std::string lua_value_string(lua_State *L, int index)
  {
    if (lua_isboolean(L, index))
    {
      return lua_toboolean(L, index) ? "true" : "false";
    }
    if (lua_isnumber(L, index))
    {
      const double n = lua_tonumber(L, index);
      if (n == (double)(long long)n)
      {
        return std::to_string((long long)n);
      }
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%g", n);
      return buf;
    }
    return luaL_checkstring(L, index);
  }

  // A field the settings catalog declares for a key, or "". The catalog is
  // runtime/lua/features/settings/catalog.lua, which is the schema the settings
  // menu reads too; a key it does not know falls back to the native scope list
  // (core/options.h) and to the type its value reads as.
  std::string catalog_field(Editor *editor, const std::string &key, const char *field)
  {
    return editor ? editor->policy_str("settings.catalog", key, field, "") : std::string();
  }

  jot_options::Scope option_scope(Editor *editor, const std::string &key)
  {
    const std::string declared = catalog_field(editor, key, "scope");
    if (declared == "buffer")
    {
      return jot_options::Scope::Buffer;
    }
    if (declared == "window")
    {
      return jot_options::Scope::Window;
    }
    if (declared == "global")
    {
      return jot_options::Scope::Global;
    }
    return jot_options::scope_of(key);
  }

  // The type name the schema gives a key, or the one its value reads as.
  std::string option_type_name(Editor *editor, const std::string &key, const std::string &value)
  {
    const std::string declared = catalog_field(editor, key, "type");
    return declared.empty() ? jot_options::inferred_type_name(value) : declared;
  }

  // A value stored on one buffer or pane, when it has one.
  bool local_value_in(const std::map<std::string, std::string> &values,
                      const std::string &key,
                      std::string &out)
  {
    const auto it = values.find(key);
    if (it == values.end())
    {
      return false;
    }
    out = it->second;
    return true;
  }

  // Pushes a stored value as the type its key takes: a bool as a boolean, an
  // int as a number, everything else as the string it is stored as.
  void push_typed_value(lua_State *L, const std::string &type, const std::string &value)
  {
    if (type == "bool")
    {
      lua_pushboolean(L, value == "true" || value == "1" || value == "on" ? 1 : 0);
      return;
    }
    if (type == "int")
    {
      lua_pushnumber(L, std::strtod(value.c_str(), nullptr));
      return;
    }
    lua_pushlstring(L, value.data(), value.size());
  }

  // Reads the target a scoped call named: an opts table {scope = "...",
  // buffer = n, pane = n}, or the bare index the two namespaces take
  // (jot.buffer.set_option(key, value, 3)). `namespace_scope` is "buffer" or
  // "window" for those namespaces and null for jot.config.
  //
  // A namespace call answers for its own scope by default, which is what makes
  // jot.buffer.get_option read the value in force for that buffer, and a key
  // whose scope is the other one is refused instead of quietly reading the
  // wrong store. jot.config with a table asks the same question by default and
  // can name any scope the key takes.
  bool parse_option_target(Editor *editor,
                           lua_State *L,
                           int opts_index,
                           const char *namespace_scope,
                           OptionTarget &target,
                           std::string &error)
  {
    target.buffer = 0;
    target.pane = 0;
    target.effective = false;
    std::string scope_name;
    if (opts_index > 0 && lua_istable(L, opts_index))
    {
      lua_getfield(L, opts_index, "scope");
      if (lua_type(L, -1) == LUA_TSTRING)
      {
        scope_name = lua_tostring(L, -1);
      }
      lua_pop(L, 1);
      lua_getfield(L, opts_index, "buffer");
      if (lua_isnumber(L, -1))
      {
        target.buffer = (int)lua_tointeger(L, -1);
      }
      lua_pop(L, 1);
      lua_getfield(L, opts_index, "pane");
      if (lua_isnumber(L, -1))
      {
        target.pane = (int)lua_tointeger(L, -1);
      }
      lua_pop(L, 1);
    }
    else if (opts_index > 0 && lua_isnumber(L, opts_index))
    {
      // The index a namespace call names, 1-based, 0 (or nothing) for the one
      // in front.
      const int named = (int)lua_tointeger(L, opts_index);
      if (namespace_scope && std::string(namespace_scope) == "window")
      {
        target.pane = named;
      }
      else
      {
        target.buffer = named;
      }
    }

    const jot_options::Scope own = option_scope(editor, target.key);
    if (scope_name.empty())
    {
      // No scope named: the key's own one, read as the value in force (its
      // buffer's or its window's value when there is one, else the global).
      const jot_options::Scope wanted = namespace_scope && std::string(namespace_scope) == "window"
                                            ? jot_options::Scope::Window
                                            : jot_options::Scope::Buffer;
      if (namespace_scope && own != wanted)
      {
        error = "\"" + target.key + "\" has no " + namespace_scope + " value";
        return false;
      }
      target.scope = own;
      target.effective = true;
      return true;
    }
    if (scope_name == "effective")
    {
      target.scope = own;
      target.effective = true;
      return true;
    }
    jot_options::Scope parsed;
    if (!jot_options::scope_from_name(scope_name, parsed))
    {
      error = "unknown scope \"" + scope_name + "\"";
      return false;
    }
    // "global" from a scoped reader means the store's own value; a local scope
    // means the buffer's or the window's value alone. Asking for the scope a key
    // does not have is an error rather than a silent fall back: the answer would
    // otherwise be the global value wearing the name of a local one.
    if (parsed != jot_options::Scope::Global && parsed != own)
    {
      error = "\"" + target.key + "\" has no " + jot_options::scope_name(parsed) + " value";
      return false;
    }
    target.scope = parsed;
    return true;
  }
} // namespace

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

int LuaAPI::option_buffer_index(int named) const
{
  if (!editor)
  {
    return -1;
  }
  const int index = named > 0 ? named - 1 : editor->current_buffer;
  return (index >= 0 && index < (int)editor->buffers.size()) ? index : -1;
}

int LuaAPI::option_pane_index(int named) const
{
  if (!editor)
  {
    return -1;
  }
  const int index = named > 0 ? named - 1 : editor->current_pane;
  return (index >= 0 && index < (int)editor->panes.size()) ? index : -1;
}

bool LuaAPI::buffer_option_local(int named, const std::string &key, std::string &out) const
{
  const int index = option_buffer_index(named);
  return index >= 0 && local_value_in(editor->buffers[(size_t)index].options, key, out);
}

bool LuaAPI::window_option_local(int named, const std::string &key, std::string &out) const
{
  const int index = option_pane_index(named);
  return index >= 0 && local_value_in(editor->panes[(size_t)index].options, key, out);
}

void LuaAPI::push_config_info(lua_State *L)
{
  // jot.config.info() answers the schema, and jot.config.info(key) answers it for
  // one key: what the key is called in the menu, what type it takes, which scope
  // it has (a buffer, a window or the session), what it defaults to, whether a
  // change reaches the editor without a restart, and the value in force. A key
  // with no catalog entry is still described -- by its name, the type its value
  // reads as and the global scope -- because a plugin's own key is a key.
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  const char *named = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : nullptr;

  // One key: {name, label, type, scope, live, default, value, known, ...}.
  auto push_one = [&](const std::string &key)
  {
    const std::string value = editor->config.get(key, "");
    const std::string type = option_type_name(editor, key, value);
    const bool known = editor->config.has(key);
    lua_newtable(L);
    lua_push_str_field(L, "name", key);
    const std::string label = catalog_field(editor, key, "label");
    lua_push_str_field(L, "label", label.empty() ? key : label);
    lua_push_str_field(L, "type", type);
    lua_push_str_field(L, "scope", jot_options::scope_name(option_scope(editor, key)));
    // Every key this build ships reaches the editor without a restart: a set
    // runs through Editor::apply_config_live, whether it came from config.lua,
    // a plugin or the settings menu. The field is here because it is the
    // question a plugin asks; a key that cannot be re-applied would answer
    // false from this line.
    lua_push_bool_field(L, "live", true);
    // The built-in default is what the store's own defaults say, not the value
    // in force: a key the user or a plugin changed still reports what it is
    // meant to be, which is the question this field answers.
    push_typed_value(L, type, editor->config.default_of(key));
    lua_setfield(L, -2, "default");
    push_typed_value(L, type, value);
    lua_setfield(L, -2, "value");
    lua_push_bool_field(L, "known", known);
    const std::vector<std::string> choices =
        editor->policy_str_list("settings.catalog", key, "options");
    if (!choices.empty())
    {
      lua_newtable(L);
      int i = 1;
      for (const std::string &choice : choices)
      {
        lua_pushlstring(L, choice.data(), choice.size());
        lua_rawseti(L, -2, i++);
      }
      lua_setfield(L, -2, "options");
    }
    if (catalog_field(editor, key, "min") != "")
    {
      lua_push_int_field(L, "min", editor->policy_int("settings.catalog", key, "min", 0));
      lua_push_int_field(L, "max", editor->policy_int("settings.catalog", key, "max", 0));
      lua_push_int_field(L, "step", editor->policy_int("settings.catalog", key, "step", 1));
    }
  };

  if (named)
  {
    push_one(named);
    return;
  }
  lua_newtable(L);
  for (const std::string &key : editor->config.keys())
  {
    push_one(key);
    lua_setfield(L, -2, key.c_str());
  }
}

void LuaAPI::push_config_list(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
  {
    return;
  }
  std::vector<std::string> keys = editor->config.keys();
  std::sort(keys.begin(), keys.end());
  int n = 1;
  for (const std::string &key : keys)
  {
    lua_pushlstring(L, key.data(), key.size());
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::option_get_from_lua(lua_State *L, const char *namespace_scope)
{
  // jot.config.get(key, opts) / jot.buffer.get_option(key, buffer) /
  // jot.window.get_option(key, pane). A scope the key does not have is an error
  // with a message, so a plugin that asked the wrong question hears about it.
  // Every path pushes the same three: the value, whether the key is set at all,
  // and the message when the call was refused. The count the binding returns is
  // the count pushed here, so a path that pushed fewer would hand Lua whatever
  // was in the slot it read next.
  if (!editor)
  {
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return;
  }
  OptionTarget target;
  target.key = luaL_optstring(L, 1, "");
  std::string error;
  if (!parse_option_target(editor, L, 2, namespace_scope, target, error))
  {
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushlstring(L, error.data(), error.size());
    return;
  }
  std::string stored;
  const bool have_local = target.scope == jot_options::Scope::Buffer
                              ? buffer_option_local(target.buffer, target.key, stored)
                              : (target.scope == jot_options::Scope::Window
                                     ? window_option_local(target.pane, target.key, stored)
                                     : false);
  // `jot.config.get` is the store's reader and answers the stored spelling
  // whatever the scope; `jot.buffer.get_option` / `jot.window.get_option` answer
  // the type the key takes, so a plugin reads a width as a number and a toggle
  // as a boolean and does not parse strings it just wrote itself.
  const bool typed = namespace_scope != nullptr;
  if (have_local)
  {
    const std::string type =
        option_type_name(editor, target.key, editor->config.get(target.key, ""));
    if (typed)
    {
      push_typed_value(L, type, stored);
    }
    else
    {
      lua_pushlstring(L, stored.data(), stored.size());
    }
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return;
  }
  if (target.scope != jot_options::Scope::Global && !target.effective)
  {
    // The buffer (or window) has no value of its own, which is a nil read and
    // not an error: the local scope is empty until something sets it. Only an
    // explicit local scope reads this way -- a call that asked for the value in
    // force goes on to the global one below.
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return;
  }
  if (!editor->config.has(target.key))
  {
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return;
  }
  const std::string value = editor->config.get(target.key, "");
  if (typed)
  {
    push_typed_value(L, option_type_name(editor, target.key, value), value);
  }
  else
  {
    lua_pushlstring(L, value.data(), value.size());
  }
  lua_pushboolean(L, 1);
  lua_pushnil(L);
}

void LuaAPI::option_set_from_lua(lua_State *L, const char *namespace_scope)
{
  // jot.config.set(key, value, opts) / jot.buffer.set_option(key, value, buffer)
  // / jot.window.set_option(key, value, pane). A local write stays in memory
  // with the buffer or the pane; only the global scope is written to
  // settings.conf, which is why the scope is part of the call and not a guess.
  if (!editor)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "no editor");
    return;
  }
  OptionTarget target;
  target.key = luaL_checkstring(L, 1);
  const std::string value = lua_value_string(L, 2);
  std::string error;
  if (!parse_option_target(editor, L, 3, namespace_scope, target, error))
  {
    lua_pushboolean(L, 0);
    lua_pushlstring(L, error.data(), error.size());
    return;
  }
  // Two pushes on every path (whether it was written, and the message when it
  // was not), matching the two the binding returns.
  if (target.scope == jot_options::Scope::Global)
  {
    editor->config.set(target.key, value);
    editor->apply_config_live();
    editor->config.save();
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return;
  }
  const int index = target.scope == jot_options::Scope::Buffer ? option_buffer_index(target.buffer)
                                                               : option_pane_index(target.pane);
  if (index < 0)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(
        L, target.scope == jot_options::Scope::Buffer ? "no such buffer" : "no such window");
    return;
  }
  std::map<std::string, std::string> &values = target.scope == jot_options::Scope::Buffer
                                                   ? editor->buffers[(size_t)index].options
                                                   : editor->panes[(size_t)index].options;
  values[target.key] = value;
  editor->needs_redraw = true;
  lua_pushboolean(L, 1);
  lua_pushnil(L);
}

void LuaAPI::option_unset_from_lua(lua_State *L, const char *namespace_scope)
{
  // Drops a local value (the buffer or the window goes back to the global one),
  // or unsets the global key in the store.
  if (!editor)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "no editor");
    return;
  }
  OptionTarget target;
  target.key = luaL_checkstring(L, 1);
  std::string error;
  if (!parse_option_target(editor, L, 2, namespace_scope, target, error))
  {
    lua_pushboolean(L, 0);
    lua_pushlstring(L, error.data(), error.size());
    return;
  }
  if (target.scope == jot_options::Scope::Global)
  {
    editor->config.unset(target.key);
    editor->apply_config_live();
    editor->config.save();
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return;
  }
  const int index = target.scope == jot_options::Scope::Buffer ? option_buffer_index(target.buffer)
                                                               : option_pane_index(target.pane);
  if (index < 0)
  {
    lua_pushboolean(L, 0);
    lua_pushstring(
        L, target.scope == jot_options::Scope::Buffer ? "no such buffer" : "no such window");
    return;
  }
  if (target.scope == jot_options::Scope::Buffer)
  {
    editor->buffers[(size_t)index].options.erase(target.key);
  }
  else
  {
    editor->panes[(size_t)index].options.erase(target.key);
  }
  editor->needs_redraw = true;
  lua_pushboolean(L, 1);
  lua_pushnil(L);
}

void LuaAPI::push_theme_current(lua_State *L)
{
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->config.get("color_scheme", "jot").c_str());
}

void LuaAPI::push_editor_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  lua_push_str_field(L, "theme", editor->config.get("color_scheme", "jot"));
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
