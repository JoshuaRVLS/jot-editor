// Viewport + file-tree Lua payloads: editor/window geometry, scroll/reveal
// controls, the file-tree root/tree/children pushers and buffer selection
// setters/clearers. Split out of api_core.cpp.
#include "editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"

#include <algorithm>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

void LuaAPI::push_viewport_info(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const int win_w = editor->ui ? editor->ui->get_render_width() : 0;
  const int win_h = editor->ui ? editor->ui->get_height() : 0;
  lua_newtable(L);
  lua_push_int_field(L, "width", win_w);
  lua_push_int_field(L, "height", win_h);
  lua_push_int_field(L, "status_height", editor->status_height);
  lua_push_int_field(L, "tab_height", editor->tab_height);
  lua_setfield(L, -2, "window");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_sidebar);
  lua_push_int_field(L, "width", editor->sidebar_width);
  lua_setfield(L, -2, "sidebar");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_minimap);
  lua_push_int_field(L, "width", editor->minimap_width);
  lua_setfield(L, -2, "minimap");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_integrated_terminal);
  lua_push_int_field(L, "height", editor->integrated_terminal_height);
  lua_setfield(L, -2, "terminal");
  lua_newtable(L);
  lua_push_bool_field(L, "visible", editor->show_right_panel);
  lua_push_int_field(L, "width", editor->right_panel_width);
  lua_setfield(L, -2, "right_panel");
  if (!editor->panes.empty())
  {
    const SplitPane &pane = editor->get_pane();
    lua_newtable(L);
    lua_push_int_field(L, "x", pane.x);
    lua_push_int_field(L, "y", pane.y);
    lua_push_int_field(L, "width", pane.w);
    lua_push_int_field(L, "height", pane.h);
    lua_push_int_field(L, "buffer", (long long)pane.buffer_id + 1);
    lua_push_bool_field(L, "active", pane.active);
    lua_setfield(L, -2, "pane");
    if (pane.buffer_id >= 0 && pane.buffer_id < (int)editor->buffers.size())
    {
      FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
      const int rows = std::max(0, pane.h - editor->tab_height - 1);
      const int line_count = (int)buf.line_count();
      const int first = buf.scroll_offset + 1;
      const int last = std::min(line_count, buf.scroll_offset + rows);
      lua_newtable(L);
      lua_push_int_field(L, "line_count", line_count);
      lua_push_int_field(L, "first_line", first);
      lua_push_int_field(L, "last_line", last);
      lua_push_int_field(L, "visible_lines", rows);
      lua_push_int_field(L, "cursor_line", (long long)buf.cursor.y + 1);
      lua_push_int_field(L, "cursor_col", (long long)buf.cursor.x + 1);
      lua_push_int_field(L, "scroll_x", buf.scroll_x);
      lua_setfield(L, -2, "buffer");
    }
  }
}

void LuaAPI::push_viewport_line_at(lua_State *L)
{
  if (!editor || editor->panes.empty())
  {
    lua_pushnil(L);
    return;
  }
  const int sy = (int)luaL_checkinteger(L, 1) - 1; // 0-based screen row
  const SplitPane &pane = editor->get_pane();
  const int top = pane.y + editor->tab_height;
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  if (sy < top || sy >= top + rows)
  {
    lua_pushnil(L);
    return;
  }
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    lua_pushnil(L);
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int line = buf.scroll_offset + (sy - top);
  if (line < 0 || line >= (int)buf.line_count())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushinteger(L, line + 1);
}

static void lua_push_file_node(lua_State *L, const FileNode &node)
{
  lua_newtable(L);
  lua_push_str_field(L, "name", node.name);
  lua_push_str_field(L, "path", node.path);
  lua_push_bool_field(L, "is_dir", node.is_dir);
  lua_push_bool_field(L, "expanded", node.expanded);
  lua_push_int_field(L, "depth", node.depth);
  lua_newtable(L);
  int cn = 1;
  for (const FileNode &child : node.children)
  {
    lua_push_file_node(L, child);
    lua_rawseti(L, -2, cn++);
  }
  lua_setfield(L, -2, "children");
}

