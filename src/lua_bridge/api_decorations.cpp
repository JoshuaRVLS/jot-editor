// Lua bridge for anchored decorations (extmark-style): jot.decoration.*.
// The l_* C functions live in api_bindings.cpp; the engine-facing work is
// here. Positions are 1-based line/column like the rest of the API, and are
// converted to the 0-based byte-offset model of core/app/decorations.cpp.
#include "core/app/decorations.h"
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <cstdint>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace
{
  std::uint64_t next_decoration_id = 1;
} // namespace

std::uint64_t LuaAPI::decoration_set(int buffer_idx, lua_State *L, int opts_index)
{
  if (!editor || buffer_idx < 0 || buffer_idx >= (int)editor->buffers.size())
    return 0;
  FileBuffer &buf = editor->buffers[(size_t)buffer_idx];
  if (lua_gettop(L) < opts_index || !lua_istable(L, opts_index))
    return 0;
  // A decoration placed in current-text space must not be pushed through a
  // still-pending edit window (decorations in the vector are expressed in
  // pre-edit space until the renderer anchors them), so absorb that window
  // first.
  if (buf.decoration_dirty)
  {
    editor->ensure_decorations_anchored(buf);
  }
  Decoration d;
  d.row = jot_lua::table_int(L, opts_index, "row", 1) - 1;
  d.col = jot_lua::table_int(L, opts_index, "col", 1) - 1;
  d.width = jot_lua::table_int(L, opts_index, "width", 0);
  d.priority = jot_lua::table_int(L, opts_index, "priority", 50);
  d.fg = jot_lua::table_int(L, opts_index, "fg", -1);
  d.bg = jot_lua::table_int(L, opts_index, "bg", -1);
  d.hl = jot_lua::table_string(L, opts_index, "hl", "");
  d.underline = std::clamp(jot_lua::table_int(L, opts_index, "underline", 0), 0, 2);
  d.underline_fg = jot_lua::table_int(L, opts_index, "underline_fg", -1);
  d.underline_hl = jot_lua::table_string(L, opts_index, "underline_hl", "");
  d.right_gravity = jot_lua::table_bool(L, opts_index, "right_gravity", true);
  d.virt_text = jot_lua::table_string(L, opts_index, "virt_text", "");
  d.virt_fg = jot_lua::table_int(L, opts_index, "virt_fg", -1);
  d.virt_bg = jot_lua::table_int(L, opts_index, "virt_bg", -1);
  d.virt_hl = jot_lua::table_string(L, opts_index, "virt_hl", "");
  if (d.row < 0 || d.col < 0 || d.width < 0)
  {
    return 0;
  }
  const long long update_id = jot_lua::table_int(L, opts_index, "id", -1);
  if (update_id >= 0)
  {
    decoration_erase(buf, (std::uint64_t)update_id);
    d.id = (std::uint64_t)update_id;
  }
  else
  {
    d.id = next_decoration_id++;
  }
  decoration_insert(buf, std::move(d));
  return d.id;
}

bool LuaAPI::decoration_delete(int buffer_idx, std::uint64_t id)
{
  if (!editor || buffer_idx < 0 || buffer_idx >= (int)editor->buffers.size())
    return false;
  return decoration_erase(editor->buffers[(size_t)buffer_idx], id);
}

void LuaAPI::decoration_clear(int buffer_idx)
{
  if (!editor || buffer_idx < 0 || buffer_idx >= (int)editor->buffers.size())
    return;
  FileBuffer &buf = editor->buffers[(size_t)buffer_idx];
  if (buf.decoration_dirty)
  {
    editor->ensure_decorations_anchored(buf);
  }
  buf.decorations.clear();
  buf.decoration_base_valid = false;
  buf.decoration_dirty = false;
}

void LuaAPI::decoration_list(int buffer_idx, lua_State *L)
{
  if (!editor || buffer_idx < 0 || buffer_idx >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)buffer_idx];
  if (buf.decoration_dirty)
  {
    editor->ensure_decorations_anchored(buf);
  }
  lua_newtable(L);
  int n = 1;
  for (const Decoration &d : buf.decorations)
  {
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)d.id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, d.row + 1);
    lua_setfield(L, -2, "row");
    lua_pushinteger(L, d.col + 1);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, d.width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, d.priority);
    lua_setfield(L, -2, "priority");
    lua_pushboolean(L, d.right_gravity);
    lua_setfield(L, -2, "right_gravity");
    if (d.fg != -1)
    {
      lua_pushinteger(L, d.fg);
      lua_setfield(L, -2, "fg");
    }
    if (d.bg != -1)
    {
      lua_pushinteger(L, d.bg);
      lua_setfield(L, -2, "bg");
    }
    if (!d.hl.empty())
    {
      lua_pushstring(L, d.hl.c_str());
      lua_setfield(L, -2, "hl");
    }
    if (d.underline != 0)
    {
      lua_pushinteger(L, d.underline);
      lua_setfield(L, -2, "underline");
    }
    if (d.underline_fg != -1)
    {
      lua_pushinteger(L, d.underline_fg);
      lua_setfield(L, -2, "underline_fg");
    }
    if (!d.underline_hl.empty())
    {
      lua_pushstring(L, d.underline_hl.c_str());
      lua_setfield(L, -2, "underline_hl");
    }
    if (!d.virt_text.empty())
    {
      lua_pushstring(L, d.virt_text.c_str());
      lua_setfield(L, -2, "virt_text");
    }
    if (d.virt_fg != -1)
    {
      lua_pushinteger(L, d.virt_fg);
      lua_setfield(L, -2, "virt_fg");
    }
    if (d.virt_bg != -1)
    {
      lua_pushinteger(L, d.virt_bg);
      lua_setfield(L, -2, "virt_bg");
    }
    if (!d.virt_hl.empty())
    {
      lua_pushstring(L, d.virt_hl.c_str());
      lua_setfield(L, -2, "virt_hl");
    }
    lua_rawseti(L, -2, n++);
  }
}