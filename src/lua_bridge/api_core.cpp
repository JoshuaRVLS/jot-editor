#include "editor.h"
#include "host_api.h"
#include "lua_bridge/api.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <array>

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
int l_ui_buffer_create(lua_State *L){lua_pushinteger(L,api(L).create_scratch_buffer(lua_toboolean(L,1),lua_toboolean(L,2)));return 1;}
int l_ui_buffer_set_lines(lua_State *L){luaL_checktype(L,5,LUA_TTABLE);std::vector<std::string> v;for(int i=1;;i++){lua_rawgeti(L,5,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}v.emplace_back(luaL_checkstring(L,-1));lua_pop(L,1);}lua_pushboolean(L,api(L).set_scratch_lines((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),lua_toboolean(L,4),v));return 1;}
int l_ui_buffer_get_lines(lua_State *L){auto v=api(L).get_scratch_lines((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),lua_toboolean(L,4));lua_newtable(L);int n=1;for(auto&s:v){lua_pushlstring(L,s.data(),s.size());lua_rawseti(L,-2,n++);}return 1;}
int l_ui_buffer_delete(lua_State *L){lua_pushboolean(L,api(L).delete_scratch_buffer((int)luaL_checkinteger(L,1)));return 1;}
int l_ui_float_open(lua_State *L){luaL_checktype(L,3,LUA_TTABLE);lua_pushinteger(L,api(L).open_float((int)luaL_checkinteger(L,1),lua_toboolean(L,2),L,3));return 1;}
int l_ui_float_configure(lua_State *L){luaL_checktype(L,2,LUA_TTABLE);lua_pushboolean(L,api(L).configure_float((int)luaL_checkinteger(L,1),L,2));return 1;}
int l_ui_float_get_config(lua_State *L){int w=(int)luaL_checkinteger(L,1);lua_newtable(L);auto it=api(L).float_windows.find(w);if(it==api(L).float_windows.end())return 1;auto&f=it->second;lua_pushinteger(L,f.col);lua_setfield(L,-2,"col");lua_pushinteger(L,f.row);lua_setfield(L,-2,"row");lua_pushinteger(L,f.w);lua_setfield(L,-2,"width");lua_pushinteger(L,f.h);lua_setfield(L,-2,"height");lua_pushstring(L,f.relative.c_str());lua_setfield(L,-2,"relative");lua_pushstring(L,f.anchor.c_str());lua_setfield(L,-2,"anchor");lua_pushstring(L,f.border.c_str());lua_setfield(L,-2,"border");return 1;}
int l_ui_float_close(lua_State *L){lua_pushboolean(L,api(L).close_float((int)luaL_checkinteger(L,1),lua_toboolean(L,2)));return 1;}
int l_ui_float_is_valid(lua_State *L){lua_pushboolean(L,api(L).is_float_valid((int)luaL_checkinteger(L,1)));return 1;}
int l_ui_float_buffer(lua_State *L){int w=(int)luaL_checkinteger(L,1);auto it=api(L).float_windows.find(w);lua_pushinteger(L,it==api(L).float_windows.end()?0:it->second.buffer);return 1;}
int l_ui_float_focus(lua_State *L){int w=(int)luaL_checkinteger(L,1);lua_pushboolean(L,api(L).is_float_valid(w));if(lua_toboolean(L,-1))api(L).current_float_window=w;return 1;}
int l_ui_float_current(lua_State *L){lua_pushinteger(L,api(L).current_float_window);return 1;}
int l_float_open(lua_State *L){luaL_checktype(L,2,LUA_TTABLE);lua_pushinteger(L,api(L).open_float((int)luaL_checkinteger(L,1),true,L,2));return 1;}
int l_float_set_lines(lua_State *L){int w=(int)luaL_checkinteger(L,1);auto it=api(L).float_windows.find(w);if(it==api(L).float_windows.end())return(lua_pushboolean(L,0),1);luaL_checktype(L,2,LUA_TTABLE);lua_pushvalue(L,2);lua_replace(L,5);lua_pushinteger(L,it->second.buffer);lua_replace(L,1);lua_pushinteger(L,0);lua_replace(L,2);lua_pushinteger(L,-1);lua_replace(L,3);lua_pushboolean(L,0);lua_replace(L,4);return l_ui_buffer_set_lines(L);}
int l_float_get_lines(lua_State *L){int w=(int)luaL_checkinteger(L,1);auto it=api(L).float_windows.find(w);if(it==api(L).float_windows.end())return(lua_newtable(L),1);lua_pushinteger(L,it->second.buffer);lua_replace(L,1);lua_pushinteger(L,0);lua_replace(L,2);lua_pushinteger(L,-1);lua_replace(L,3);lua_pushboolean(L,0);lua_replace(L,4);return l_ui_buffer_get_lines(L);}
int l_float_configure(lua_State *L){luaL_checktype(L,2,LUA_TTABLE);lua_pushboolean(L,api(L).configure_float((int)luaL_checkinteger(L,1),L,2));return 1;}
int l_float_close(lua_State *L){lua_pushboolean(L,api(L).close_float((int)luaL_checkinteger(L,1),true));return 1;}
int l_float_on_key(lua_State *L){int w=(int)luaL_checkinteger(L,1);luaL_checktype(L,2,LUA_TFUNCTION);auto it=api(L).float_windows.find(w);if(it==api(L).float_windows.end())return(lua_pushboolean(L,0),1);if(it->second.key_callback>=0)luaL_unref(L,LUA_REGISTRYINDEX,it->second.key_callback);lua_pushvalue(L,2);it->second.key_callback=luaL_ref(L,LUA_REGISTRYINDEX);return(lua_pushboolean(L,1),1);}
int l_float_on_mouse(lua_State *L){int w=(int)luaL_checkinteger(L,1);luaL_checktype(L,2,LUA_TFUNCTION);auto it=api(L).float_windows.find(w);if(it==api(L).float_windows.end())return(lua_pushboolean(L,0),1);if(it->second.mouse_callback>=0)luaL_unref(L,LUA_REGISTRYINDEX,it->second.mouse_callback);lua_pushvalue(L,2);it->second.mouse_callback=luaL_ref(L,LUA_REGISTRYINDEX);it->second.mouse=true;return(lua_pushboolean(L,1),1);}
void inject(lua_State *L, LuaAPI *a, const char *name, lua_CFunction fn){lua_pushlightuserdata(L,a);lua_pushcclosure(L,fn,1);lua_setglobal(L,name);}
void field(lua_State *L, const char *name, lua_CFunction fn) { lua_pushcfunction(L,fn); lua_setfield(L,-2,name); }
void command_field(lua_State *L, LuaAPI *a, const char *name, const char *command) {
  lua_pushlightuserdata(L, a);
  lua_pushstring(L, command);
  lua_pushcclosure(L, [](lua_State *s) { api(s).execute_command(lua_tostring(s, lua_upvalueindex(2))); return 0; }, 2);
  lua_setfield(L, -2, name);
}
}

