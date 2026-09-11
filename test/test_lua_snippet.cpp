// Headless tests for the bundled snippet engine (runtime/lua/features/snippet/*).
//
// The module tree is loaded into a raw Lua state whose `jot` API is stubbed by
// an in-memory buffer + cursor, so a full expansion (parse -> session render ->
// buffer write -> jump/choice navigation) can be driven without an Editor, a
// terminal or a real window.
#include <catch2/catch_test_macros.hpp>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace
{
  // The `jot` stub: an in-memory line array, a cursor and no-op decorations.
  // `apply_edit` mirrors HostCoreAPI::apply_edit's splice semantics (half-open
  // range, head/tail kept, `\n` split into lines).
  const char *kPrelude = R"LUA(
    local dir = ...
    local cfg = {}
    local lines = { "" }
    local cur = { line = 1, col = 1 }
    local deco_id = 0
    local test_snippet_handler = nil

    local function line_starts(t)
      local starts, off = {}, 0
      for i = 1, #t do starts[i] = off; off = off + #t[i] + 1 end
      starts[#t + 1] = off
      return starts
    end

    jot = {
      config = {
        get = function(k, d) local v = cfg[k]; if v == nil then return d end; return v end,
        get_bool = function(k, d) local v = cfg[k]; if v == nil then return d end; return v == true end,
        get_number = function(k, d) local v = cfg[k]; if v == nil then return d end; return tonumber(v) end,
        set = function(k, v) cfg[k] = v end,
      },
      buffer = {
        lines = function() return lines end,
        get_line = function(i) return lines[i] or "" end,
        meta = function() return { line_count = #lines, path = "/tmp/test.lua", name = "test.lua" } end,
        filetype = function() return "lua" end,
        get_selection = function() return "" end,
        apply_edit = function(l1, c1, l2, c2, text)
          local buf = table.concat(lines, "\n")
          local starts = line_starts(lines)
          local a = (starts[l1] or 0) + c1 - 1
          local b = (starts[l2] or 0) + c2 - 1
          if b < a then a, b = b, a end
          buf = buf:sub(1, a) .. (text or "") .. buf:sub(b + 1)
          lines = {}
          local pos = 1
          while true do
            local nl = buf:find("\n", pos, true)
            if not nl then lines[#lines + 1] = buf:sub(pos); break end
            lines[#lines + 1] = buf:sub(pos, nl - 1)
            pos = nl + 1
          end
          return true
        end,
        select = function() end,
        clear_selection = function() end,
      },
      cursor = {
        get = function() return cur.line, cur.col end,
        set = function(l, c) cur.line = l; cur.col = c end,
      },
      theme = { palette = function() return {} end },
      decoration = {
        set = function() deco_id = deco_id + 1; return deco_id end,
        delete = function() end,
      },
      editor = { request_redraw = function() end, info = function() return { word = "" } end },
      viewport = { info = function() return { window = { width = 80, height = 24 } } end },
      workspace = { path = function() return "/tmp" end },
      treesitter = { language_for_extension = function() return nil end },
      ui = { show_message = function() end, picker = function() end },
      lsp = { register_snippet_handler = function(fn) test_snippet_handler = fn end },
    }

    local order = { "config", "doc", "env", "nodes", "parser",
                    "store", "json", "session", "expand", "lsp" }
    for _, name in ipairs(order) do
      package.loaded["jot_snip." .. name] = assert(loadfile(dir .. name .. ".lua"))()
    end
    snip = {
      config = package.loaded["jot_snip.config"],
      doc = package.loaded["jot_snip.doc"],
      env = package.loaded["jot_snip.env"],
      nodes = package.loaded["jot_snip.nodes"],
      parser = package.loaded["jot_snip.parser"],
      store = package.loaded["jot_snip.store"],
      session = package.loaded["jot_snip.session"],
      expand = package.loaded["jot_snip.expand"],
      lsp = package.loaded["jot_snip.lsp"],
    }

    function test_set_text(text)
      lines = {}
      local pos = 1
      while true do
        local nl = text:find("\n", pos, true)
        if not nl then lines[#lines + 1] = text:sub(pos); break end
        lines[#lines + 1] = text:sub(pos, nl - 1)
        pos = nl + 1
      end
      cur.line, cur.col = #lines, #lines[#lines] + 1
    end
    function test_text() return table.concat(lines, "\n") end
    function test_cursor() return cur.line, cur.col end
    function test_expand(body)
      return snip.expand.expand({ snippet = { trig = "", snippet_text = body, name = "test" } })
    end
    function test_jump(dir) return snip.session.jump(dir) end
    function test_choice(dir) return snip.session.change_choice(dir) end
    function test_active() return snip.session.active() end
    function test_index() return snip.session.current_index() end
    function test_values(index) return snip.session.values()[index] end
    function test_lsp_install() snip.lsp.install() end
    function test_lsp_handle(text, sl, sc, el, ec)
      if not test_snippet_handler then return false end
      return test_snippet_handler({
        text = text, start_line = sl, start_col = sc, end_line = el, end_col = ec,
      })
    end

    function test_doc_checks()
      local ok = true
      local function check(c) if not c then ok = false end end
      local t = { "ab", "c" }
      local starts = snip.doc.line_starts(t)
      check(starts[1] == 0 and starts[2] == 3 and starts[3] == 5)
      check(snip.doc.offset(starts, t, 2, 1) == 3)
      check(snip.doc.offset(starts, t, 1, 3) == 2)
      local p = snip.doc.position(starts, t, 4)
      check(p.line == 2 and p.col == 2)
      check(snip.doc.text_between(starts, t, 0, 5) == "ab\nc")
      check(table.concat(snip.doc.splice(starts, t, 2, 3, "X\nY"), "|") == "abX|Yc")
      check(snip.doc.shift(0, 2, 3, 2) == 0)
      check(snip.doc.shift(3, 2, 3, 2) == 4)
      check(snip.doc.shift(5, 2, 3, 2) == 6)
      return ok
    end

    function test_env_checks()
      local ok = true
      local function check(c) if not c then ok = false end end
      local ctx = {
        path = "/a/b/test.lua", line = 7, col = 3, columns = 80, lines = 24,
        line_text = "abc", word = "abc", selected_text = "sel", tab_size = 4,
        snippet_name = "foo",
      }
      check(snip.env.get("TM_FILENAME", ctx) == "test.lua")
      check(snip.env.get("TM_FILENAME_BASE", ctx) == "test")
      check(snip.env.get("TM_DIRECTORY", ctx) == "/a/b")
      check(snip.env.get("TM_LINE_NUMBER", ctx) == "7")
      check(snip.env.get("TM_LINE_INDEX", ctx) == "6")
      check(snip.env.get("TM_COLUMN_NUMBER", ctx) == "3")
      check(snip.env.get("TM_SELECTED_TEXT", ctx) == "sel")
      check(snip.env.get("TM_TAB_SIZE", ctx) == "4")
      check(snip.env.get("LS_SNIPPET_NAME", ctx) == "foo")
      snip.env.extend("MY_VAR", "42")
      check(snip.env.get("MY_VAR", ctx) == "42")
      check(snip.env.resolve("A ${MY_VAR} B $TM_FILENAME", ctx) == "A 42 B test.lua")
      return ok
    end

    function test_store_checks()
      local ok = true
      local function check(c) if not c then ok = false end end
      snip.store.cleanup_all()
      snip.store.add_snippets("lua", { { trig = "ff", snippet_text = "for" } })
      check(#snip.store.get_snippets("lua", false) == 1)
      -- The keyed-map form LuaSnip's snip_env collections use.
      snip.store.add_snippets("lua", { mm = { trig = "mm", snippet_text = "map" } })
      check(#snip.store.get_snippets("lua", false) == 2)
      -- extends adds the parents' snippets to the child filetype.
      snip.store.add_snippets("python", { { trig = "py", snippet_text = "py" } })
      snip.store.filetype_extend("django", { "python" })
      local inherited = snip.store.get_snippets("django", false)
      check(#inherited == 1 and inherited[1].trig == "py")
      -- cleanup drops one filetype.
      snip.store.cleanup("lua")
      check(#snip.store.get_snippets("lua", false) == 0)
      -- autosnippets live in their own bucket.
      snip.store.add_snippets("go", { { trig = "au", snippet_text = "a", snippetType = "autosnippet" } })
      check(#snip.store.get_autosnippets("go") == 1)
      check(#snip.store.get_snippets("go", false) == 0)
      snip.store.cleanup_all()
      return ok
    end

    function test_expand_checks()
      local ok = true
      local function check(c) if not c then ok = false end end
      snip.store.cleanup_all()
      snip.store.add_snippets("lua", { { trig = "fn", snippet_text = "function() $0 end" } })
      check(snip.expand.match("fn", "lua", false) ~= nil)
      check(snip.expand.match(" fn", "lua", false) ~= nil)
      -- Word boundary: a trigger preceded by a word character does not match.
      check(snip.expand.match("afn", "lua", false) == nil)
      -- Priority: the lower number wins even when a longer trigger also matches.
      snip.store.cleanup_all()
      snip.store.add_snippets("lua", {
        { trig = "ab", snippet_text = "ab", priority = 1 },
        { trig = "b", snippet_text = "b", priority = 5 },
      })
      local s = snip.expand.match("ab", "lua", false)
      check(s ~= nil and (s.trig or s.trigger) == "ab")
      snip.store.cleanup_all()
      return ok
    end
  )LUA";

  struct SnipState
  {
    lua_State *L = nullptr;

    SnipState()
    {
      L = luaL_newstate();
      REQUIRE(L != nullptr);
      luaL_openlibs(L);
      const std::string dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/snippet/";
      REQUIRE(luaL_loadstring(L, kPrelude) == LUA_OK);
      lua_pushstring(L, dir.c_str());
      REQUIRE(lua_pcall(L, 1, 0, 0) == LUA_OK);
    }

    ~SnipState()
    {
      if (L)
        lua_close(L);
    }

    SnipState(const SnipState &) = delete;
    SnipState &operator=(const SnipState &) = delete;

    void set_text(const std::string &text)
    {
      lua_getglobal(L, "test_set_text");
      lua_pushlstring(L, text.data(), text.size());
      if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        FAIL("set_text failed: " << lua_tostring(L, -1));
    }

    std::string text()
    {
      lua_getglobal(L, "test_text");
      if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        FAIL("text failed: " << lua_tostring(L, -1));
      const char *value = lua_tostring(L, -1);
      std::string out = value ? value : "";
      lua_pop(L, 1);
      return out;
    }

    bool expand(const std::string &body)
    {
      lua_getglobal(L, "test_expand");
      lua_pushlstring(L, body.data(), body.size());
      if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        FAIL("expand failed: " << lua_tostring(L, -1));
      bool ok = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      return ok;
    }

    void call_void(const char *fn)
    {
      lua_getglobal(L, fn);
      if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        FAIL(fn << " failed: " << lua_tostring(L, -1));
    }

    // Drives the registered LSP snippet handler with an explicit replace range.
    bool lsp_handle(const std::string &text, int sl, int sc, int el, int ec)
    {
      lua_getglobal(L, "test_lsp_handle");
      lua_pushlstring(L, text.data(), text.size());
      lua_pushinteger(L, sl);
      lua_pushinteger(L, sc);
      lua_pushinteger(L, el);
      lua_pushinteger(L, ec);
      if (lua_pcall(L, 5, 1, 0) != LUA_OK)
        FAIL("lsp_handle failed: " << lua_tostring(L, -1));
      bool ok = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      return ok;
    }

    // Calls a zero/one-int-argument boolean helper from the prelude.
    bool call_bool(const char *fn, int arg = 0)
    {
      lua_getglobal(L, fn);
      if (arg != 0)
        lua_pushinteger(L, arg);
      if (lua_pcall(L, arg != 0 ? 1 : 0, 1, 0) != LUA_OK)
        FAIL(fn << " failed: " << lua_tostring(L, -1));
      bool ok = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      return ok;
    }

    int current_index()
    {
      lua_getglobal(L, "test_index");
      if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        FAIL("index failed: " << lua_tostring(L, -1));
      int index = (int)lua_tointeger(L, -1);
      lua_pop(L, 1);
      return index;
    }

    std::string value_at(int index)
    {
      lua_getglobal(L, "test_values");
      lua_pushinteger(L, index);
      if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        FAIL("values failed: " << lua_tostring(L, -1));
      const char *value = lua_tostring(L, -1);
      std::string out = value ? value : "";
      lua_pop(L, 1);
      return out;
    }
  };
} // namespace

TEST_CASE("Snippet parser renders tabstop defaults and mirrors", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("for (${1:int} ${2:i} = ${3:0}; ${2} < ${4:n}; ${2}++) {\n}$0"));
  REQUIRE(state.text() == "for (int i = 0; i < n; i++) {\n}");
}

TEST_CASE("Snippet choices start on the first item and cycle", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("${1|alpha,beta,gamma|}$0"));
  REQUIRE(state.text() == "alpha");
  REQUIRE(state.call_bool("test_choice", 1));
  REQUIRE(state.text() == "beta");
  REQUIRE(state.call_bool("test_choice", 1));
  REQUIRE(state.text() == "gamma");
  // Cycling past the end wraps back to the first item.
  REQUIRE(state.call_bool("test_choice", 1));
  REQUIRE(state.text() == "alpha");
}

TEST_CASE("Snippet variables resolve from the editor context", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("file: $TM_FILENAME line: ${TM_LINE_NUMBER}$0"));
  REQUIRE(state.text() == "file: test.lua line: 1");
}

TEST_CASE("Snippet transforms apply case conversions to a tabstop", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("${1:hello} ${1/(.*)/\\U\\1/}$0"));
  REQUIRE(state.text() == "hello HELLO");
}

TEST_CASE("Snippet session tracks mirrored tabstop values", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("${1:foo} ${1}$0"));
  REQUIRE(state.text() == "foo foo");
  REQUIRE(state.value_at(1) == "foo");
}

TEST_CASE("Snippet session jumps through tabstops and exits cleanly", "[snippet][lua]")
{
  SnipState state;
  state.set_text("");
  REQUIRE(state.expand("${1:aa}${2:bb}$0"));
  REQUIRE(state.text() == "aabb");
  REQUIRE(state.call_bool("test_active"));
  REQUIRE(state.current_index() == 1);

  REQUIRE(state.call_bool("test_jump", 1));
  REQUIRE(state.current_index() == 2);

  // Jumping forward lands on the final stop ($0)...
  REQUIRE(state.call_bool("test_jump", 1));
  REQUIRE(state.current_index() == 0);
  REQUIRE(state.call_bool("test_active"));

  // ...and jumping past it ends the session.
  REQUIRE(state.call_bool("test_jump", 1));
  REQUIRE_FALSE(state.call_bool("test_active"));
}

TEST_CASE("LSP snippet completions expand through the engine", "[snippet][lua][lsp]")
{
  SnipState state;
  state.set_text("foo");
  state.call_void("test_lsp_install");
  // The native path replaces [1:1, 1:4) — the "foo" the completion covers —
  // and the engine renders the raw snippet text with real tabstops.
  REQUIRE(state.lsp_handle("${1:int} ${2:x}", 1, 1, 1, 4));
  REQUIRE(state.text() == "int x");
  REQUIRE(state.call_bool("test_active"));
  REQUIRE(state.current_index() == 1);
}

TEST_CASE("Snippet document offsets round-trip and splice correctly", "[snippet][lua]")
{
  SnipState state;
  REQUIRE(state.call_bool("test_doc_checks"));
}

TEST_CASE("Snippet environment resolves builtins, user and namespaced variables", "[snippet][lua]")
{
  SnipState state;
  REQUIRE(state.call_bool("test_env_checks"));
}

TEST_CASE("Snippet store indexes by filetype, extends and autosnippets", "[snippet][lua]")
{
  SnipState state;
  REQUIRE(state.call_bool("test_store_checks"));
}

TEST_CASE("Snippet expansion respects word boundaries and priorities", "[snippet][lua]")
{
  SnipState state;
  REQUIRE(state.call_bool("test_expand_checks"));
}
