#include "editor.h"
#include "host_api.h"
#include "lua_bridge/api.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace fs = std::filesystem;
static LuaAPI *active_api = nullptr;

namespace {
std::string lower(std::string s) { for (char &c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string key_name(const std::string &s) {
  std::string out;
  for (char c : s) if (!std::isspace((unsigned char)c)) out += c;
  return out;
}
LuaAPI &api(lua_State *L) { return *static_cast<LuaAPI *>(lua_touserdata(L, lua_upvalueindex(1))); }
int l_show_message(lua_State *L) { api(L).show_message(luaL_optstring(L, 1, "")); return 0; }
int l_register_command(lua_State *L) { auto &a=api(L); const char *name=luaL_optstring(L,1,""); const char *detail=luaL_optstring(L,3,"Runtime command"); luaL_checktype(L,2,LUA_TFUNCTION); lua_pushvalue(L,2); int ref=luaL_ref(L,LUA_REGISTRYINDEX); std::string id="lua."+std::to_string(ref); a.lua_callbacks[id]=ref; a.register_command(name,id,detail); return 0; }
int l_register_keymap(lua_State *L) { auto &a=api(L); const char *key=luaL_optstring(L,1,""); std::string cb; std::string cmd; if(lua_isfunction(L,2)){lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);cb="lua."+std::to_string(ref);a.lua_callbacks[cb]=ref;}else cmd=luaL_optstring(L,2,""); a.register_keymap(key,cb,cmd,luaL_optstring(L,3,""),luaL_optstring(L,4,"global")); return 0; }
int l_register_autocmd(lua_State *L) { auto &a=api(L); luaL_checktype(L,2,LUA_TFUNCTION); lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);std::string id="lua."+std::to_string(ref);a.lua_callbacks[id]=ref;a.register_autocmd(luaL_optstring(L,1,""),id);return 0; }
int l_register_panel(lua_State *L) { auto &a=api(L); luaL_checktype(L,2,LUA_TFUNCTION);lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);std::string id="lua."+std::to_string(ref);a.lua_callbacks[id]=ref;a.register_panel(luaL_optstring(L,1,""),id,luaL_optstring(L,3,""));return 0; }
int l_get_buffer(lua_State *L){lua_pushstring(L,api(L).get_current_buffer().c_str());return 1;}
int l_set_buffer(lua_State *L){api(L).set_current_buffer(luaL_optstring(L,1,""));return 0;}
int l_get_selection(lua_State *L){lua_pushstring(L,api(L).get_selection().c_str());return 1;}
int l_replace(lua_State *L){api(L).replace_selection(luaL_optstring(L,1,""));return 0;}
int l_insert(lua_State *L){api(L).insert_text(luaL_optstring(L,1,""));return 0;}
int l_cursor(lua_State *L){auto p=api(L).get_cursor();lua_pushinteger(L,p.first+1);lua_pushinteger(L,p.second+1);return 2;}
int l_set_cursor(lua_State *L){api(L).set_cursor((int)luaL_checkinteger(L,1)-1,(int)luaL_checkinteger(L,2)-1);return 0;}
int l_current_file(lua_State *L){lua_pushstring(L,api(L).current_file().c_str());return 1;}
int l_open(lua_State *L){api(L).open_file(luaL_checkstring(L,1));return 0;}
int l_save(lua_State *L){api(L).save_current_file();return 0;}
int l_execute(lua_State *L){api(L).execute_command(luaL_checkstring(L,1));return 0;}
int l_job(lua_State *L){api(L).run_job(luaL_checkstring(L,1),luaL_optstring(L,2,""),luaL_optstring(L,3,""));return 0;}
int l_close_buffer(lua_State *L){lua_pushboolean(L,api(L).host().core.close_buffer((int)luaL_checkinteger(L,1)-1));return 1;}
int l_new_buffer(lua_State *L){api(L).host().core.new_buffer();return 0;}
int l_save_buffer(lua_State *L){lua_pushboolean(L,api(L).host().io.save_buffer((int)luaL_checkinteger(L,1)-1));return 1;}
int l_open_workspace(lua_State *L){api(L).host().io.open_workspace(luaL_checkstring(L,1));return 0;}
int l_toggle_sidebar(lua_State *L){api(L).host().io.toggle_sidebar();return 0;}
int l_toggle_terminal(lua_State *L){api(L).host().io.toggle_terminal();return 0;}
int l_editor_execute(lua_State *L){api(L).execute_command(luaL_checkstring(L,1));return 0;}
int l_editor_redraw(lua_State *L){api(L).host().render.request_redraw();return 0;}
int l_capabilities(lua_State *L){
  lua_newtable(L);
  const char *names[] = {"buffer","cursor","selection","pane","file","ui","keymap","job","edit","search","folds","bookmarks","workspace","terminal","tasks","theme","config","lsp","debugger","git","treesitter","image"};
  for (const char *name : names) { lua_pushboolean(L, 1); lua_setfield(L, -2, name); }
  return 1;
}
int l_theme_list(lua_State *L){lua_newtable(L);int n=1;for(const auto&name:api(L).list_themes()){lua_pushstring(L,name.c_str());lua_rawseti(L,-2,n++);}return 1;}
int l_theme_apply(lua_State *L){lua_pushboolean(L,api(L).apply_colorscheme(luaL_checkstring(L,1)));return 1;}
int l_command(lua_State *L){
  api(L).execute_command(luaL_checkstring(L,1));
  return 0;
}
int l_picker(lua_State *L){auto &a=api(L);luaL_checktype(L,3,LUA_TFUNCTION);lua_pushvalue(L,3);int r=luaL_ref(L,LUA_REGISTRYINDEX);std::string select="lua."+std::to_string(r);a.lua_callbacks[select]=r;std::string items;if(lua_isfunction(L,2)){lua_pushvalue(L,2);r=luaL_ref(L,LUA_REGISTRYINDEX);items="lua."+std::to_string(r);a.lua_callbacks[items]=r;}else {luaL_checktype(L,2,LUA_TTABLE);lua_pushvalue(L,2);r=luaL_ref(L,LUA_REGISTRYINDEX);items="lua."+std::to_string(r);}a.show_picker(luaL_optstring(L,1,"Runtime Picker"),items,select);return 0;}
int l_panel_show(lua_State *L){api(L).show_panel(luaL_optstring(L,1,""));return 0;}
int l_buffer_list(lua_State *L) { auto &a=api(L); lua_newtable(L); int n=1; for (const auto &b : a.host().core.list_buffers()) { lua_newtable(L); lua_pushinteger(L,b.index+1); lua_setfield(L,-2,"index"); lua_pushstring(L,b.filepath.c_str()); lua_setfield(L,-2,"path"); lua_pushboolean(L,b.modified); lua_setfield(L,-2,"modified"); lua_pushboolean(L,b.active); lua_setfield(L,-2,"active"); lua_pushboolean(L,b.preview); lua_setfield(L,-2,"preview"); lua_rawseti(L,-2,n++); } return 1; }
int l_layout(lua_State *L) { auto x=api(L).host().render.layout(); lua_newtable(L); lua_pushinteger(L,x.width);lua_setfield(L,-2,"width");lua_pushinteger(L,x.height);lua_setfield(L,-2,"height");lua_pushboolean(L,x.sidebar_visible);lua_setfield(L,-2,"sidebar_visible");lua_pushinteger(L,x.sidebar_width);lua_setfield(L,-2,"sidebar_width");lua_pushboolean(L,x.minimap_visible);lua_setfield(L,-2,"minimap_visible");lua_pushboolean(L,x.terminal_visible);lua_setfield(L,-2,"terminal_visible");lua_pushinteger(L,x.terminal_height);lua_setfield(L,-2,"terminal_height"); return 1; }
int l_panes(lua_State *L) { auto &a=api(L);lua_newtable(L);int n=1;for(const auto&p:a.host().render.list_panes()){lua_newtable(L);lua_pushinteger(L,p.index+1);lua_setfield(L,-2,"index");lua_pushinteger(L,p.buffer_id+1);lua_setfield(L,-2,"buffer");lua_pushinteger(L,p.x);lua_setfield(L,-2,"x");lua_pushinteger(L,p.y);lua_setfield(L,-2,"y");lua_pushinteger(L,p.w);lua_setfield(L,-2,"width");lua_pushinteger(L,p.h);lua_setfield(L,-2,"height");lua_pushboolean(L,p.focused);lua_setfield(L,-2,"focused");lua_rawseti(L,-2,n++);}return 1;}
int l_switch_buffer(lua_State *L){return api(L).host().core.switch_buffer((int)luaL_checkinteger(L,1)-1) ? (lua_pushboolean(L,1),1) : (lua_pushboolean(L,0),1);}
int l_split_h(lua_State *L){api(L).host().render.split_horizontal();return 0;} int l_split_v(lua_State *L){api(L).host().render.split_vertical();return 0;}
int l_focus_next(lua_State *L){api(L).host().render.focus_next_pane();return 0;} int l_focus_prev(lua_State *L){api(L).host().render.focus_prev_pane();return 0;}
int l_resize(lua_State *L){lua_pushboolean(L,api(L).host().render.resize_focused_pane((int)luaL_checkinteger(L,1)));return 1;} int l_redraw(lua_State *L){api(L).host().render.request_redraw();return 0;}
void inject(lua_State *L, LuaAPI *a, const char *name, lua_CFunction fn){lua_pushlightuserdata(L,a);lua_pushcclosure(L,fn,1);lua_setglobal(L,name);}
void field(lua_State *L, const char *name, lua_CFunction fn) { lua_pushcfunction(L,fn); lua_setfield(L,-2,name); }
void command_field(lua_State *L, LuaAPI *a, const char *name, const char *command) {
  lua_pushlightuserdata(L, a);
  lua_pushstring(L, command);
  lua_pushcclosure(L, [](lua_State *s) { api(s).execute_command(lua_tostring(s, lua_upvalueindex(2))); return 0; }, 2);
  lua_setfield(L, -2, name);
}
}

