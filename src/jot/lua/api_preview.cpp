// LuaAPI preview surface: the native half of the markdown preview transport.
//
// The Lua markdown feature (runtime/lua/features/markdown/) owns all of the
// content policy — parsing, the page template, config, commands. This file
// owns only the transport: it wraps PreviewServer (see
// markdown/preview_server.h) and exposes it as jot.preview.*.
#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/api_internal.h"
#include "markdown/preview_server.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

using namespace jot_lua;

PreviewServer *LuaAPI::ensure_preview_server()
{
  if (!editor)
    return nullptr;
  if (!preview_server_)
    preview_server_.reset(new PreviewServer(&editor->event_loop_));
  return preview_server_.get();
}

void LuaAPI::preview_start_from_lua(lua_State *L)
{
  PreviewServer *server = ensure_preview_server();
  if (!server)
  {
    lua_pushnil(L);
    lua_pushstring(L, "preview transport unavailable");
    return;
  }
  std::string host = "127.0.0.1";
  int port = 0;
  if (lua_istable(L, 1))
  {
    host = table_string(L, 1, "host", host);
    port = table_int(L, 1, "port", port);
  }

  int bound_port = 0;
  std::string error;
  if (!server->start(host, port, &bound_port, &error))
  {
    lua_pushnil(L);
    lua_pushstring(L, error.c_str());
    return;
  }
  lua_pushboolean(L, 1);
  lua_pushinteger(L, bound_port);
}

void LuaAPI::preview_stop_from_lua(lua_State *L)
{
  (void)L;
  if (preview_server_)
    preview_server_->stop();
}

void LuaAPI::preview_status_from_lua(lua_State *L)
{
  lua_newtable(L);
  const int t = lua_gettop(L);
  lua_set_bool_field(L, t, "running", preview_server_ && preview_server_->running());
  lua_set_int_field(L, t, "port", preview_server_ ? preview_server_->port() : 0);
  lua_set_int_field(L, t, "clients", preview_server_ ? preview_server_->client_count() : 0);
}

void LuaAPI::preview_set_page_from_lua(lua_State *L)
{
  PreviewServer *server = ensure_preview_server();
  if (!server)
    return;
  size_t len = 0;
  const char *html = luaL_optlstring(L, 1, "", &len);
  server->set_page(std::string(html, len));
}

void LuaAPI::preview_page_from_lua(lua_State *L)
{
  if (!preview_server_)
  {
    lua_pushstring(L, "");
    return;
  }
  const std::string &page = preview_server_->page();
  lua_pushlstring(L, page.data(), page.size());
}

void LuaAPI::preview_set_content_from_lua(lua_State *L)
{
  PreviewServer *server = ensure_preview_server();
  if (!server)
    return;
  size_t len = 0;
  const char *body = luaL_optlstring(L, 1, "", &len);
  server->set_content(std::string(body, len));
}

void LuaAPI::preview_notify_from_lua(lua_State *L)
{
  if (!preview_server_)
    return;
  const std::string event = luaL_optstring(L, 1, "message");
  size_t len = 0;
  const char *data = luaL_optlstring(L, 2, "", &len);
  preview_server_->notify(event, std::string(data, len));
}

void LuaAPI::preview_sync_from_lua(lua_State *L)
{
  if (preview_server_)
    preview_server_->sync_clients((int)luaL_checkinteger(L, 1));
}

void LuaAPI::preview_take_scroll_from_lua(lua_State *L)
{
  const int line = preview_server_ ? preview_server_->take_scroll() : -1;
  if (line < 0)
    lua_pushnil(L);
  else
    lua_pushinteger(L, line);
}