static int table_int(lua_State *L,int i,const char*k,int d){lua_getfield(L,i,k);int v=lua_isnumber(L,-1)?(int)lua_tointeger(L,-1):d;lua_pop(L,1);return v;}
static bool table_bool(lua_State *L,int i,const char*k,bool d){lua_getfield(L,i,k);bool v=lua_isboolean(L,-1)?lua_toboolean(L,-1):d;lua_pop(L,1);return v;}
static std::string table_string(lua_State *L,int i,const char*k,const std::string&d){lua_getfield(L,i,k);std::string v=lua_isstring(L,-1)?lua_tostring(L,-1):d;lua_pop(L,1);return v;}

LuaAPI::LuaAPI(Editor *ed) : editor(ed), lua_state(nullptr), lua_initialized(false) { active_api=this; }
EditorHostAPI &LuaAPI::host() { return *editor->host_api; }
LuaAPI::~LuaAPI(){ cleanup(); if(active_api==this) active_api=nullptr; }
int LuaAPI::create_scratch_buffer(bool listed, bool scratch) { int id=next_scratch_buffer++; scratch_buffers.emplace(id,LuaScratchBuffer{id,listed,scratch,true,{""}}); return id; }
bool LuaAPI::set_scratch_lines(int id,int start,int end,bool strict,const std::vector<std::string>&v){auto it=scratch_buffers.find(id);if(it==scratch_buffers.end()||!it->second.valid)return false;auto&l=it->second.lines;int n=(int)l.size();if(start<0)start=n+start;if(end<0)end=n+end;if(strict&&(start<0||end<start||start>n||end>n))return false;start=std::clamp(start,0,n);end=std::clamp(end,start,n);l.erase(l.begin()+start,l.begin()+end);l.insert(l.begin()+start,v.begin(),v.end());if(l.empty())l.push_back("");return true;}
std::vector<std::string> LuaAPI::get_scratch_lines(int id,int start,int end,bool strict)const{auto it=scratch_buffers.find(id);if(it==scratch_buffers.end()||!it->second.valid)return{};auto l=it->second.lines;int n=(int)l.size();if(start<0)start=n+start;if(end<0)end=n+end;if(strict&&(start<0||end<start||start>n||end>n))return{};start=std::clamp(start,0,n);end=std::clamp(end,start,n);return{l.begin()+start,l.begin()+end};}
bool LuaAPI::delete_scratch_buffer(int id){auto it=scratch_buffers.find(id);if(it==scratch_buffers.end())return false;for(auto wi=float_windows.begin();wi!=float_windows.end();)if(wi->second.buffer==id){if(lua_state){if(wi->second.key_callback>=0)luaL_unref((lua_State*)lua_state,LUA_REGISTRYINDEX,wi->second.key_callback);if(wi->second.mouse_callback>=0)luaL_unref((lua_State*)lua_state,LUA_REGISTRYINDEX,wi->second.mouse_callback);}wi=float_windows.erase(wi);}else++wi;it->second.valid=false;scratch_buffers.erase(it);return true;}
bool LuaAPI::configure_float(int id,lua_State*L,int ti){auto it=float_windows.find(id);if(it==float_windows.end())return false;auto&f=it->second;f.x=table_int(L,ti,"col",f.x);f.y=table_int(L,ti,"row",f.y);f.col=f.x;f.row=f.y;f.w=table_int(L,ti,"width",f.w);f.h=table_int(L,ti,"height",f.h);f.relative=table_string(L,ti,"relative",f.relative);f.anchor=table_string(L,ti,"anchor",f.anchor);f.border=table_string(L,ti,"border",f.border);if(f.w<1||f.h<1||(f.relative!="editor"&&f.relative!="cursor"&&f.relative!="win"&&f.relative!="mouse")||(f.border!="none"&&f.border!="single"&&f.border!="double"&&f.border!="rounded"&&f.border!="custom"))return false;f.zindex=table_int(L,ti,"zindex",f.zindex);f.focusable=table_bool(L,ti,"focusable",f.focusable);f.mouse=table_bool(L,ti,"mouse",f.mouse);f.hide=table_bool(L,ti,"hide",f.hide);f.style_minimal=table_bool(L,ti,"style_minimal",f.style_minimal);f.title=table_string(L,ti,"title",f.title);f.footer=table_string(L,ti,"footer",f.footer);f.fg=table_int(L,ti,"fg",f.fg);f.bg=table_int(L,ti,"bg",f.bg);lua_getfield(L,ti,"style");if(lua_istable(L,-1)){f.style_minimal=table_bool(L,-1,"minimal",f.style_minimal);f.fg=table_int(L,-1,"fg",f.fg);f.bg=table_int(L,-1,"bg",f.bg);}lua_pop(L,1);lua_getfield(L,ti,"border_chars");if(lua_istable(L,-1))for(int i=0;i<8;i++){lua_rawgeti(L,-1,i+1);if(lua_isstring(L,-1))f.custom_border[i]=lua_tostring(L,-1);lua_pop(L,1);}lua_pop(L,1);for(const char*key:{"on_key","key_callback"}){lua_getfield(L,ti,key);if(lua_isfunction(L,-1)){if(f.key_callback>=0)luaL_unref(L,LUA_REGISTRYINDEX,f.key_callback);lua_pushvalue(L,-1);f.key_callback=luaL_ref(L,LUA_REGISTRYINDEX);lua_pop(L,1);break;}lua_pop(L,1);}for(const char*key:{"on_mouse","mouse_callback"}){lua_getfield(L,ti,key);if(lua_isfunction(L,-1)){if(f.mouse_callback>=0)luaL_unref(L,LUA_REGISTRYINDEX,f.mouse_callback);lua_pushvalue(L,-1);f.mouse_callback=luaL_ref(L,LUA_REGISTRYINDEX);lua_pop(L,1);break;}lua_pop(L,1);}return true;}
int LuaAPI::open_float(int buffer,bool enter,lua_State*L,int ti){if(scratch_buffers.find(buffer)==scratch_buffers.end())return 0;LuaFloatWindow f;f.handle=next_float_window++;f.buffer=buffer;f.enter=enter;f.creation_order=next_float_order++;float_windows.emplace(f.handle,f);if(!configure_float(f.handle,L,ti)){float_windows.erase(f.handle);return 0;}if(enter)current_float_window=f.handle;return f.handle;}
bool LuaAPI::close_float(int id,bool){auto it=float_windows.find(id);if(it==float_windows.end())return false;if(lua_state){if(it->second.key_callback>=0)luaL_unref((lua_State*)lua_state,LUA_REGISTRYINDEX,it->second.key_callback);if(it->second.mouse_callback>=0)luaL_unref((lua_State*)lua_state,LUA_REGISTRYINDEX,it->second.mouse_callback);}float_windows.erase(it);if(current_float_window==id)current_float_window=0;return true;}
bool LuaAPI::is_float_valid(int id)const{return float_windows.find(id)!=float_windows.end();}
void LuaAPI::clear_floats(){while(!float_windows.empty())close_float(float_windows.begin()->first,true);scratch_buffers.clear();current_float_window=0;}
bool LuaAPI::float_input(int ch,bool ctrl,bool shift,bool alt){if(!lua_state||!editor||!editor->event_loop_.is_main_thread())return false;std::vector<LuaFloatWindow*> fs;for(auto&x:float_windows)fs.push_back(&x.second);std::sort(fs.begin(),fs.end(),[](auto*a,auto*b){return a->zindex!=b->zindex?a->zindex>b->zindex:a->creation_order>b->creation_order;});for(auto*f:fs){if(f->hide||!f->focusable||f->key_callback<0)continue;int handle=f->handle;lua_State*L=(lua_State*)lua_state;int top=lua_gettop(L);lua_rawgeti(L,LUA_REGISTRYINDEX,f->key_callback);lua_newtable(L);lua_pushinteger(L,ch);lua_setfield(L,-2,"key");lua_pushboolean(L,ctrl);lua_setfield(L,-2,"ctrl");lua_pushboolean(L,shift);lua_setfield(L,-2,"shift");lua_pushboolean(L,alt);lua_setfield(L,-2,"alt");lua_pushinteger(L,handle);lua_setfield(L,-2,"window");int ok=lua_pcall(L,1,1,0)==LUA_OK;bool consume=ok&&lua_toboolean(L,-1);if(!ok)std::cerr<<"Lua float key callback error: "<<lua_tostring(L,-1)<<"\n";lua_settop(L,top);if(consume){current_float_window=handle;return true;}}return false;}
bool LuaAPI::float_mouse(int x,int y,int button,bool pressed,bool released,bool motion,bool ctrl,bool shift,bool alt){if(!lua_state||!editor||!editor->event_loop_.is_main_thread())return false;for(auto it=float_windows.rbegin();it!=float_windows.rend();++it){auto&f=it->second;if(f.hide||f.mouse_callback<0)continue;int handle=f.handle;int bx=f.x,by=f.y;if(x<bx||x>=bx+f.w||y<by||y>=by+f.h)continue;lua_State*L=(lua_State*)lua_state;int top=lua_gettop(L);lua_rawgeti(L,LUA_REGISTRYINDEX,f.mouse_callback);lua_newtable(L);lua_pushinteger(L,x-bx);lua_setfield(L,-2,"col");lua_pushinteger(L,y-by);lua_setfield(L,-2,"row");lua_pushinteger(L,button);lua_setfield(L,-2,"button");lua_pushboolean(L,pressed);lua_setfield(L,-2,"pressed");lua_pushboolean(L,released);lua_setfield(L,-2,"released");lua_pushboolean(L,motion);lua_setfield(L,-2,"motion");lua_pushboolean(L,ctrl);lua_setfield(L,-2,"ctrl");lua_pushboolean(L,shift);lua_setfield(L,-2,"shift");lua_pushboolean(L,alt);lua_setfield(L,-2,"alt");int ok=lua_pcall(L,1,1,0)==LUA_OK;bool consume=ok&&lua_toboolean(L,-1);if(!ok)std::cerr<<"Lua float mouse callback error: "<<lua_tostring(L,-1)<<"\n";lua_settop(L,top);if(consume){current_float_window=handle;return true;}}return false;}
void LuaAPI::render_floats(){if(!editor||!editor->ui)return;int rw=editor->ui->get_render_width(),rh=std::max(1,editor->ui->get_height()-editor->status_height);std::vector<LuaFloatWindow*> fs;for(auto&x:float_windows)if(!x.second.hide)fs.push_back(&x.second);std::sort(fs.begin(),fs.end(),[](auto*a,auto*b){return a->zindex!=b->zindex?a->zindex<b->zindex:a->creation_order<b->creation_order;});for(auto*f:fs){int x=f->col,y=f->row;if(f->relative=="cursor"&&!editor->panes.empty()){auto&p=editor->get_pane();auto&b=editor->get_buffer(p.buffer_id);x=p.x+9+b.cursor.x;y=p.y+editor->tab_height+b.cursor.y-b.scroll_offset;}else if(f->relative=="win"&&!editor->panes.empty()){auto&p=editor->get_pane();x=p.x+f->col;y=p.y+f->row;}if(f->anchor.find('S')!=std::string::npos)y-=f->h;if(f->anchor.find('E')!=std::string::npos)x-=f->w;x=std::clamp(x,0,std::max(0,rw-f->w));y=std::clamp(y,0,std::max(0,rh-f->h));f->x=x;f->y=y;UIRect r{x,y,std::min(f->w,rw-x),std::min(f->h,rh-y)};editor->ui->fill_rect(r," ",f->fg,f->bg);if(f->border != "none"){std::array<std::string,8>b={"─","│","─","│","┌","┐","┘","└"};if(f->border=="double")b={"═","║","═","║","╔","╗","╝","╚"};if(f->border=="rounded")b={"─","│","─","│","╭","╮","╯","╰"};if(f->border=="custom")b=f->custom_border;editor->ui->draw_text(x,y,b[4],f->fg,f->bg);editor->ui->draw_text(x+f->w-1,y,b[5],f->fg,f->bg);editor->ui->draw_text(x,y+f->h-1,b[7],f->fg,f->bg);editor->ui->draw_text(x+f->w-1,y+f->h-1,b[6],f->fg,f->bg);for(int i=1;i<f->w-1;i++){editor->ui->draw_text(x+i,y,b[0],f->fg,f->bg);editor->ui->draw_text(x+i,y+f->h-1,b[2],f->fg,f->bg);}for(int i=1;i<f->h-1;i++){editor->ui->draw_text(x,y+i,b[3],f->fg,f->bg);editor->ui->draw_text(x+f->w-1,y+i,b[1],f->fg,f->bg);}}auto bi=scratch_buffers.find(f->buffer);if(bi==scratch_buffers.end())continue;int ix=x+(f->border=="none"?0:1),iy=y+(f->border=="none"?0:1),iw=std::max(0,r.w-(f->border=="none"?0:2)),ih=std::max(0,r.h-(f->border=="none"?0:2));for(int i=0;i<ih&&i<(int)bi->second.lines.size();i++)editor->ui->draw_text(ix,iy+i,ui_truncate_cells(bi->second.lines[i],iw),f->fg,f->bg);if(!f->title.empty())editor->ui->draw_text(x+1,y,ui_truncate_cells(" "+f->title+" ",std::max(0,r.w-2)),f->fg,f->bg,true);if(!f->footer.empty())editor->ui->draw_text(x+1,y+r.h-1,ui_truncate_cells(" "+f->footer+" ",std::max(0,r.w-2)),f->fg,f->bg);}}
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
      "jot.api={set_theme_color=set_hl}; vim=jot")) {
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
   lua_getglobal(L,"jot");lua_getfield(L,-1,"ui");lua_newtable(L);field(L,"create",l_ui_buffer_create);field(L,"set_lines",l_ui_buffer_set_lines);field(L,"get_lines",l_ui_buffer_get_lines);field(L,"delete",l_ui_buffer_delete);lua_setfield(L,-2,"buffer");lua_newtable(L);field(L,"open",l_float_open);field(L,"set_lines",l_float_set_lines);field(L,"get_lines",l_float_get_lines);field(L,"configure",l_float_configure);field(L,"get_config",l_ui_float_get_config);field(L,"close",l_float_close);field(L,"is_valid",l_ui_float_is_valid);field(L,"buffer",l_ui_float_buffer);field(L,"focus",l_ui_float_focus);field(L,"current",l_ui_float_current);field(L,"on_key",l_float_on_key);field(L,"on_mouse",l_float_on_mouse);lua_setfield(L,-2,"float");lua_pop(L,2);
   lua_getglobal(L,"jot");lua_getfield(L,-1,"theme");field(L,"set_color",[](lua_State*s){auto&a=api(s);luaL_checktype(s,2,LUA_TTABLE);lua_getfield(s,2,"fg");int fg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);lua_getfield(s,2,"bg");int bg=lua_isnumber(s,-1)?(int)lua_tointeger(s,-1):-1;lua_pop(s,1);a.set_theme_color(luaL_optstring(s,1,""),fg,bg);return 0;});lua_pop(L,2);
  lua_getglobal(L,"jot");lua_getfield(L,-1,"ui");field(L,"register_panel",l_register_panel);lua_pop(L,2);
  load_plugins();
  return true;
}
 void LuaAPI::cleanup(){if(!lua_state)return;clear_floats();lua_close(static_cast<lua_State*>(lua_state));lua_state=nullptr;lua_callbacks.clear();lua_initialized=false;}
 void LuaAPI::clear_runtime_state(){plugin_commands.clear();plugin_keymaps.clear();plugin_autocmds.clear();plugin_panels.clear();plugin_load_status.clear();clear_floats();if(lua_state){for(auto&x:lua_callbacks)luaL_unref(static_cast<lua_State*>(lua_state),LUA_REGISTRYINDEX,x.second);}lua_callbacks.clear();}
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
