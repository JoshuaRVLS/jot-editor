// Headless test of the bundled Lua toast module (src/lua/features/ui/toast.lua).
// The module is loaded into a raw Lua state whose jot.* API is stubbed with
// recording functions, so no Editor / terminal is needed. Covers show/stack/
// progress/animation/dismiss, the visible-stack cap, the jot.toast bridge
// registration, and the "toast.message" event-bus forwarding.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <signal.h>
#include <string>
#include <vector>
#include <unistd.h>

#include "core/editor.h"
#include "lua_bridge/api.h"
#include "lua_bridge/api_internal.h"
#include "ui/ui.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  struct StubState
  {
    int open_count = 0;
    int close_count = 0;
    int delete_count = 0;
    int set_spans_count = 0;
    int configure_count = 0;
    int redraw_count = 0;
    int register_count = 0;
    int handler_count = 0;
    int on_dismiss_count = 0;
    int last_col = 0;
    int last_row = 0;
    int last_width = 0;
    int last_height = 0;
    int last_fg = -1;
    int last_bg = -1;
    int last_border_fg = -1;
    int last_title_fg = -1;
    int last_mouse = 0;
    int last_zindex = 0;
    std::string last_border;
    int lines_count = 0;
    std::vector<int> configure_rows;
    std::vector<int> span_lens; // last span len per set_spans call
    std::vector<int> span_fgs;
    std::vector<int> interval_refs; // registry refs to set_interval callbacks
    int last_event_interval_ms = 0;
    int event_subscribe_count = 0;
    std::string last_event_name;
    int event_cb_ref = -1; // registry ref to the subscribed toast.message callback
  };

  StubState g;

  int stub_buffer_create(lua_State *L)
  {
    lua_pushinteger(L, 1);
    return 1;
  }

  int stub_buffer_set_lines(lua_State *L)
  {
    luaL_checktype(L, 5, LUA_TTABLE);
    int n = 0;
    for (;;)
    {
      lua_rawgeti(L, 5, n + 1);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      lua_pop(L, 1);
      ++n;
    }
    g.lines_count = n;
    return 0;
  }

  int stub_buffer_delete(lua_State *)
  {
    g.delete_count++;
    return 0;
  }

  int stub_float_open(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    g.open_count++;
    lua_getfield(L, 2, "col");
    g.last_col = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "row");
    g.last_row = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "width");
    g.last_width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "height");
    g.last_height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "border");
    g.last_border = lua_isnil(L, -1) ? "" : lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "fg");
    g.last_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "bg");
    g.last_bg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "border_fg");
    g.last_border_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "title_fg");
    g.last_title_fg = lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "mouse");
    g.last_mouse = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    lua_getfield(L, 2, "zindex");
    g.last_zindex = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, 1);
    return 1;
  }

  int stub_float_close(lua_State *)
  {
    g.close_count++;
    return 0;
  }

  int stub_float_configure(lua_State *L)
  {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "row");
    g.configure_rows.push_back((int)lua_tointeger(L, -1));
    lua_pop(L, 1);
    g.configure_count++;
    return 1;
  }

  int stub_float_set_spans(lua_State *L)
  {
    luaL_checktype(L, 3, LUA_TTABLE);
    g.set_spans_count++;
    int n = 0;
    for (;;)
    {
      lua_rawgeti(L, 3, n + 1);
      if (lua_isnil(L, -1))
      {
        lua_pop(L, 1);
        break;
      }
      if (lua_istable(L, -1))
      {
        lua_getfield(L, -1, "len");
        g.span_lens.push_back((int)lua_tointeger(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, -1, "fg");
        g.span_fgs.push_back(lua_isnil(L, -1) ? -1 : (int)lua_tointeger(L, -1));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      ++n;
    }
    return 1;
  }

  int stub_redraw(lua_State *)
  {
    g.redraw_count++;
    return 0;
  }

  int stub_timer_interval(lua_State *L)
  {
    g.last_event_interval_ms = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    g.interval_refs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    lua_pushinteger(L, (lua_Integer)g.interval_refs.size());
    return 1;
  }

  int stub_timer_clear(lua_State *)
  {
    return 0;
  }

  int stub_toast_register(lua_State *L)
  {
    luaL_checktype(L, 1, LUA_TTABLE);
    g.register_count++;
    return 0;
  }

  int stub_events_subscribe(lua_State *L)
  {
    g.event_subscribe_count++;
    g.last_event_name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    if (g.event_cb_ref >= 0)
    {
      luaL_unref(L, LUA_REGISTRYINDEX, g.event_cb_ref);
    }
    g.event_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushstring(L, "ev.toast.message.1");
    return 1;
  }

  int stub_on_dismiss(lua_State *)
  {
    g.on_dismiss_count++;
    return 0;
  }

  // Fires the n-th recorded set_interval callback (1-based).
  void fire_interval(lua_State *L, int which)
  {
    REQUIRE(which >= 1);
    REQUIRE(which <= (int)g.interval_refs.size());
    lua_rawgeti(L, LUA_REGISTRYINDEX, g.interval_refs[which - 1]);
    REQUIRE(lua_pcall(L, 0, 0, 0) == LUA_OK);
  }

  void push_stub_jot(lua_State *L)
  {
    lua_newtable(L); // jot
    lua_newtable(L); // jot.ui
    lua_pushcfunction(L,
                      [](lua_State *LL) -> int
                      {
                        luaL_checkstring(LL, 1);
                        g.handler_count++; // handler registrations
                        return 0;
                      });
    lua_setfield(L, -2, "handler");
    lua_newtable(L);
    lua_pushcfunction(L, stub_buffer_create);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, stub_buffer_set_lines);
    lua_setfield(L, -2, "set_lines");
    lua_pushcfunction(L, stub_buffer_delete);
    lua_setfield(L, -2, "delete");
    lua_setfield(L, -2, "buffer");
    lua_newtable(L);
    lua_pushcfunction(L, stub_float_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, stub_float_close);
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, stub_float_configure);
    lua_setfield(L, -2, "configure");
    lua_pushcfunction(L, stub_float_set_spans);
    lua_setfield(L, -2, "set_spans");
    lua_setfield(L, -2, "float");
    lua_setfield(L, -2, "ui"); // jot.ui

    lua_newtable(L); // jot.editor
    lua_pushcfunction(L, stub_redraw);
    lua_setfield(L, -2, "request_redraw");
    lua_setfield(L, -2, "editor");

    lua_newtable(L); // jot.viewport
    lua_pushcfunction(L,
                      [](lua_State *LL) -> int
                      {
                        lua_createtable(LL, 0, 1); // info
                        lua_createtable(LL, 0, 2); // window
                        lua_pushinteger(LL, 120);
                        lua_setfield(LL, -2, "width");
                        lua_pushinteger(LL, 40);
                        lua_setfield(LL, -2, "height");
                        lua_setfield(LL, -2, "window");
                        return 1;
                      });
    lua_setfield(L, -2, "info");
    lua_setfield(L, -2, "viewport");

    lua_newtable(L); // jot.timer
    lua_pushcfunction(L, stub_timer_interval);
    lua_setfield(L, -2, "set_interval");
    lua_pushcfunction(L, stub_timer_clear);
    lua_setfield(L, -2, "clear");
    lua_setfield(L, -2, "timer");

    lua_newtable(L); // jot.events
    lua_pushcfunction(L, stub_events_subscribe);
    lua_setfield(L, -2, "subscribe");
    lua_setfield(L, -2, "events");

    lua_newtable(L); // jot.toast
    lua_pushcfunction(L, stub_toast_register);
    lua_setfield(L, -2, "register");
    lua_setfield(L, -2, "toast");

    lua_setglobal(L, "jot");
  }

  void push_module_field(lua_State *L, int table_index, const char *name)
  {
    lua_getfield(L, table_index, name);
  }

  // Calls toast.show{...} on the ui.kit module table and returns the toast id.
  int call_show(lua_State *L, int module_index, const char *msg, int duration_ms)
  {
    lua_getfield(L, module_index, "toast");
    lua_getfield(L, -1, "show");
    lua_newtable(L);
    lua_pushstring(L, msg);
    lua_setfield(L, -2, "message");
    lua_pushinteger(L, duration_ms);
    lua_setfield(L, -2, "duration_ms");
    lua_pushcfunction(L, stub_on_dismiss);
    lua_setfield(L, -2, "on_dismiss");
    const int rc = lua_pcall(L, 1, 1, 0);
    if (rc != LUA_OK)
    {
      std::fprintf(stderr, "toast.show error: %s\n", lua_tostring(L, -1));
      luaL_traceback(L, L, nullptr, 2);
      std::fprintf(stderr, "traceback: %s\n", lua_tostring(L, -1));
    }
    REQUIRE(rc == LUA_OK);
    const int id = (int)lua_tointeger(L, -1);
    lua_pop(L, 2); // id + toast table
    return id;
  }

  int call_info_count(lua_State *L, int module_index)
  {
    lua_getfield(L, module_index, "toast");
    lua_getfield(L, -1, "info");
    REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
    lua_getfield(L, -1, "count");
    const int count = (int)lua_tointeger(L, -1);
    lua_pop(L, 3);
    return count;
  }
