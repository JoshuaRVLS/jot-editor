// jot.undo: the undo tree of the buffer in front, for a panel to draw. The tree
// itself lives in buffers/undo.cpp; this is the read side and the jump off it.
#include "core/editor.h"
#include "lua/api/api.h"
#include "lua/api/api_internal.h"

#include <cstddef>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

namespace
{
  // What a state's save marker says: 1 for the state the last write landed on,
  // 2 for one saved and written again since, 0 for never saved. The older marks
  // stay visible because a write is a point in the history worth walking to.
  int saved_marker(const FileBuffer &buf, const FileBuffer::UndoNode &node)
  {
    if (node.saved_at == 0)
    {
      return 0;
    }
    return node.saved_at == buf.undo_writes ? 1 : 2;
  }
} // namespace

void LuaAPI::push_undo_tree(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
  {
    return;
  }
  FileBuffer &buf = editor->get_buffer();
  editor->undo_reconcile(buf);

  lua_newtable(L);
  int row = 0;
  for (int i = 0; i < (int)buf.undo_nodes.size(); i++)
  {
    const FileBuffer::UndoNode &node = buf.undo_nodes[(size_t)i];
    lua_newtable(L);
    lua_push_int_field(L, "seq", node.seq);
    lua_push_int_field(L, "time", node.time_ms / 1000); // epoch seconds
    lua_push_int_field(L, "saved", saved_marker(buf, node));
    lua_push_bool_field(L, "current", i == buf.undo_current);
    if (node.parent >= 0)
    {
      lua_push_int_field(L, "parent", buf.undo_nodes[(size_t)node.parent].seq);
    }
    else
    {
      lua_pushnil(L);
      lua_setfield(L, -2, "parent");
    }
    lua_rawseti(L, -2, ++row);
  }
  lua_setfield(L, -2, "states");

  lua_push_int_field(
      L, "current", buf.undo_current >= 0 ? buf.undo_nodes[(size_t)buf.undo_current].seq : 0);
  lua_push_int_field(L, "last", buf.undo_seq_last);
  lua_push_int_field(L, "writes", buf.undo_writes);
  lua_push_int_field(L, "bytes", (long long)buf.undo_tree_bytes);
}

void LuaAPI::push_undo_state_lines(lua_State *L)
{
  const int seq = (int)luaL_checkinteger(L, 1);
  if (!editor)
  {
    lua_pushnil(L);
    return;
  }
  std::vector<std::string> lines;
  if (!editor->undo_state_lines(editor->get_buffer(), seq, lines))
  {
    // Unreadable: a state this build cannot walk to, or a buffer still on disk
    // behind a lazy load.
    lua_pushnil(L);
    return;
  }
  lua_newtable(L);
  for (std::size_t i = 0; i < lines.size(); i++)
  {
    lua_pushlstring(L, lines[i].data(), lines[i].size());
    lua_rawseti(L, -2, (int)i + 1);
  }
}

void LuaAPI::undo_tree_goto_from_lua(lua_State *L)
{
  const int seq = (int)luaL_checkinteger(L, 1);
  lua_pushboolean(L, editor && editor->undo_tree_goto(seq));
}

void LuaAPI::undo_tree_clear_from_lua(lua_State *L)
{
  (void)L;
  if (!editor)
  {
    return;
  }
  editor->get_buffer().clear_undo_history();
  editor->needs_redraw = true;
}
