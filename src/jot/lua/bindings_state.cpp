// Lua host bindings: Editor state queries (symbols, clipboard, terminals, recent files, search, picker, motions, sidebar, memory).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_symbols_list(lua_State *L)
  {
    api(L).push_symbols(L);
    return 1;
  }
  int l_clip_copy(lua_State *L)
  {
    api(L).clipboard_copy_from_lua(L);
    return 0;
  }
  int l_clip_cut(lua_State *L)
  {
    api(L).clipboard_cut_from_lua(L);
    return 0;
  }
  int l_clip_paste(lua_State *L)
  {
    api(L).clipboard_paste_from_lua(L);
    return 0;
  }
  int l_clip_get(lua_State *L)
  {
    api(L).push_clipboard_text(L);
    return 1;
  }
  int l_clip_set(lua_State *L)
  {
    api(L).clipboard_set_from_lua(L);
    return 0;
  }
  int l_terminal_list(lua_State *L)
  {
    api(L).push_terminal_list(L);
    return 1;
  }
  int l_terminal_write(lua_State *L)
  {
    api(L).terminal_write_from_lua(L);
    return 1;
  }
  int l_terminal_close(lua_State *L)
  {
    api(L).terminal_close_from_lua(L);
    return 1;
  }
  int l_terminal_activate(lua_State *L)
  {
    api(L).terminal_activate_from_lua(L);
    return 1;
  }
  int l_terminal_spawn(lua_State *L)
  {
    api(L).terminal_spawn_from_lua(L);
    return 1;
  }
  int l_workspace_path(lua_State *L)
  {
    api(L).push_workspace_path(L);
    return 1;
  }
  int l_recent_files(lua_State *L)
  {
    api(L).push_recent_files(L);
    return 1;
  }
  int l_recent_workspaces(lua_State *L)
  {
    api(L).push_recent_workspaces(L);
    return 1;
  }
  int l_search_info(lua_State *L)
  {
    api(L).push_search_info(L);
    return 1;
  }
  int l_search_matches(lua_State *L)
  {
    api(L).push_search_matches(L);
    return 1;
  }
  int l_picker_active(lua_State *L)
  {
    api(L).push_picker_active(L);
    return 1;
  }
  int l_picker_info(lua_State *L)
  {
    api(L).push_picker_info(L);
    return 1;
  }
  int l_picker_items(lua_State *L)
  {
    api(L).push_picker_items(L);
    return 1;
  }
  int l_picker_accept(lua_State *L)
  {
    api(L).picker_accept_from_lua(L);
    return 1;
  }
  int l_picker_close(lua_State *L)
  {
    api(L).picker_close_from_lua(L);
    return 1;
  }
  int l_process_memory(lua_State *L)
  {
    const long long bytes = api(L).process_memory_bytes();
    if (bytes < 0)
    {
      lua_pushnil(L);
    }
    else
    {
      lua_pushinteger(L, bytes);
    }
    return 1;
  }
  int l_motion_word_next(lua_State *L)
  {
    api(L).motion_from_lua(L, 0);
    return 0;
  }
  int l_motion_word_prev(lua_State *L)
  {
    api(L).motion_from_lua(L, 1);
    return 0;
  }
  int l_motion_line_start(lua_State *L)
  {
    api(L).motion_from_lua(L, 2);
    return 0;
  }
  int l_motion_line_end(lua_State *L)
  {
    api(L).motion_from_lua(L, 3);
    return 0;
  }
  int l_motion_file_start(lua_State *L)
  {
    api(L).motion_from_lua(L, 4);
    return 0;
  }
  int l_motion_file_end(lua_State *L)
  {
    api(L).motion_from_lua(L, 5);
    return 0;
  }
  int l_motion_matching_bracket(lua_State *L)
  {
    api(L).motion_from_lua(L, 6);
    return 0;
  }
  int l_motion_select_function(lua_State *L)
  {
    api(L).motion_from_lua(L, 7);
    return 0;
  }
  int l_sidebar_info(lua_State *L)
  {
    api(L).push_sidebar_info(L);
    return 1;
  }
  int l_sidebar_set_view(lua_State *L)
  {
    api(L).sidebar_set_view_from_lua(L);
    return 0;
  }
} // namespace lua_bind