TEST_CASE("Lua float rendering paints buffer lines into the editor grid")
{
  struct sigaction sa {};
  sa.sa_handler = [](int) {
    void *frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, 2);
    _exit(1);
  };
  sigaction(SIGSEGV, &sa, nullptr);

  // A pristine config home so the Editor constructor's config bootstrap
  // doesn't load the real user config (which would call into an unhosted
  // Lua state).
  char cfgdir[] = "/tmp/jot_render_test_XXXXXX";
  mkdtemp(cfgdir);
  setenv("JOT_CONFIG_HOME", cfgdir, 1);
  setenv("JOT_CACHE_HOME", cfgdir, 1);
  // A minimal Editor with a UI grid exercises the real render_floats() path:
  // the frame/border/title are painted, and the scratch-buffer lines (the
  // toast body) must be painted inside the inset — this is the path the user
  // reported as missing (empty toast box).
  Terminal term; // inert until init(); safe to render into
  UI *ui = new UI(&term); // Editor's destructor deletes its ui
  Editor e;
  LuaAPI api(&e);
  api.attach_test_ui(ui);
  ui->resize(120, 40);

  const int buf = api.create_scratch_buffer(false, true);
  REQUIRE(buf > 0);
  REQUIRE(api.set_scratch_lines(buf, 0, -1, true, {"TOAST-PROBE-BODY", "SECOND"}));

  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  lua_newtable(L); // opts
  lua_pushinteger(L, 60);
  lua_setfield(L, -2, "col");
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "row");
  lua_pushinteger(L, 14);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, 4);
  lua_setfield(L, -2, "height");
  lua_pushstring(L, "rounded");
  lua_setfield(L, -2, "border");
  lua_pushstring(L, "TTITLE");
  lua_setfield(L, -2, "title");
  lua_pushinteger(L, 100000);
  lua_setfield(L, -2, "zindex");
  const int win = api.open_float(buf, false, L, lua_gettop(L));
  REQUIRE(win > 0);

  api.render_floats();

  auto cell = [&](int x, int y) { return ui->cell_at(x, y); };
  // Rounded corner at the topleft.
  REQUIRE(cell(60, 1)->ch == "\u256d"); // ╭
  // Title painted on the border row.
  REQUIRE(cell(62, 1)->ch == "T");
  // Body lines painted inside the inset (col+1,row+1).
  REQUIRE(cell(61, 2)->ch == "T");
  REQUIRE(cell(62, 2)->ch == "O");
  REQUIRE(cell(70, 2)->ch == "B"); // 9th char of TOAST-PROBE-BODY
  REQUIRE(cell(61, 3)->ch == "S"); // SECOND line

  lua_close(L);
}

