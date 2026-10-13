// Headless tests for the bundled HTML preview session
// (runtime/lua/features/html/*). The module tree is loaded into a raw Lua state
// whose `jot` API is stubbed, so no Editor, no terminal and no HTTP server is
// needed to check what the session asks the transport to serve and when it asks
// the browser to reload.
//
// Two things here are worth pinning above the end-to-end probe. The URL: it is
// the one part of the feature whose failure mode is a 404 in the browser rather
// than a bad preview, and it is built from two absolute paths, so the relative
// step and the encoding are asserted directly. And the reload policy: a page
// load per keystroke is a flicker instead of a preview, so the debounce and the
// "only when the text changed" rule are what the session exists for.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// lua_rawlen on a runtime older than 5.2, which is what LuaJIT is.
#include "lua/lua_compat.h"

namespace
{
  // The `jot` stub: a config store, a buffer whose text the test sets, a
  // workspace path, and a preview transport that records every call. The
  // markdown preview's config and browser modules are loaded first because the
  // HTML session shares its launcher, exactly as the native loader does.
  const char *kPrelude = R"LUA(
    local html_dir, md_dir = ...
    store = {}
    calls = {}
    buffer_path = nil
    buffer_text = ""
    workspace_path = ""

    jot = {
      config = {
        get = function(k, d) local v = store[k]; if v == nil then return d end; return v end,
        get_bool = function(k, d) local v = store[k]; if v == nil then return d end; return v == true end,
        get_number = function(k, d) local v = store[k]; if v == nil then return d end; return tonumber(v) end,
        set = function(k, v) store[k] = v end,
      },
      buffer = {
        meta = function()
          if not buffer_path then return nil end
          return { path = buffer_path, name = buffer_path:match("[^/\\]+$") }
        end,
        text = function() return buffer_text end,
      },
      workspace = { path = function() return workspace_path end },
      ui = { show_message = function() end },
      preview = {
        start = function(opts)
          table.insert(calls, { "start", tostring(opts.port) .. "," .. tostring(opts.file_root) })
          return true, 40123
        end,
        stop = function() table.insert(calls, { "stop", "" }) end,
        set_document = function(rel, text)
          table.insert(calls, { "set_document", tostring(rel) .. "," .. tostring(text) })
        end,
        notify = function(event, data)
          table.insert(calls, { "notify", tostring(event) .. "," .. tostring(data) })
        end,
      },
      timer = {
        -- Timeouts are recorded rather than run: draining them by hand is what
        -- makes the debounce's outcome assertable.
        set_timeout = function(ms, fn) table.insert(timers, fn); return #timers end,
        set_interval = function() return 0 end,
        clear = function() end,
      },
    }
    timers = {}

