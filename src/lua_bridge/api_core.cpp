#include "editor.h"
#include "host_api.h"
#include "lua_bridge/api.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace fs = std::filesystem;
static PythonAPI *active_api = nullptr;

namespace {
std::string lower(std::string s) { for (char &c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string key_name(const std::string &s) {
  std::string out;
  for (char c : s) if (!std::isspace((unsigned char)c)) out += c;
  return out;
}
PythonAPI &api(lua_State *L) { return *static_cast<PythonAPI *>(lua_touserdata(L, lua_upvalueindex(1))); }
int l_show_message(lua_State *L) { api(L).py_show_message(luaL_optstring(L, 1, "")); return 0; }
int l_register_command(lua_State *L) { auto &a=api(L); const char *name=luaL_optstring(L,1,""); const char *detail=luaL_optstring(L,3,"Plugin command"); luaL_checktype(L,2,LUA_TFUNCTION); lua_pushvalue(L,2); int ref=luaL_ref(L,LUA_REGISTRYINDEX); std::string id="lua."+std::to_string(ref); a.lua_callbacks[id]=ref; a.py_register_command(name,id,detail); return 0; }
int l_register_keymap(lua_State *L) { auto &a=api(L); const char *key=luaL_optstring(L,1,""); std::string cb; std::string cmd; if(lua_isfunction(L,2)){lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);cb="lua."+std::to_string(ref);a.lua_callbacks[cb]=ref;}else cmd=luaL_optstring(L,2,""); a.py_register_keymap(key,cb,cmd,luaL_optstring(L,3,""),luaL_optstring(L,4,"global")); return 0; }
int l_register_autocmd(lua_State *L) { auto &a=api(L); luaL_checktype(L,2,LUA_TFUNCTION); lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);std::string id="lua."+std::to_string(ref);a.lua_callbacks[id]=ref;a.py_register_autocmd(luaL_optstring(L,1,""),id);return 0; }
int l_register_panel(lua_State *L) { auto &a=api(L); luaL_checktype(L,2,LUA_TFUNCTION);lua_pushvalue(L,2);int ref=luaL_ref(L,LUA_REGISTRYINDEX);std::string id="lua."+std::to_string(ref);a.lua_callbacks[id]=ref;a.py_register_panel(luaL_optstring(L,1,""),id,luaL_optstring(L,3,""));return 0; }
int l_get_buffer(lua_State *L){lua_pushstring(L,api(L).py_get_current_buffer().c_str());return 1;}
int l_set_buffer(lua_State *L){api(L).py_set_current_buffer(luaL_optstring(L,1,""));return 0;}
int l_get_selection(lua_State *L){lua_pushstring(L,api(L).py_get_selection().c_str());return 1;}
int l_replace(lua_State *L){api(L).py_replace_selection(luaL_optstring(L,1,""));return 0;}
int l_insert(lua_State *L){api(L).py_insert_text(luaL_optstring(L,1,""));return 0;}
int l_cursor(lua_State *L){std::string raw=api(L).py_get_cursor();auto p=raw.find(':');lua_pushinteger(L,std::stoi(raw.substr(0,p)));lua_pushinteger(L,std::stoi(raw.substr(p+1)));return 2;}
int l_set_cursor(lua_State *L){api(L).py_set_cursor((int)luaL_optinteger(L,1,0),(int)luaL_optinteger(L,2,0));return 0;}
int l_current_file(lua_State *L){lua_pushstring(L,api(L).py_current_file().c_str());return 1;}
int l_open(lua_State *L){api(L).py_open_file(luaL_optstring(L,1,""));return 0;}
int l_save(lua_State *L){api(L).py_save_current_file();return 0;}
int l_execute(lua_State *L){api(L).py_execute_command(luaL_optstring(L,1,""));return 0;}
int l_job(lua_State *L){api(L).py_run_job(luaL_optstring(L,1,""),luaL_optstring(L,2,""),luaL_optstring(L,3,""));return 0;}
int l_picker(lua_State *L){auto &a=api(L);luaL_checktype(L,3,LUA_TFUNCTION);lua_pushvalue(L,3);int r=luaL_ref(L,LUA_REGISTRYINDEX);std::string select="lua."+std::to_string(r);a.lua_callbacks[select]=r;std::string items;if(lua_isfunction(L,2)){lua_pushvalue(L,2);r=luaL_ref(L,LUA_REGISTRYINDEX);items="lua."+std::to_string(r);a.lua_callbacks[items]=r;}else {luaL_checktype(L,2,LUA_TTABLE);lua_pushvalue(L,2);r=luaL_ref(L,LUA_REGISTRYINDEX);items="lua."+std::to_string(r);}a.py_show_picker(luaL_optstring(L,1,"Plugin Picker"),items,select);return 0;}
int l_panel_show(lua_State *L){api(L).py_show_panel(luaL_optstring(L,1,""));return 0;}
void inject(lua_State *L, PythonAPI *a, const char *name, lua_CFunction fn){lua_pushlightuserdata(L,a);lua_pushcclosure(L,fn,1);lua_setglobal(L,name);}
}