TEST_CASE("jot.toast C++ forwarder marshals calls to the Lua module")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  lua_atpanic(L,
              [](lua_State *Ls) -> int
              {
                std::fprintf(stderr, "PANIC: %s\n", lua_tostring(Ls, -1));
                return 0;
              });
  luaL_openlibs(L);
  push_stub_jot(L);

  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  const int run_rc = lua_pcall(L, 0, 1, 0);
  if (run_rc != LUA_OK)
  {
    std::fprintf(stderr, "ui.lua load error: %s\n", lua_tostring(L, -1));
  }
  REQUIRE(run_rc == LUA_OK); // returns the kit module table

  // The app boot passes the toast module table as the FIRST argument to
  // jot.toast.register; put it in stack slot 1 the way the binding would.
  LuaAPI api(nullptr);
  lua_getfield(L, 1, "toast");
  lua_replace(L, 1); // [toast@1]
  api.register_toast_module(L);
  REQUIRE(api.toast_module_ref_ >= 0);
  lua_pop(L, 1); // clear so each forwarder call below sees only its own args

  // jot.toast.show{...} -> forwarder -> module.show -> float opens.
  lua_newtable(L);
  lua_pushstring(L, "via bridge");
  lua_setfield(L, -2, "message");
  lua_pushinteger(L, 100);
  lua_setfield(L, -2, "duration_ms");
  api.toast_show_from_lua(L); // base = 1 (the opts table)
  REQUIRE(g.open_count == 1);
  const int id = (int)lua_tointeger(L, -1);
  REQUIRE(id > 0);
  lua_pop(L, 1);

  // jot.toast.dismiss(id) -> forwarder -> module.dismiss -> float closed.
  lua_pushinteger(L, id);
  api.toast_dismiss_from_lua(L); // consumes the id from the stack
  REQUIRE(g.close_count == 1);

  // jot.toast.info() -> forwarder -> module.info -> count table.
  api.toast_info_from_lua(L);
  REQUIRE(lua_istable(L, -1));
  lua_getfield(L, -1, "count");
  REQUIRE((int)lua_tointeger(L, -1) == 0);
  lua_pop(L, 2);

  // The deprecated message channel delivers directly through the same path
  // jot.toast.show uses (no event-bus round trip).
  api.emit_toast_direct(L, "direct message", 80);
  REQUIRE(g.open_count == 2);
  api.toast_info_from_lua(L); // one direct toast remains
  REQUIRE(lua_istable(L, -1));
  lua_getfield(L, -1, "count");
  REQUIRE((int)lua_tointeger(L, -1) == 1);
  lua_pop(L, 2);

  for (int ref : g.interval_refs)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  if (g.event_cb_ref >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, g.event_cb_ref);
  }
  lua_close(L);
}

} // namespace