void LuaAPI::push_filetree_root(lua_State *L)
{
  if (!editor || editor->root_dir.empty())
  {
    lua_pushnil(L);
    return;
  }
  lua_pushstring(L, editor->root_dir.c_str());
}

void LuaAPI::push_filetree_tree(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  int n = 1;
  for (const FileNode &node : editor->file_tree)
  {
    lua_push_file_node(L, node);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::push_filetree_children(lua_State *L)
{
  lua_newtable(L);
  if (!editor)
    return;
  const std::string wanted = luaL_optstring(L, 1, "");
  if (wanted.empty())
    return;
  const FileNode *found = nullptr;
  std::function<void(const FileNode &)> search = [&](const FileNode &node)
  {
    if (found)
      return;
    if (node.path == wanted)
    {
      found = &node;
      return;
    }
    for (const FileNode &child : node.children)
      search(child);
  };
  for (const FileNode &node : editor->file_tree)
    search(node);
  if (!found)
    return;
  int n = 1;
  for (const FileNode &child : found->children)
  {
    lua_push_file_node(L, child);
    lua_rawseti(L, -2, n++);
  }
}

void LuaAPI::viewport_scroll_top_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  const int line_count = (int)buf.line_count();
  const int target = (int)luaL_checkinteger(L, 1) - 1;
  const int max_top = std::max(0, line_count - rows);
  buf.scroll_offset = std::clamp(target, 0, max_top);
  buf.scroll_offset = std::min(buf.scroll_offset, std::max(0, line_count - 1));
  editor->needs_redraw = true;
}

void LuaAPI::viewport_scroll_lines_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  const int rows = std::max(0, pane.h - editor->tab_height - 1);
  const int line_count = (int)buf.line_count();
  const int delta = (int)luaL_checkinteger(L, 1);
  const int max_top = std::max(0, line_count - rows);
  buf.scroll_offset = std::clamp(buf.scroll_offset + delta, 0, max_top);
  editor->needs_redraw = true;
}

void LuaAPI::viewport_scroll_col_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  const SplitPane &pane = editor->get_pane();
  if (pane.buffer_id < 0 || pane.buffer_id >= (int)editor->buffers.size())
  {
    return;
  }
  FileBuffer &buf = editor->buffers[(size_t)pane.buffer_id];
  buf.scroll_x = std::max(0, (int)luaL_checkinteger(L, 1) - 1);
  editor->needs_redraw = true;
}

void LuaAPI::viewport_reveal_from_lua(lua_State *L)
{
  if (!editor || editor->panes.empty())
    return;
  editor->ensure_cursor_visible();
  editor->needs_redraw = true;
}

void LuaAPI::buffer_select_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 5);
  if (idx < 0)
    return;
  FileBuffer &buf = editor->buffers[(size_t)idx];
  const int line_count = (int)buf.line_count();
  if (line_count == 0)
    return;
  const int sl = (int)luaL_checkinteger(L, 1) - 1;
  const int sc = (int)luaL_checkinteger(L, 2) - 1;
  const int el = (int)luaL_checkinteger(L, 3) - 1;
  const int ec = (int)luaL_checkinteger(L, 4) - 1;
  buf.selection.start.y = std::clamp(sl, 0, line_count - 1);
  buf.selection.start.x = std::clamp(sc, 0, (int)buf.line(buf.selection.start.y).size());
  buf.selection.end.y = std::clamp(el, 0, line_count - 1);
  buf.selection.end.x = std::clamp(ec, 0, (int)buf.line(buf.selection.end.y).size());
  buf.selection.active = true;
  editor->needs_redraw = true;
}

void LuaAPI::buffer_clear_selection_from_lua(lua_State *L)
{
  if (!editor)
    return;
  const int idx = resolve_buffer_arg(L, 1);
  if (idx < 0)
    return;
  editor->buffers[(size_t)idx].selection.active = false;
  editor->needs_redraw = true;
}