    package.loaded["jot_md.config"] = assert(loadfile(md_dir .. "config.lua"))()
    package.loaded["jot_md.browser"] = assert(loadfile(md_dir .. "browser.lua"))()
    package.loaded["jot_html.config"] = assert(loadfile(html_dir .. "config.lua"))()
    package.loaded["jot_html.preview"] = assert(loadfile(html_dir .. "preview.lua"))()
    config = package.loaded["jot_html.config"]
    preview = package.loaded["jot_html.preview"]
  )LUA";

  struct HtmlState
  {
    lua_State *L = nullptr;

    HtmlState()
    {
      L = luaL_newstate();
      REQUIRE(L != nullptr);
      luaL_openlibs(L);
      const std::string html_dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/html/";
      const std::string md_dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/markdown/";
      REQUIRE(luaL_loadstring(L, kPrelude) == LUA_OK);
      lua_pushstring(L, html_dir.c_str());
      lua_pushstring(L, md_dir.c_str());
      REQUIRE(lua_pcall(L, 2, 0, 0) == LUA_OK);
    }

    ~HtmlState()
    {
      if (L)
        lua_close(L);
    }

    HtmlState(const HtmlState &) = delete;
    HtmlState &operator=(const HtmlState &) = delete;

    void set(const std::string &name, const std::string &value)
    {
      lua_pushstring(L, value.c_str());
      lua_setglobal(L, name.c_str());
    }

    void set_config(const std::string &key, const std::string &value)
    {
      lua_getglobal(L, "jot");
      lua_getfield(L, -1, "config");
      lua_getfield(L, -1, "set");
      lua_pushstring(L, key.c_str());
      lua_pushstring(L, value.c_str());
      REQUIRE(lua_pcall(L, 2, 0, 0) == LUA_OK);
      lua_settop(L, 0);
    }

    // Calls `preview.<name>(args...)`. `results` is how many values the callee
    // returns (1 for the helpers that answer, 0 for the ones that act).
    void push_call(const std::string &name, const std::vector<std::string> &args, int results)
    {
      lua_getglobal(L, "preview");
      lua_getfield(L, -1, name.c_str());
      for (const std::string &arg : args)
      {
        lua_pushstring(L, arg.c_str());
      }
      REQUIRE(lua_pcall(L, (int)args.size(), results, 0) == LUA_OK);
    }

    std::string call(const std::string &name, const std::vector<std::string> &args = {})
    {
      push_call(name, args, 1);
      const char *result = lua_tostring(L, -1);
      const std::string out = result ? result : "";
      lua_pop(L, 2); // the result and the preview table
      return out;
    }

    bool call_ok(const std::string &name, const std::vector<std::string> &args = {})
    {
      push_call(name, args, 1);
      const bool ok = lua_toboolean(L, -1) != 0;
      lua_pop(L, 2);
      return ok;
    }

    void call_void(const std::string &name, const std::vector<std::string> &args = {})
    {
      push_call(name, args, 0);
      lua_pop(L, 1); // the preview table
    }

    // Every recorded call as "name:payload".
    std::vector<std::string> calls()
    {
      std::vector<std::string> out;
      lua_getglobal(L, "calls");
      lua_pushnil(L);
      while (lua_next(L, -2) != 0)
      {
        std::string line;
        lua_rawgeti(L, -1, 1);
        line += lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        line += ":";
        lua_rawgeti(L, -1, 2);
        line += lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        out.push_back(line);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      return out;
    }

    static bool has_call(const std::vector<std::string> &list, const std::string &prefix)
    {
      for (const std::string &entry : list)
      {
        if (entry.rfind(prefix, 0) == 0)
        {
          return true;
        }
      }
      return false;
    }

    void clear_calls()
    {
      lua_newtable(L);
      lua_setglobal(L, "calls");
      lua_settop(L, 0);
    }

    // Runs every timer the session armed (the debounce from on_buffer_change).
    void run_timers()
    {
      lua_getglobal(L, "timers");
      const int count = (int)lua_rawlen(L, -1);
      for (int i = 1; i <= count; i++)
      {
        lua_rawgeti(L, -1, i);
        REQUIRE(lua_pcall(L, 0, 0, 0) == LUA_OK);
      }
      lua_pop(L, 1);
      lua_newtable(L);
      lua_setglobal(L, "timers");
      lua_settop(L, 0);
    }
  };
} // namespace

TEST_CASE("HTML preview: the URL is the file's path relative to the served root", "[html][lua]")
{
  HtmlState state;

  REQUIRE(state.call("url_path", {"/w/project", "/w/project/index.html"}) == "index.html");
  REQUIRE(state.call("url_path", {"/w/project", "/w/project/pages/about.html"})
          == "pages/about.html");
  // The root's own trailing separator does not double the one in the URL ...
  REQUIRE(state.call("url_path", {"/w/project/", "/w/project/a/b.html"}) == "a/b.html");
  // ... and a sibling directory that merely shares the prefix is not inside it.
  REQUIRE(state.call("url_path", {"/w/pro", "/w/project/index.html"}).empty());
  // A path a URL cannot carry literally is encoded, not passed through.
  REQUIRE(state.call("url_path", {"/w/p", "/w/p/my page.html"}) == "my%20page.html");
  REQUIRE(state.call("url_path", {"/w/p", "/w/p/a#b.html"}) == "a%23b.html");
}