TEST_CASE("Bundled toast module shows, stacks, and auto-dismisses")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  lua_atpanic(L,
              [](lua_State *Ls) -> int
              {
                std::fprintf(stderr, "PANIC: %s\n", lua_tostring(Ls, -1));
                void *frames[32];
                int n = backtrace(frames, 32);
                char **sym = backtrace_symbols(frames, n);
                for (int i = 0; i < n; i++)
                {
                  std::fprintf(stderr, "  %s\n", sym[i]);
                }
                return 0;
              });
  luaL_openlibs(L);
  push_stub_jot(L);

  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  const int run_rc = lua_pcall(L, 0, 1, 0);
  if (run_rc != LUA_OK)
  {
    std::fprintf(stderr, "ui.lua load error: %s\n", lua_tostring(L, -1));
  }
  REQUIRE(run_rc == LUA_OK); // returns the module table
  REQUIRE(lua_istable(L, 1));

  // ui.lua calls jot.toast.register(toast) and toast.attach() at load time.
  REQUIRE(g.register_count == 1);
  REQUIRE(g.event_subscribe_count == 1);
  REQUIRE(g.last_event_name == "toast.message");

  // --- show() opens a rounded top-right float with correct geometry ---
  const int first = call_show(L, 1, "hello", 250);
  REQUIRE(first > 0);
  REQUIRE(g.open_count == 1);
  REQUIRE(g.last_border == "rounded");
  REQUIRE(g.last_width == 56);
  REQUIRE(g.last_height == 3); // single-line message becomes the title row, + 2 borders
  REQUIRE(g.last_col == 62);   // 120 - 56 - margin(1) - 1
  REQUIRE(g.last_row == 4);    // final row 1 + entry slide of 3
  REQUIRE(g.last_fg == 250);
  REQUIRE(g.last_bg == 235);
  REQUIRE(g.last_border_fg == 215); // level accent frames the toast
  REQUIRE(g.last_title_fg == 251);
  REQUIRE(g.last_mouse == 1);  // click-to-dismiss
  REQUIRE(g.last_zindex == 100000);
  REQUIRE(g.lines_count == 1); // title row only (message folded in, no progress)
  // Icon span carries the level accent; title text uses the title color.
  REQUIRE(g.span_lens.size() == 2);
  REQUIRE(g.span_fgs[0] == 215);
  // Timer registered at the 50 ms tick resolution.
  REQUIRE(g.last_event_interval_ms == 50);

  // --- slide-in: the row steps toward the final position over three ticks ---
  fire_interval(L, 1); // tick 1: row 1 + 2
  fire_interval(L, 1); // tick 2: row 1 + 1
  fire_interval(L, 1); // tick 3: row 1, sliding done
  REQUIRE(g.configure_count == 3);
  REQUIRE(g.configure_rows[0] == 3);
  REQUIRE(g.configure_rows[1] == 2);
  REQUIRE(g.configure_rows[2] == 1);

  // --- stacking: show a second toast before the first expires so they
  // overlap; it parks below the first one ---
  fire_interval(L, 1); // tick 4 (not yet expired)
  const int second = call_show(L, 1, "another message here", 400);
  REQUIRE(second > 0);
  REQUIRE(g.open_count == 2);
  // First toast: height 3, gap 1 -> second final row = margin 1 + 3 + 1 = 5,
  // then the +3 entry offset.
  REQUIRE(g.last_row == 8);

  // --- dismissing the first restacks the second up into its slot ---
  REQUIRE(call_info_count(L, 1) == 2);
  lua_getfield(L, 1, "toast");
  lua_getfield(L, -1, "dismiss");
  lua_pushinteger(L, first);
  REQUIRE(lua_pcall(L, 1, 0, 0) == LUA_OK);
  lua_pop(L, 1); // toast table
  REQUIRE(g.close_count == 1);
  REQUIRE(call_info_count(L, 1) == 1);
  REQUIRE(g.configure_rows.back() == 1); // restacked to the top slot

  // --- auto-dismiss of the second after 400 ms / 50 ms = 8 ticks ---
  for (int i = 0; i < 8; i++)
  {
    fire_interval(L, 2); // second toast's interval handle
  }
  REQUIRE(g.close_count == 2);
  REQUIRE(g.delete_count == 2);
  REQUIRE(g.on_dismiss_count == 2);
  REQUIRE(call_info_count(L, 1) == 0);

  // --- the visible cap evicts the oldest when filled ---
  const int first_new = call_show(L, 1, "one", 300);
  REQUIRE(first_new > 0);
  for (int i = 0; i < 4; i++)
  {
    call_show(L, 1, "more", 300);
  }
  REQUIRE(call_info_count(L, 1) == 5); // max_visible default
  call_show(L, 1, "overflow", 300);    // pushes the oldest out
  REQUIRE(call_info_count(L, 1) == 5);

  for (int ref : g.interval_refs)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  if (g.event_cb_ref >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, g.event_cb_ref);
  }
  lua_close(L);
}