PythonAPI::PythonAPI(Editor *ed) : editor(ed), lua_state(nullptr), lua_initialized(false) { active_api=this; }
PythonAPI::~PythonAPI(){ cleanup(); if(active_api==this) active_api=nullptr; }
bool PythonAPI::init(){
 if(lua_initialized)return true; lua_State *L=luaL_newstate(); if(!L)return false; luaL_openlibs(L);lua_state=L;lua_initialized=true;
 inject(L,this,"show_message",l_show_message); inject(L,this,"command",l_register_command); inject(L,this,"autocmd",l_register_autocmd); inject(L,this,"set_hl",[](lua_State *s){auto&a=api(s);luaL_checktype(s,2,LUA_TTABLE);lua_getfield(s,2,"fg");int fg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);lua_getfield(s,2,"bg");int bg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);a.py_set_theme_color(luaL_optstring(s,1,""),fg,bg);return 0;});
 inject(L,this,"get_current_buffer",l_get_buffer);inject(L,this,"set_current_buffer",l_set_buffer);inject(L,this,"get_selection",l_get_selection);inject(L,this,"replace_selection",l_replace);inject(L,this,"insert_text",l_insert);inject(L,this,"cursor",l_cursor);inject(L,this,"set_cursor",l_set_cursor);inject(L,this,"current_file",l_current_file);inject(L,this,"open_file",l_open);inject(L,this,"save",l_save);inject(L,this,"execute",l_execute);inject(L,this,"run_job",l_job);inject(L,this,"show_picker",l_picker);inject(L,this,"show_panel",l_panel_show);inject(L,this,"register_keymap",l_register_keymap);inject(L,this,"register_panel",l_register_panel);
 lua_newtable(L);lua_pushstring(L,"2.0.0");lua_setfield(L,-2,"api_version");const char*globals[]={"show_message","command","autocmd","set_hl","get_current_buffer","set_current_buffer","get_selection","replace_selection","insert_text","cursor","set_cursor","current_file","open_file","save","execute","run_job","show_picker","show_panel","register_keymap","register_panel"};for(const char*n:globals){lua_getglobal(L,n);lua_setfield(L,-2,n);}lua_pushvalue(L,-1);lua_setglobal(L,"vim");lua_setglobal(L,"jot");
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
  load_plugins();
  return true;
}
void PythonAPI::cleanup(){if(!lua_state)return;lua_close(static_cast<lua_State*>(lua_state));lua_state=nullptr;lua_callbacks.clear();lua_initialized=false;}
void PythonAPI::clear_plugin_state(){plugin_commands.clear();plugin_keymaps.clear();plugin_autocmds.clear();plugin_panels.clear();plugin_load_status.clear();for(auto&x:lua_callbacks)luaL_unref(static_cast<lua_State*>(lua_state),LUA_REGISTRYINDEX,x.second);lua_callbacks.clear();}
bool PythonAPI::load_script_path(const std::string &module,const std::string &path){lua_State*L=static_cast<lua_State*>(lua_state);int top=lua_gettop(L);if(luaL_loadfile(L,path.c_str())||lua_pcall(L,0,0,0)){std::string e=lua_tostring(L,-1);lua_pop(L,1);plugin_load_status.push_back({module,path,false,e});if(editor)editor->set_message("Plugin failed: "+module);lua_settop(L,top);return false;}plugin_load_status.push_back({module,path,true,""});return true;}
void PythonAPI::load_plugins(){if(!lua_initialized)return;fs::path root;const char*env=getenv("JOT_CONFIG_HOME");if(env&&*env)root=env;else {const char*home=getenv("HOME");const char*app=getenv("APPDATA");if(app&&*app)root=fs::path(app)/"jot";else if(home)root=fs::path(home)/".config"/"jot";}if(root.empty())return;fs::create_directories(root/"plugins");clear_plugin_state();fs::path init=root/"init.lua";if(fs::exists(init))load_script_path("jot_init",init.string());std::vector<fs::path> files;for(auto&e:fs::directory_iterator(root/"plugins")){if(e.is_regular_file()&&e.path().extension()==".lua")files.push_back(e.path());else if(e.is_directory()&&fs::exists(e.path()/"plugin.lua"))files.push_back(e.path()/"plugin.lua");}std::sort(files.begin(),files.end());for(auto&p:files)load_script_path("jot_plugin_"+p.stem().string(),p.string());fire_autocmd("EditorEnter");}
void PythonAPI::reload_plugins(){load_plugins();fire_autocmd("PluginReload");if(editor)editor->set_message("Reloaded "+std::to_string(plugin_load_status.size())+" plugin file(s)");}
bool PythonAPI::call_callback_string(const std::string &id,const std::string &arg){auto it=lua_callbacks.find(id);if(it==lua_callbacks.end())return false;lua_State*L=static_cast<lua_State*>(lua_state);lua_rawgeti(L,LUA_REGISTRYINDEX,it->second);lua_pushstring(L,arg.c_str());if(lua_pcall(L,1,0,0)){std::cerr<<"Lua callback error: "<<lua_tostring(L,-1)<<"\n";lua_pop(L,1);return false;}return true;}
void PythonAPI::on_buffer_open(const std::string&f){if(editor)editor->notify_lsp_open(f);fire_autocmd("BufOpen",f,-1);}void PythonAPI::on_buffer_change(const std::string&f,const std::string&){if(editor)editor->notify_lsp_change(f);fire_autocmd("BufChange",f,-1);}void PythonAPI::on_buffer_save(const std::string&f){if(editor)editor->notify_lsp_save(f);fire_autocmd("BufSave",f,-1);}
void PythonAPI::py_register_command(const std::string&n,const std::string&c,const std::string&d){if(n.empty()||c.empty())return;for(auto&x:plugin_commands)if(x.name==lower(n)){x.callback=c;x.detail=d;return;}plugin_commands.push_back({lower(n),c,d});}
bool PythonAPI::run_plugin_command(const std::string&n,const std::string&a){for(auto&x:plugin_commands)if(x.name==n){call_callback_string(x.callback,a);return true;}return false;}
bool PythonAPI::run_plugin_keymap(const std::string&k,const std::string&m){for(auto&x:plugin_keymaps)if(x.key==key_name(k)&&(x.mode=="global"||x.mode==m)){if(!x.command.empty()&&editor){if(!x.command.empty()&&x.command[0]==':')editor->execute_ex_command(x.command);else if(editor->host_api)editor->host_api->io.execute_command(x.command);}else call_callback_string(x.callback,"");return true;}return false;}
void PythonAPI::fire_autocmd(const std::string&e,const std::string&f,int b){for(auto&x:plugin_autocmds)if(x.event==e)call_callback_string(x.callback,e+"\n"+f+"\n"+std::to_string(b));}
void PythonAPI::py_register_keymap(const std::string&k,const std::string&c,const std::string&cmd,const std::string&d,const std::string&m){plugin_keymaps.push_back({key_name(k),c,cmd,d,m});}
void PythonAPI::py_register_autocmd(const std::string&e,const std::string&c){plugin_autocmds.push_back({e,c});}
void PythonAPI::py_register_panel(const std::string&n,const std::string&c,const std::string&t){plugin_panels.push_back({n,c,t});}
bool PythonAPI::run_plugin_callback(const std::string&c,const std::string&a){return call_callback_string(c,a);}
std::string PythonAPI::py_get_current_buffer(){return editor&&editor->host_api?editor->host_api->core.buffer_content():"";}
void PythonAPI::py_set_current_buffer(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.set_buffer_content(s);}
std::string PythonAPI::py_get_selection(){return editor&&editor->host_api?editor->host_api->core.selected_text():"";}
void PythonAPI::py_replace_selection(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.replace_selection(s);}
void PythonAPI::py_insert_text(const std::string&s){if(editor&&editor->host_api)editor->host_api->core.insert_text(s);}
std::string PythonAPI::py_get_cursor(){if(!editor||!editor->host_api)return "0:0";auto p=editor->host_api->core.cursor();return std::to_string(p.first)+":"+std::to_string(p.second);}
void PythonAPI::py_set_cursor(int l,int c){if(editor&&editor->host_api)editor->host_api->core.set_cursor(l,c);}
std::string PythonAPI::py_current_file(){return editor&&editor->host_api?editor->host_api->core.current_file():"";}
void PythonAPI::py_open_file(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.open_file(s);}
void PythonAPI::py_save_current_file(){if(editor&&editor->host_api)editor->host_api->io.save_current_file();}
void PythonAPI::py_execute_command(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.execute_command(s);}
void PythonAPI::py_run_job(const std::string&a,const std::string&b,const std::string&c){if(editor&&editor->host_api)editor->host_api->io.run_job(a,b,c);}
void PythonAPI::py_show_picker(const std::string&a,const std::string&b,const std::string&c){if(editor&&editor->host_api)editor->host_api->io.show_plugin_picker(a,b,c);}
void PythonAPI::py_show_panel(const std::string&s){if(editor&&editor->host_api)editor->host_api->io.show_plugin_panel(s);}
std::vector<std::string> PythonAPI::plugin_panel_lines(const std::string&name){for(auto&p:plugin_panels)if(p.name==name){auto i=lua_callbacks.find(p.callback);if(i==lua_callbacks.end())return{};lua_State*L=static_cast<lua_State*>(lua_state);lua_rawgeti(L,LUA_REGISTRYINDEX,i->second);lua_pushstring(L,name.c_str());if(lua_pcall(L,1,1,0)) {lua_pop(L,1);return{};}std::vector<std::string> out;if(lua_istable(L,-1)){lua_pushnil(L);while(lua_next(L,-2)){out.push_back(lua_tostring(L,-1)?lua_tostring(L,-1):"");lua_pop(L,1);}}lua_pop(L,1);return out;}return{};}
std::vector<std::string> PythonAPI::plugin_picker_items(const std::string&callback){auto i=lua_callbacks.find(callback);if(i==lua_callbacks.end())return{};lua_State*L=static_cast<lua_State*>(lua_state);lua_rawgeti(L,LUA_REGISTRYINDEX,i->second);lua_pushstring(L,"");if(lua_pcall(L,1,1,0)){lua_pop(L,1);return{};}std::vector<std::string>out;if(lua_istable(L,-1)){lua_pushnil(L);while(lua_next(L,-2)){out.push_back(lua_tostring(L,-1)?lua_tostring(L,-1):"");lua_pop(L,1);}}lua_pop(L,1);return out;}