LuaAPI::LuaAPI(Editor *ed) : editor(ed), lua_state(nullptr), lua_initialized(false) { active_api=this; }
EditorHostAPI &LuaAPI::host() { return *editor->host_api; }
LuaAPI::~LuaAPI(){ cleanup(); if(active_api==this) active_api=nullptr; }
 bool LuaAPI::init(){
 if(lua_initialized)return true; lua_State *L=luaL_newstate(); if(!L)return false; luaL_openlibs(L);lua_state=L;lua_initialized=true;
  inject(L,this,"show_message",l_show_message); inject(L,this,"command",l_register_command); inject(L,this,"autocmd",l_register_autocmd); inject(L,this,"set_hl",[](lua_State *s){auto&a=api(s);luaL_checktype(s,2,LUA_TTABLE);lua_getfield(s,2,"fg");int fg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);lua_getfield(s,2,"bg");int bg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);a.set_theme_color(luaL_optstring(s,1,""),fg,bg);return 0;});
 inject(L,this,"get_current_buffer",l_get_buffer);inject(L,this,"set_current_buffer",l_set_buffer);inject(L,this,"get_selection",l_get_selection);inject(L,this,"replace_selection",l_replace);inject(L,this,"insert_text",l_insert);inject(L,this,"cursor",l_cursor);inject(L,this,"set_cursor",l_set_cursor);inject(L,this,"current_file",l_current_file);inject(L,this,"open_file",l_open);inject(L,this,"save",l_save);inject(L,this,"execute",l_execute);inject(L,this,"run_job",l_job);inject(L,this,"show_picker",l_picker);inject(L,this,"show_panel",l_panel_show);inject(L,this,"register_keymap",l_register_keymap);inject(L,this,"register_panel",l_register_panel);
  lua_newtable(L);lua_pushstring(L,"3.0.0");lua_setfield(L,-2,"api_version");lua_pushstring(L,"Lua coordinates are 1-based line/column");lua_setfield(L,-2,"coordinate_convention");
  lua_newtable(L);field(L,"get_text",l_get_buffer);field(L,"set_text",l_set_buffer);field(L,"get_selection",l_get_selection);field(L,"replace_selection",l_replace);field(L,"insert",l_insert);field(L,"cursor",l_cursor);field(L,"set_cursor",l_set_cursor);field(L,"list",l_buffer_list);field(L,"switch",l_switch_buffer);lua_setfield(L,-2,"buffer");
  lua_newtable(L);field(L,"get",l_cursor);field(L,"set",l_set_cursor);lua_setfield(L,-2,"cursor");
  lua_newtable(L);field(L,"layout",l_layout);field(L,"panes",l_panes);field(L,"split_horizontal",l_split_h);field(L,"split_vertical",l_split_v);field(L,"focus_next",l_focus_next);field(L,"focus_previous",l_focus_prev);field(L,"resize",l_resize);field(L,"redraw",l_redraw);lua_setfield(L,-2,"pane");
  lua_newtable(L);field(L,"open",l_open);field(L,"save",l_save);field(L,"execute",l_execute);field(L,"run",l_job);lua_setfield(L,-2,"file");
  lua_newtable(L);field(L,"show_message",l_show_message);field(L,"picker",l_picker);field(L,"panel",l_panel_show);lua_setfield(L,-2,"ui");
  lua_newtable(L);field(L,"register",l_register_keymap);lua_setfield(L,-2,"keymap");lua_newtable(L);field(L,"run",l_job);lua_setfield(L,-2,"job");lua_newtable(L);field(L,"set",[](lua_State*s){auto&a=api(s);luaL_checktype(s,2,LUA_TTABLE);lua_getfield(s,2,"fg");int fg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);lua_getfield(s,2,"bg");int bg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);a.set_theme_color(luaL_optstring(s,1,""),fg,bg);return 0;});lua_setfield(L,-2,"theme");
  lua_getglobal(L,"show_message");lua_setfield(L,-2,"notify");lua_getglobal(L,"command");lua_setfield(L,-2,"command");lua_getglobal(L,"autocmd");lua_setfield(L,-2,"autocmd");lua_getglobal(L,"register_keymap");lua_setfield(L,-2,"register_keymap");lua_getglobal(L,"register_panel");lua_setfield(L,-2,"register_panel");lua_getglobal(L,"show_picker");lua_setfield(L,-2,"show_picker");lua_getglobal(L,"show_panel");lua_setfield(L,-2,"show_panel");lua_pushvalue(L,-1);lua_setglobal(L,"vim");lua_setglobal(L,"jot");
  if (luaL_dostring(L,
      "jot.notify=show_message; jot.command=command; jot.autocmd=autocmd; "
      "jot.execute=execute; jot.open_file=open; jot.save=save; "
      "jot.buffer={get_text=get_current_buffer,set_text=set_current_buffer,"
      "get_selection=get_selection,replace_selection=replace_selection,"
      "insert_text=insert_text,cursor=cursor,set_cursor=set_cursor,"
      "current_file=current_file}; "
      "jot.ui={show_picker=show_picker,register_panel=register_panel,"
      "show_panel=show_panel}; "
      "jot.keymap={set=register_keymap}; jot.job={run=run_job}; "
      "jot.api={nvim_set_hl=set_hl}; vim=jot")) {
    std::cerr << "Lua API setup failed: " << lua_tostring(L, -1) << "\n";
    lua_pop(L, 1);
  }
  // Complete stable runtime surface after compatibility aliases are installed.
  lua_getglobal(L, "jot");
  lua_pushcfunction(L, l_capabilities); lua_setfield(L, -2, "capabilities");
  lua_newtable(L);
  field(L, "execute", l_editor_execute); field(L, "request_redraw", l_editor_redraw);
  command_field(L, this, "undo", ":undo"); command_field(L, this, "redo", ":redo");
  command_field(L, this, "insert_newline", ":newline"); command_field(L, this, "delete", ":delete");
  command_field(L, this, "indent", ":indent"); command_field(L, this, "outdent", ":outdent");
  command_field(L, this, "comment", ":comment"); command_field(L, this, "duplicate", ":duplicate");
  command_field(L, this, "move_up", ":moveup"); command_field(L, this, "move_down", ":movedown");
  command_field(L, this, "join", ":join"); command_field(L, this, "uppercase", ":upper");
  command_field(L, this, "lowercase", ":lower"); command_field(L, this, "replace", ":replace");
  command_field(L, this, "surround", ":surround"); command_field(L, this, "increment", ":incnum");
  command_field(L, this, "select_all", ":selectall"); command_field(L, this, "select_line", ":selectline");
  command_field(L, this, "search", "Toggle Search"); command_field(L, this, "format", "Format Document");
  lua_setfield(L, -2, "edit");
  lua_newtable(L); field(L, "get", l_cursor); field(L, "set", l_set_cursor);
  command_field(L, this, "select_all", ":selectall"); command_field(L, this, "select_line", ":selectline");
  lua_setfield(L, -2, "cursor");
  lua_newtable(L); field(L, "current_file", l_current_file); field(L, "open", l_open); field(L, "save", l_save); field(L, "execute", l_editor_execute);
  field(L, "save_buffer", l_save_buffer); field(L, "close", l_close_buffer);
  field(L, "new", l_new_buffer); field(L, "open_workspace", l_open_workspace);
  lua_setfield(L, -2, "file");
  lua_newtable(L); field(L, "toggle_sidebar", l_toggle_sidebar); field(L, "toggle_terminal", l_toggle_terminal);
  field(L, "request_redraw", l_editor_redraw); lua_setfield(L, -2, "ui");
  lua_getglobal(L, "jot"); lua_getfield(L, -1, "theme");
  field(L, "list", l_theme_list); field(L, "apply", l_theme_apply);
  lua_pop(L, 2);
  lua_newtable(L); field(L, "layout", l_layout); field(L, "list", l_panes);
  field(L, "split_horizontal", l_split_h); field(L, "split_vertical", l_split_v);
  field(L, "focus_next", l_focus_next); field(L, "focus_previous", l_focus_prev);
  field(L, "resize", l_resize); command_field(L, this, "close", "Close Pane");
  lua_setfield(L, -2, "pane");
  lua_getglobal(L, "jot"); lua_getfield(L, -1, "ui");
  field(L, "show_message", l_show_message); field(L, "picker", l_picker); field(L, "panel", l_panel_show);
  lua_pop(L, 2);
  lua_newtable(L); field(L, "execute", l_editor_execute); field(L, "request_redraw", l_editor_redraw);
  lua_setfield(L, -2, "editor");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "next", ":next"); command_field(L, this, "previous", ":prev"); lua_setfield(L, -2, "search");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "toggle", ":togglefold"); command_field(L, this, "fold", ":fold"); command_field(L, this, "unfold", ":unfold"); command_field(L, this, "all", ":foldall"); lua_setfield(L, -2, "folds");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "toggle", "Toggle Bookmark"); command_field(L, this, "next", "Next Bookmark"); command_field(L, this, "previous", "Previous Bookmark"); lua_setfield(L, -2, "bookmarks");
  lua_newtable(L); field(L, "execute", l_command); field(L, "open", l_open_workspace); lua_setfield(L, -2, "workspace");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "toggle", "Toggle Terminal"); command_field(L, this, "new", "New Terminal"); lua_setfield(L, -2, "terminal");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "list", "Tasks"); command_field(L, this, "run", "Run Task"); lua_setfield(L, -2, "tasks");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "definition", "LSP Definition"); command_field(L, this, "back", "LSP Back"); command_field(L, this, "completion", "LSP Completion"); lua_setfield(L, -2, "lsp");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "continue", "Debug Continue"); command_field(L, this, "pause", "Debug Pause"); command_field(L, this, "step_in", "Debug Step In"); command_field(L, this, "step_over", "Debug Step Over"); command_field(L, this, "step_out", "Debug Step Out"); command_field(L, this, "stop", "Debug Stop"); lua_setfield(L, -2, "debugger");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "refresh", "Git Refresh"); command_field(L, this, "stage_all", "Git Stage All"); lua_setfield(L, -2, "git");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "status", "Tree-sitter Status"); command_field(L, this, "reload", "Reload Tree-sitter"); lua_setfield(L, -2, "treesitter");
  lua_newtable(L); field(L, "execute", l_command); command_field(L, this, "open", "Open Image"); lua_setfield(L, -2, "image");
  lua_newtable(L); field(L, "execute", l_command); lua_setfield(L, -2, "config");
  lua_setglobal(L, "jot");
  load_plugins();
  return true;
}
void LuaAPI::cleanup(){if(!lua_state)return;lua_close(static_cast<lua_State*>(lua_state));lua_state=nullptr;lua_callbacks.clear();lua_initialized=false;}
void LuaAPI::clear_runtime_state(){plugin_commands.clear();plugin_keymaps.clear();plugin_autocmds.clear();plugin_panels.clear();plugin_load_status.clear();if(lua_state){for(auto&x:lua_callbacks)luaL_unref(static_cast<lua_State*>(lua_state),LUA_REGISTRYINDEX,x.second);}lua_callbacks.clear();}
bool LuaAPI::load_script_path(const std::string &module,const std::string &path){lua_State*L=static_cast<lua_State*>(lua_state);int top=lua_gettop(L);if(luaL_loadfile(L,path.c_str())||lua_pcall(L,0,0,0)){std::string e=lua_tostring(L,-1);lua_pop(L,1);plugin_load_status.push_back({module,path,false,e});if(editor)editor->set_message("Plugin failed: "+module);lua_settop(L,top);return false;}plugin_load_status.push_back({module,path,true,""});return true;}
void LuaAPI::load_plugins(){if(!lua_initialized)return;fs::path root;const char*env=getenv("JOT_CONFIG_HOME");if(env&&*env)root=env;else {const char*home=getenv("HOME");const char*app=getenv("APPDATA");if(app&&*app)root=fs::path(app)/"jot";else if(home)root=fs::path(home)/".config"/"jot";}if(root.empty())return;fs::create_directories(root/"plugins");clear_runtime_state();fs::path init=root/"init.lua";if(fs::exists(init))load_script_path("jot_init",init.string());std::vector<fs::path> files;for(auto&e:fs::directory_iterator(root/"plugins")){if(e.is_regular_file()&&e.path().extension()==".lua")files.push_back(e.path());else if(e.is_directory()&&fs::exists(e.path()/"plugin.lua"))files.push_back(e.path()/"plugin.lua");}std::sort(files.begin(),files.end());for(auto&p:files)load_script_path("jot_plugin_"+p.stem().string(),p.string());fire_autocmd("EditorEnter");}
void LuaAPI::reload_plugins(){load_plugins();fire_autocmd("PluginReload");if(editor)editor->set_message("Reloaded "+std::to_string(plugin_load_status.size())+" plugin file(s)");}
bool LuaAPI::call_callback_string(const std::string &id,const std::string &arg){if(!lua_initialized||!editor||!editor->event_loop_.is_main_thread())return false;auto it=lua_callbacks.find(id);if(it==lua_callbacks.end())return false;lua_State*L=static_cast<lua_State*>(lua_state);int top=lua_gettop(L);lua_rawgeti(L,LUA_REGISTRYINDEX,it->second);lua_pushstring(L,arg.c_str());bool ok=lua_pcall(L,1,0,0)==LUA_OK;if(!ok){std::cerr<<"Lua callback error: "<<lua_tostring(L,-1)<<"\n";}lua_settop(L,top);return ok;}
 bool LuaAPI::call_callback_event(const std::string &id,const std::string &event,const std::string &filepath,int buffer){if(!lua_initialized||!editor||!editor->event_loop_.is_main_thread())return false;auto it=lua_callbacks.find(id);if(it==lua_callbacks.end())return false;lua_State*L=static_cast<lua_State*>(lua_state);int top=lua_gettop(L);lua_rawgeti(L,LUA_REGISTRYINDEX,it->second);lua_newtable(L);lua_pushstring(L,event.c_str());lua_setfield(L,-2,"event");lua_pushstring(L,filepath.c_str());lua_setfield(L,-2,"path");if(buffer<0)lua_pushnil(L);else lua_pushinteger(L,buffer+1);lua_setfield(L,-2,"buffer");if(buffer>=0&&buffer<(int)editor->buffers.size()){const Cursor pos=editor->buffers[(size_t)buffer].cursor;lua_pushinteger(L,pos.y+1);lua_setfield(L,-2,"line");lua_pushinteger(L,pos.x+1);lua_setfield(L,-2,"column");}if(lua_pcall(L,1,0,0)!=LUA_OK){std::cerr<<"Lua event callback error: "<<lua_tostring(L,-1)<<"\n";lua_settop(L,top);return false;}lua_settop(L,top);return true;}
