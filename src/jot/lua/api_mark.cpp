// LuaAPI mark surface: set / query / delete / jump marks and the mark
// list the buffer-local mark popup renders.
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
  editor->record_jump();
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