TEST_CASE("Toast module forwards statusline messages from the event bus")
{
  g = StubState{};
  lua_State *L = luaL_newstate();
  REQUIRE(L != nullptr);
  luaL_openlibs(L);
  push_stub_jot(L);

  REQUIRE(jot_lua::load_ui_kit_modules(L));
  const std::string path = std::string(JOT_LUA_SOURCE_DIR) + "/features/ui.lua";
  REQUIRE(luaL_loadfile(L, path.c_str()) == LUA_OK);
  REQUIRE(lua_pcall(L, 0, 1, 0) == LUA_OK);
  REQUIRE(lua_istable(L, 1));

  // Simulate set_message: broadcast the toast.message event with a payload.
  REQUIRE(g.event_cb_ref >= 0);
  lua_rawgeti(L, LUA_REGISTRYINDEX, g.event_cb_ref);
  lua_newtable(L); // payload
  lua_pushstring(L, "Saved 3 buffers");
  lua_setfield(L, -2, "message");
  lua_pushinteger(L, 120);
  lua_setfield(L, -2, "duration_ms");
  REQUIRE(lua_pcall(L, 1, 0, 0) == LUA_OK);

  // The forwarded message became a normal info toast.
  REQUIRE(g.open_count == 1);
  REQUIRE(call_info_count(L, 1) == 1);
  REQUIRE(g.on_dismiss_count == 0);
  REQUIRE(g.last_title_fg == 251); // default info palette (no title given)

  for (int ref : g.interval_refs)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  if (g.event_cb_ref >= 0)
  {
    luaL_unref(L, LUA_REGISTRYINDEX, g.event_cb_ref);
  }
  lua_close(L);
}