TEST_CASE("HTML preview: the served root is the workspace the file lives in", "[html][lua]")
{
  HtmlState state;
  state.set("buffer_path", "/w/project/index.html");
  state.set("buffer_text", "<h1>a</h1>\n");
  state.set("workspace_path", "/w/project");

  REQUIRE(state.call_ok("start"));
  {
    const auto list = state.calls();
    INFO("calls: " << (list.empty() ? "" : list[0]));
    // The transport is told to serve the tree, and the buffer's own text is the
    // document that tree answers with.
    REQUIRE(HtmlState::has_call(list, "start:0,/w/project"));
    REQUIRE(HtmlState::has_call(list, "set_document:index.html,<h1>a</h1>\n"));
    REQUIRE(HtmlState::has_call(list, "notify:reload,index.html"));
  }
  REQUIRE(state.call("url") == "http://127.0.0.1:40123/index.html");
  state.call_void("stop");

  // With `root = "dir"` the file's own directory is the root, so the URL is the
  // bare name however deep the file sits.
  state.set_config("html_preview_root", "dir");
  state.set("buffer_path", "/w/project/pages/about.html");
  REQUIRE(state.call_ok("start"));
  REQUIRE(state.call("url") == "http://127.0.0.1:40123/about.html");
  state.call_void("stop");
}

TEST_CASE("HTML preview: edits reload the page, once, and only when it changed", "[html][lua]")
{
  HtmlState state;
  state.set("buffer_path", "/w/site/index.html");
  state.set("buffer_text", "<p>one</p>\n");
  state.set("workspace_path", "/w/site");
  REQUIRE(state.call_ok("start"));
  REQUIRE(HtmlState::has_call(state.calls(), "notify:reload,index.html"));
  state.clear_calls();

  // The edit schedules, it does not push: a document load per keystroke is a
  // flicker, not a preview.
  state.set("buffer_text", "<p>two</p>\n");
  state.call_void("on_buffer_change");
  REQUIRE(state.calls().empty());

  // The pending timer fires: now the new text is served and the page reloaded.
  state.run_timers();
  {
    const auto list = state.calls();
    REQUIRE(HtmlState::has_call(list, "set_document:index.html,<p>two</p>\n"));
    REQUIRE(HtmlState::has_call(list, "notify:reload,index.html"));
  }

  // A change that leaves the text where the page already is is not a reload:
  // typing and undoing back must not reload the browser.
  state.clear_calls();
  state.call_void("on_buffer_change");
  state.run_timers();
  REQUIRE(state.calls().empty());

  // A save is not debounced: the file changed on disk as well.
  state.set("buffer_text", "<p>three</p>\n");
  state.call_void("on_buffer_save");
  {
    const auto list = state.calls();
    REQUIRE(HtmlState::has_call(list, "set_document:index.html,<p>three</p>\n"));
    REQUIRE(HtmlState::has_call(list, "notify:reload,index.html"));
  }
  state.call_void("stop");
}

TEST_CASE("HTML preview: only HTML buffers start it", "[html][lua]")
{
  HtmlState state;
  state.set("workspace_path", "/w/site");
  state.set("buffer_text", "x\n");

  state.set("buffer_path", "/w/site/notes.md");
  REQUIRE_FALSE(state.call_ok("start"));
  REQUIRE(state.calls().empty());

  state.set("buffer_path", "/w/site/style.css");
  REQUIRE_FALSE(state.call_ok("start"));
  REQUIRE(state.calls().empty());

  state.set("buffer_path", "/w/site/page.html");
  REQUIRE(state.call_ok("start"));
  REQUIRE(HtmlState::has_call(state.calls(), "set_document:page.html,"));
  state.call_void("stop");
}
