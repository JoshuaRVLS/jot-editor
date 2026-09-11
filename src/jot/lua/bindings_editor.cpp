// Lua host bindings: Editor actions and queries exposed to Lua (messages, cursor, buffer switching, file open/save, job launch, pickers, panels, capabilities).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  int l_show_message(lua_State *L)
  {
    api(L).show_message(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_show_transient_message(lua_State *L)
  {
    api(L).show_transient_message(luaL_optstring(L, 1, ""),
                                  (int)luaL_optinteger(L, 2, 5000));
    return 0;
  }
  int l_file_list(lua_State *L)
  {
    api(L).push_file_list(L);
    return 1;
  }
  int l_file_read(lua_State *L)
  {
    api(L).push_file_read(L);
    return lua_isnil(L, -1) ? 2 : 1;
  }
  int l_editor_default_tab(lua_State *L)
  {
    api(L).default_tab_from_lua(L);
    return 0;
  }
  int l_editor_default_shift_tab(lua_State *L)
  {
    api(L).default_shift_tab_from_lua(L);
    return 0;
  }
  int l_get_buffer(lua_State *L)
  {
    lua_pushstring(L, api(L).get_current_buffer().c_str());
    return 1;
  }
  int l_set_buffer(lua_State *L)
  {
    api(L).set_current_buffer(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_get_selection(lua_State *L)
  {
    lua_pushstring(L, api(L).get_selection().c_str());
    return 1;
  }
  int l_replace(lua_State *L)
  {
    api(L).replace_selection(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_insert(lua_State *L)
  {
    api(L).insert_text(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_cursor(lua_State *L)
  {
    auto p = api(L).get_cursor();
    lua_pushinteger(L, p.first + 1);
    lua_pushinteger(L, p.second + 1);
    return 2;
  }
  int l_set_cursor(lua_State *L)
  {
    api(L).set_cursor((int)luaL_checkinteger(L, 1) - 1, (int)luaL_checkinteger(L, 2) - 1);
    return 0;
  }
  int l_current_file(lua_State *L)
  {
    lua_pushstring(L, api(L).current_file().c_str());
    return 1;
  }
  int l_open(lua_State *L)
  {
    api(L).open_file(luaL_checkstring(L, 1));
    return 0;
  }
  int l_save(lua_State *L)
  {
    api(L).save_current_file();
    return 0;
  }
  int l_execute(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_job(lua_State *L)
  {
    api(L).run_job(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), luaL_optstring(L, 3, ""));
    return 0;
  }
  int l_picker(lua_State *L)
  {
    auto &a = api(L);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    int r = luaL_ref(L, LUA_REGISTRYINDEX);
    std::string select = "lua." + std::to_string(r);
    a.lua_callbacks[select] = r;
    std::string items;
    if (lua_isfunction(L, 2))
    {
      lua_pushvalue(L, 2);
      r = luaL_ref(L, LUA_REGISTRYINDEX);
      items = "lua." + std::to_string(r);
      a.lua_callbacks[items] = r;
    }
    else
    {
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_pushvalue(L, 2);
      r = luaL_ref(L, LUA_REGISTRYINDEX);
      items = "lua." + std::to_string(r);
    }
    a.show_picker(luaL_optstring(L, 1, "Runtime Picker"), items, select);
    return 0;
  }
  int l_panel_show(lua_State *L)
  {
    api(L).show_panel(luaL_optstring(L, 1, ""));
    return 0;
  }
  int l_close_buffer(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().core.close_buffer((int)luaL_checkinteger(L, 1) - 1));
    return 1;
  }
  int l_new_buffer(lua_State *L)
  {
    api(L).host().core.new_buffer();
    return 0;
  }
  int l_save_buffer(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().io.save_buffer((int)luaL_checkinteger(L, 1) - 1));
    return 1;
  }
  int l_open_workspace(lua_State *L)
  {
    api(L).host().io.open_workspace(luaL_checkstring(L, 1));
    return 0;
  }
  int l_toggle_sidebar(lua_State *L)
  {
    api(L).host().io.toggle_sidebar();
    return 0;
  }
  int l_toggle_zen(lua_State *L)
  {
    lua_pushboolean(L, api(L).host().io.toggle_zen());
    return 1;
  }
  int l_toggle_terminal(lua_State *L)
  {
    api(L).host().io.toggle_terminal();
    return 0;
  }
  int l_editor_execute(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_editor_redraw(lua_State *L)
  {
    api(L).host().render.request_redraw();
    return 0;
  }
  int l_editor_restart(lua_State *L)
  {
    const bool force = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, api(L).restart_editor(force) ? 1 : 0);
    return 1;
  }
  int l_capabilities(lua_State *L)
  {
    lua_newtable(L);
    const char *names[] = {
        "buffer",      "cursor", "selection", "clipboard", "picker",     "events",  "viewport",
        "filetree",    "pane",   "file",      "ui",        "keymap",     "job",     "edit",
        "search",      "folds",  "bookmarks", "workspace", "terminal",   "tasks",   "theme",
        "config",      "lsp",    "debugger",  "git",       "treesitter", "symbols", "image",
        "diagnostics", "marks",  "status",    "timer",     "motion",     "sidebar",
        "toast",      "process"};
    for (const char *name : names)
    {
      lua_pushboolean(L, 1);
      lua_setfield(L, -2, name);
    }
    return 1;
  }
  int l_command(lua_State *L)
  {
    api(L).execute_command(luaL_checkstring(L, 1));
    return 0;
  }
  int l_redraw(lua_State *L)
  {
    api(L).host().render.request_redraw();
    return 0;
  }
} // namespace lua_bind