void LuaAPI::on_buffer_open(const std::string&f){if(editor)editor->notify_lsp_open(f);fire_autocmd("BufOpen",f,-1);}void LuaAPI::on_buffer_change(const std::string&f,const std::string&){if(editor)editor->notify_lsp_change(f);fire_autocmd("BufChange",f,-1);}void LuaAPI::on_buffer_save(const std::string&f){if(editor)editor->notify_lsp_save(f);fire_autocmd("BufSave",f,-1);}
void LuaAPI::register_command(const std::string&n,const std::string&c,const std::string&d){if(n.empty()||c.empty())return;for(auto&x:plugin_commands)if(x.name==lower(n)){x.callback=c;x.detail=d;return;}plugin_commands.push_back({lower(n),c,d});}
bool LuaAPI::run_plugin_command(const std::string&n,const std::string&a){for(auto&x:plugin_commands)if(x.name==n){call_callback_string(x.callback,a);return true;}return false;}
bool LuaAPI::run_plugin_keymap(const std::string&k,const std::string&m){for(auto&x:plugin_keymaps)if(x.key==key_name(k)&&(x.mode=="global"||x.mode==m)){if(!x.command.empty()&&editor){if(!x.command.empty()&&x.command[0]==':')editor->execute_ex_command(x.command);else if(editor->host_api)editor->host_api->io.execute_command(x.command);}else call_callback_string(x.callback,"");return true;}return false;}
void LuaAPI::fire_autocmd(const std::string&e,const std::string&f,int b){for(auto&x:plugin_autocmds)if(x.event==e)call_callback_event(x.callback,e,f,b);}
void LuaAPI::register_keymap(const std::string&k,const std::string&c,const std::string&cmd,const std::string&d,const std::string&m){plugin_keymaps.push_back({key_name(k),c,cmd,d,m});}
void LuaAPI::register_autocmd(const std::string&e,const std::string&c){plugin_autocmds.push_back({e,c});}
void LuaAPI::register_panel(const std::string&n,const std::string&c,const std::string&t){plugin_panels.push_back({n,c,t});}
bool LuaAPI::run_plugin_callback(const std::string&c,const std::string&a){return call_callback_string(c,a);}
std::string LuaAPI::get_current_buffer(){return editor&&editor->host_api?editor->host_api->core.buffer_content():"";}
void LuaAPI::set_current_buffer(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.set_buffer_content(s);}
std::string LuaAPI::get_selection(){return editor&&editor->host_api?editor->host_api->core.selected_text():"";}
void LuaAPI::replace_selection(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.replace_selection(s);}
void LuaAPI::insert_text(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.insert_text(s);}
std::pair<int,int> LuaAPI::get_cursor(){return editor&&editor->host_api?editor->host_api->core.cursor():std::pair<int,int>{0,0};}
void LuaAPI::set_cursor(int l,int c){if(editor&&editor->host_api)editor->host_api->core.set_cursor(l,c);}
std::string LuaAPI::current_file(){return editor&&editor->host_api?editor->host_api->core.current_file():"";}
void LuaAPI::open_file(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.open_file(s);}
void LuaAPI::save_current_file(){if(editor&&editor->host_api)editor->host_api->io.save_current_file();}
void LuaAPI::execute_command(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.execute_command(s);}
void LuaAPI::run_job(const std::string&a,const std::string&b,const std::string&c){if(editor&&editor->host_api)editor->host_api->io.run_job(a,b,c);}
void LuaAPI::show_picker(const std::string&a,const std::string&b,const std::string&c){if(editor&&editor->host_api)editor->host_api->io.show_plugin_picker(a,b,c);}
void LuaAPI::show_panel(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.show_plugin_panel(s);}
std::vector<std::string> LuaAPI::plugin_panel_lines(const std::string&name){for(auto&p:plugin_panels)if(p.name==name){auto i=lua_callbacks.find(p.callback);if(i==lua_callbacks.end())return{};lua_State*L=static_cast<lua_State*>(lua_state);lua_rawgeti(L,LUA_REGISTRYINDEX,i->second);lua_pushstring(L,name.c_str());if(lua_pcall(L,1,1,0)) {lua_pop(L,1);return{};}std::vector<std::string> out;if(lua_istable(L,-1)){lua_pushnil(L);while(lua_next(L,-2)){out.push_back(lua_tostring(L,-1)?lua_tostring(L,-1):"");lua_pop(L,1);}}lua_pop(L,1);return out;}return{};}
std::vector<std::string> LuaAPI::plugin_picker_items(const std::string&callback){auto i=lua_callbacks.find(callback);if(i==lua_callbacks.end())return{};lua_State*L=static_cast<lua_State*>(lua_state);lua_rawgeti(L,LUA_REGISTRYINDEX,i->second);lua_pushstring(L,"");if(lua_pcall(L,1,1,0)){lua_pop(L,1);return{};}std::vector<std::string>out;if(lua_istable(L,-1)){lua_pushnil(L);while(lua_next(L,-2)){out.push_back(lua_tostring(L,-1)?lua_tostring(L,-1):"");lua_pop(L,1);}}lua_pop(L,1);return out;}
