// Headless tests for the bundled AI assistant (runtime/lua/features/ai/*).
//
// The module tree is loaded into a raw Lua state whose jot.* API is stubbed by
// recording functions (the same trick test_lua_markdown.cpp uses), so the parts
// worth pinning here need no Editor, no terminal and no provider: the JSON
// codec the request and the streamed reply both go through, the wire payload
// each adapter kind builds, the answer/error extraction, the `#`/`/` context
// expansion, the inline diff, and the chat transcript's text<->buffer round
// trip. What no stub can prove - the socket, the streaming loop, the diff float
// on a real screen - is test/ai_probe.py's job.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// install_bundled_utf8 + install_bundled_lua_lib: the steps the editor's host
// takes that a raw state here has to take too, so the bundled feature sees the
// dialect it is written to (docs/LUA_API.md, "The scripting dialect") and the
// shared library it requires at load time (runtime/lua/lib/json.lua).
#include "lua/api/api_internal.h"

namespace
{
  // The `jot` stub, the module loading and a few knobs the Lua chunks reach for
  // (`store` for config, `buffer`/`files` for what the buffer and the disk
  // hold, `rec` for what the module asked the editor to do).
  const char *kPrelude = R"LUA(
    local dir = ...
    store = {}
    files = {}
    rec = { messages = {}, commands = {}, keymaps = {}, jobs = {}, edits = {} }
    buffer = {
      index = 1,
      path = "src/a.lua",
      language = "lua",
      lines = { "local x = 1" },
      text = "local x = 1\n",
      selection = { active = false },
      diagnostics = {},
    }
    jot = {
      config = {
        get = function(k, d) local v = store[k]; if v == nil then return d end; return v end,
        get_bool = function(k, d) local v = store[k]; if v == nil then return d end; return v == true end,
        get_number = function(k, d) local v = store[k]; if v == nil then return d end; return tonumber(v) end,
        set = function(k, v) store[k] = v end,
      },
      buffer = {
        current = function() return buffer.index end,
        list = function() return { { index = buffer.index, name = buffer.path } } end,
        lines = function() return buffer.lines end,
        text = function() return buffer.text end,
        meta = function() return { path = buffer.path, name = buffer.path } end,
        selection = function() return buffer.selection end,
        get_var = function() return nil end,
        set_var = function() end,
        apply_edit = function(a, b, c, d, text) rec.edits[#rec.edits + 1] = text end,
      },
      workspace = { path = function() return "/ws" end },
      file = { read = function(path) return files[path] end },
      diagnostics = { get = function() return buffer.diagnostics end },
      cursor = { get = function() return 1, 1 end, set = function() end },
      ui = {
        show_message = function(m) rec.messages[#rec.messages + 1] = m end,
        picker = function(title, items, on_select) rec.picker = items end,
        popup = function(text, title) rec.popup = text end,
        float = {
          open = function(buf, config) rec.float = config; return 7 end,
          close = function(win) rec.closed = win; return true end,
          set_lines = function(win, lines) rec.float_lines = lines end,
          set_spans = function(win, row, spans) rec.spans = rec.spans or {}; rec.spans[row] = spans end,
          on_key = function(win, fn) rec.on_key = fn; return true end,
          on_mouse = function(win, fn) rec.on_mouse = fn; return true end,
        },
        buffer = { create = function() return 11 end, delete = function() return true end },
      },
      viewport = { info = function() return { window = { width = 120, height = 40 } } end },
      timer = {
        set_interval = function(ms, fn) return 1 end,
        set_timeout = function(ms, fn) return 2 end,
        clear = function() end,
      },
      job = {
        capture = function(cmd, cwd, cb) rec.jobs[#rec.jobs + 1] = { command = cmd, cwd = cwd }; return true end,
      },
      keymap = {
        set = function(chord, fn, detail) rec.keymaps[#rec.keymaps + 1] = { chord = chord, detail = detail } end,
        remove = function() end,
      },
      command = function(name, fn, detail) rec.commands[name] = detail or "" end,
      theme = {
        palette = function()
          return {
            default = { fg = 7, bg = 0 },
            comment = { fg = 8 },
            string = { fg = 2 },
            status_error = { fg = 1 },
            panel_border = { fg = 4 },
          }
        end,
      },
    }
    for _, name in ipairs({ "json", "config", "http", "context", "inline" }) do
      package.loaded["jot_ai." .. name] = assert(loadfile(dir .. name .. ".lua"))()
    end
    -- chat.lua requires context and the others, so it loads after them; init.lua
    -- registers the commands and keymaps and is loaded last, like boot does.
    package.loaded["jot_ai.chat"] = assert(loadfile(dir .. "chat.lua"))()
    local init = assert(loadfile(dir .. "init.lua"))()

    -- String-in/string-out adapters. A test about newlines should hand its
    -- fixture in as an argument: written into a Lua literal it needs the
    -- newline escaped through the C++ source, the shell and the editor's own
    -- quoting before Lua ever sees it.
    ai = {
      json = package.loaded["jot_ai.json"],
      config = package.loaded["jot_ai.config"],
      http = package.loaded["jot_ai.http"],
      context = package.loaded["jot_ai.context"],
      inline = package.loaded["jot_ai.inline"],
      chat = package.loaded["jot_ai.chat"],
      init = init,
      store = store,
      buffer = buffer,
      files = files,
      rec = rec,
    }
    probe = {
      diff_rows = function(before, after)
        local out = {}
        local rows = ai.inline.diff(before, after)
        for _, row in ipairs(rows) do
          out[#out + 1] = row.kind .. ":" .. row.text
        end
        return table.concat(out, "|") .. (rows.cut and "|cut" or "")
      end,
      strip_fence = ai.inline.strip_fence,
      set_file = function(path, text) files[path] = text; return "ok" end,
      expand = function(text)
        local prompt, notes = ai.context.expand(text)
        return prompt .. "\n---\n" .. table.concat(notes, "\n")
      end,
      render = function(user, assistant, status, prompt)
        local messages = {}
        if user ~= "" then
          messages[#messages + 1] = { role = "user", content = user, display = user }
        end
        if assistant ~= "" then
          messages[#messages + 1] = { role = "assistant", content = assistant }
        end
        if prompt == "__answering__" then
          prompt = false
        end
        return table.concat(ai.chat.render_lines(messages, status, prompt), "\n")
      end,
      prompt_from = function(buffer_text)
        local lines = {}
        for line in (buffer_text .. "\n"):gmatch("(.-)\n") do
          lines[#lines + 1] = line
        end
        return ai.chat.prompt_from_lines(lines)
      end,
    }
  )LUA";

  struct AiState
  {
    lua_State *L = nullptr;

    AiState()
    {
      L = luaL_newstate();
      REQUIRE(L != nullptr);
      luaL_openlibs(L);
      // The dialect the bundled ai feature is written to (docs/LUA_API.md): the
      // host installs the bundled utf8 and the shared library (runtime/lua/lib)
      // before any bundled file runs, and this state runs the same files, so it
      // gets the same steps.
      jot_lua::install_bundled_utf8(L);
      jot_lua::install_bundled_lua_lib(L);
      const std::string dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/ai/";
      REQUIRE(luaL_loadstring(L, kPrelude) == LUA_OK);
      lua_pushstring(L, dir.c_str());
      if (lua_pcall(L, 1, 0, 0) != LUA_OK)
      {
        const char *error = lua_tostring(L, -1);
        FAIL("AI prelude failed: " << (error ? error : "unknown"));
      }
    }

    ~AiState()
    {
      if (L)
      {
        lua_close(L);
      }
    }

    AiState(const AiState &) = delete;
    AiState &operator=(const AiState &) = delete;

    // Runs a chunk and hands back its first return value as a string. The
    // result count is fixed at one, so a chunk that returns more (a byte pair,
    // say) cannot leave a value behind for the next chunk to trip over, and a
    // chunk that returns nothing still leaves exactly one slot to pop.
    std::string run(const std::string &code) const
    {
      if (luaL_loadstring(L, code.c_str()) != LUA_OK)
      {
        const char *error = lua_tostring(L, -1);
        const std::string message = error ? error : "unknown";
        lua_pop(L, 1);
        FAIL("Lua chunk did not compile: " << message << "\n" << code);
      }
      if (lua_pcall(L, 0, 1, 0) != LUA_OK)
      {
        const char *error = lua_tostring(L, -1);
        const std::string message = error ? error : "unknown";
        lua_pop(L, 1);
        FAIL("Lua chunk failed: " << message << "\n" << code);
      }
      std::string out;
      if (lua_isstring(L, -1))
      {
        out = lua_tostring(L, -1);
      }
      lua_pop(L, 1);
      return out;
    }

    // Runs a chunk for its side effects only.
    void exec(const std::string &code) const
    {
      (void)run(code);
    }

    // Calls probe.<name> with the given string arguments and returns its first
    // result as a string.
    std::string probe(const char *name, const std::vector<std::string> &args) const
    {
      lua_getglobal(L, "probe");
      lua_getfield(L, -1, name);
      lua_remove(L, -2);
      for (const std::string &arg : args)
      {
        lua_pushlstring(L, arg.data(), arg.size());
      }
      if (lua_pcall(L, static_cast<int>(args.size()), 1, 0) != LUA_OK)
      {
        const char *error = lua_tostring(L, -1);
        const std::string message = error ? error : "unknown";
        lua_settop(L, 0);
        FAIL("probe." << name << " failed: " << message);
      }
      std::string out;
      if (lua_isstring(L, -1))
      {
        out = lua_tostring(L, -1);
      }
      lua_pop(L, 1);
      return out;
    }
  };

  bool has(const std::string &haystack, const std::string &needle)
  {
    return haystack.find(needle) != std::string::npos;
  }
} // namespace

TEST_CASE("AI JSON encodes false, arrays and objects as they are", "[ai][lua]")
{
  AiState state;

  // The one that mattered: `body.x or fallback` sent `stream = false` to a
  // provider as null, which is not the same request.
  REQUIRE(state.run("return ai.json.encode({ stream = false })") == R"({"stream":false})");
  REQUIRE(state.run("return ai.json.encode({ stream = true })") == R"({"stream":true})");
  REQUIRE(state.run("return ai.json.encode({ on = false, off = false })")
          == R"({"off":false,"on":false})");

  // A Lua table is both list and map: a `1` key makes it an array, and an empty
  // table is an object (JSON arrays are dense).
  REQUIRE(state.run("return ai.json.encode({ 1, 2, 3 })") == "[1,2,3]");
  REQUIRE(state.run("return ai.json.encode({})") == "{}");
  REQUIRE(state.run("return ai.json.encode({ { role = 'user', content = 'hi' } })")
          == R"([{"content":"hi","role":"user"}])");

  // Nesting, numbers and the non-finite values JSON has no spelling for.
  REQUIRE(state.run("return ai.json.encode({ a = { b = { c = false } } })")
          == R"({"a":{"b":{"c":false}}})");
  REQUIRE(state.run("return ai.json.encode({ t = 0.2, n = 7, z = 0 })")
          == R"({"n":7,"t":0.2,"z":0})");
  REQUIRE(state.run("return ai.json.encode({ v = 0 / 0, i = 1 / 0 })")
          == R"({"i":null,"v":null})");

  // A body carries code, so the escapes have to survive a round trip.
  REQUIRE(state.run("return ai.json.encode({ s = 'a\"b\\\\c\\nd\\te' })")
          == R"({"s":"a\"b\\c\nd\te"})");
  REQUIRE(state.run("return ai.json.encode({ s = '\\1' })") == R"({"s":"\u0001"})");
}

TEST_CASE("AI JSON is the runtime's JSON, not a second copy", "[ai][lua]")
{
  AiState state;

  // features/ai/json.lua used to be its own encoder and decoder; it is now a
  // require of jot.lib.json, so a fix to the shared one lands here too.
  REQUIRE(state.run("return tostring(package.loaded['jot_ai.json'] == require('jot.lib.json'))")
          == "true");
  // Which means the shared decoder's rules hold here, including the one that a
  // document is the whole input.
  REQUIRE(state.run("local v, e = ai.json.decode('{\"a\":1} 2') "
                    "return v == nil and tostring(e):find('invalid', 1, true) and 'invalid' or 'no'")
          == "invalid");
}

TEST_CASE("AI JSON decodes provider frames", "[ai][lua]")
{
  AiState state;

  REQUIRE(state.run("local v = ai.json.decode('{\"a\":[1,true,null,\"x\"]}') "
                    "return tostring(v.a[1]) .. tostring(v.a[2]) .. tostring(v.a[3]) .. v.a[4]")
          == "1truenilx");

  // `\u` escapes, including the surrogate pair an answer about emoji arrives as.
  REQUIRE(state.run("return ai.json.decode('\"A\\\\u0041\"')") == "AA");
  REQUIRE(state.run("return ('%d,%d'):format(ai.json.decode('\"\\\\u00e9\"'):byte(1, 2))") == "195,169");
  REQUIRE(state.run("return ai.json.decode('\"\\\\ud83d\\\\ude00\"')") == "\xf0\x9f\x98\x80");

  // Whitespace and nesting, which is what a streamed frame looks like.
  REQUIRE(state.run("local v = ai.json.decode('  {\\n  \"choices\" : [ { \"delta\" : "
                    "{ \"content\" : \"hi\" } } ] } ' ) return v.choices[1].delta.content")
          == "hi");

  // A body that is not JSON is reported, not answered with a half table.
  REQUIRE(state.run("local v, e = ai.json.decode('{oops') "
                    "return v == nil and type(e) == 'string' and 'error' or 'accepted'")
          == "error");
}

TEST_CASE("AI wire payloads match what each adapter kind takes", "[ai][lua]")
{
  AiState state;

  // OpenAI: the system prompt leads the messages array, and max_tokens is left
  // out unless the user asked for a limit.
  REQUIRE(state.run(
              "local a = { kind = 'openai', model = 'm', system = 'S', temperature = 0.2, "
              "stream = false, max_tokens = 0 } "
              "return ai.json.encode(ai.http.payload(a, { { role = 'user', content = 'q' } }))")
          == R"({"messages":[{"content":"S","role":"system"},{"content":"q","role":"user"}],)"
             R"("model":"m","stream":false,"temperature":0.2})");
  REQUIRE(state.run(
              "local a = { kind = 'openai', model = 'm', system = '', temperature = 0.2, "
              "stream = true, max_tokens = 99 } "
              "return ai.json.encode(ai.http.payload(a, { { role = 'user', content = 'q' } }))")
          == R"({"max_tokens":99,"messages":[{"content":"q","role":"user"}],"model":"m",)"
             R"("stream":true,"temperature":0.2})");

  // Anthropic carries the system prompt beside the array and refuses a request
  // without max_tokens, so an unset limit becomes its 4096.
  REQUIRE(state.run(
              "local a = { kind = 'anthropic', model = 'c', system = 'S', temperature = 1, "
              "stream = false, max_tokens = 0 } "
              "return ai.json.encode(ai.http.payload(a, { { role = 'user', content = 'q' } }))")
          == R"({"max_tokens":4096,"messages":[{"content":"q","role":"user"}],"model":"c",)"
             R"("stream":false,"system":"S","temperature":1})");
  REQUIRE(state.run(
              "local a = { kind = 'anthropic', model = 'c', system = '', temperature = 1, "
              "stream = false, max_tokens = 10 } "
              "return ai.json.encode(ai.http.payload(a, { { role = 'user', content = 'q' } }))")
          == R"({"max_tokens":10,"messages":[{"content":"q","role":"user"}],"model":"c",)"
             R"("stream":false,"temperature":1})");

  // A CLI takes text, not a body, and gets the whole conversation as one prompt.
  REQUIRE(state.run("local a = { kind = 'cmd', system = 'S' } "
                    "return ai.http.payload(a, { { role = 'user', content = 'q' } }) == nil "
                    "and 'none' or 'body'")
          == "none");
  REQUIRE(state.run("local a = { kind = 'cmd', system = 'S' } "
                    "return ai.http.prompt_text(a, { { role = 'user', content = 'q' }, "
                    "{ role = 'assistant', content = 'a' } })")
          == "S\n\nUser:\nq\n\nAssistant:\na\n");

  // An empty message is dropped rather than sent as a blank turn.
  REQUIRE(state.run("return #ai.http.wire_messages({ { role = 'user', content = '' }, "
                    "{ role = 'user', content = 'q' } })")
          == "1");
}

TEST_CASE("AI URLs, quoting and answer extraction", "[ai][lua]")
{
  AiState state;

  REQUIRE(state.run("return ai.http.url({ kind = 'openai', base = 'http://h/v1' })")
          == "http://h/v1/chat/completions");
  REQUIRE(state.run("return ai.http.url({ kind = 'openai', base = 'http://h/v1/' })")
          == "http://h/v1/chat/completions");
  REQUIRE(state.run("return ai.http.url({ kind = 'anthropic', base = 'http://h/v1' })")
          == "http://h/v1/messages");
  // A base the user pasted with the path already on it is left alone.
  REQUIRE(state.run("return ai.http.url({ kind = 'openai', base = 'http://h/v1/chat/completions' })")
          == "http://h/v1/chat/completions");

  // A path with a quote in it still reaches the shell as one argument.
  REQUIRE(state.run("return ai.http.shell_quote(\"/tmp/it's here.req\")") == "'/tmp/it'\\''s here.req'");

  REQUIRE(state.run("return ai.http.text_of('openai', "
                    "{ choices = { { delta = { content = 'hi' } } } })")
          == "hi");
  REQUIRE(state.run("return ai.http.text_of('openai', "
                    "{ choices = { { message = { content = 'whole' } } } })")
          == "whole");
  REQUIRE(state.run("return ai.http.text_of('anthropic', { delta = { text = 'hi' } })") == "hi");
  REQUIRE(state.run("return ai.http.text_of('anthropic', "
                    "{ content = { { type = 'text', text = 'a' }, { type = 'text', text = 'b' } } })")
          == "ab");
  // A frame that carries no text (a role-only delta) is not an empty answer.
  REQUIRE(state.run("return tostring(ai.http.text_of('openai', "
                    "{ choices = { { delta = { role = 'assistant' } } } }))")
          == "nil");

  REQUIRE(state.run("return ai.http.error_of({ error = { message = 'bad key' } })") == "bad key");
  REQUIRE(state.run("return ai.http.error_of({ error = 'nope' })") == "nope");
  REQUIRE(state.run("return tostring(ai.http.error_of({ ok = true }))") == "nil");
}

TEST_CASE("AI adapters resolve from config and the environment", "[ai][lua]")
{
  AiState state;
  ::setenv("JOT_TEST_AI_KEY", "tok-1", 0);

  // Nothing configured: the entry's own model and endpoint, and the token read
  // from the variable the entry names.
  REQUIRE(state.run("ai.store.ai_key_env = 'JOT_TEST_AI_KEY' "
                    "local a = ai.config.resolve() "
                    "return a.name .. '|' .. a.kind .. '|' .. a.model .. '|' .. a.key")
          == "openai|openai|gpt-4o-mini|tok-1");

  // A named entry with its own endpoint, and a per-chat model override. An
  // entry that names no key variable needs no token: the local runtimes
  // (Ollama, llama.cpp, LM Studio) are the reason `kind` is a wire protocol.
  REQUIRE(state.run("ai.store.ai_adapter = 'ollama'; ai.store.ai_key_env = '' "
                    "local a = ai.config.resolve({ model = 'llama-x' }) "
                    "return a.name .. '|' .. a.model .. '|' .. a.base .. '|' .. tostring(a.key == '')")
          == "ollama|llama-x|http://localhost:11434/v1|true");

  // An emptied row falls back to the adapter's own value rather than sending
  // an empty model or endpoint.
  REQUIRE(state.run("ai.store.ai_adapter = 'openai'; ai.store.ai_model = '' "
                    "ai.store.ai_base_url = ''; ai.store.ai_key_env = 'JOT_TEST_AI_KEY' "
                    "local a = ai.config.resolve() return a.model .. '|' .. a.base")
          == "gpt-4o-mini|https://api.openai.com/v1");

  // Numbers and booleans come back with their type, and 0 means "no limit" once
  // the payload builder has seen it.
  REQUIRE(state.run("ai.store.ai_temperature = '0.9'; ai.store.ai_max_tokens = '256'; "
                    "ai.store.ai_stream = 'false' "
                    "local a = ai.config.resolve() "
                    "return tostring(a.temperature) .. '|' .. tostring(a.max_tokens) .. '|' .. "
                    "tostring(a.stream)")
          == "0.9|256|false");

  // The reasons a request cannot start, which the UI shows instead of failing
  // at the socket.
  REQUIRE(state.run("ai.store.ai_key_env = 'JOT_TEST_AI_MISSING' "
                    "return ai.config.blocked(ai.config.resolve())")
          == "missing JOT_TEST_AI_MISSING");
  REQUIRE(state.run("ai.store.ai_key_env = 'JOT_TEST_AI_MISSING' "
                    "return ai.config.describe(ai.config.resolve())")
          == "openai  gpt-4o-mini  https://api.openai.com/v1  (no JOT_TEST_AI_MISSING)");
  // A CLI's command comes from the entry unless the user replaced it, and a
  // command is what makes the adapter usable - there is no token to want.
  REQUIRE(state.run("ai.store.ai_adapter = 'cli'; ai.store.ai_command = 'mycli --print' "
                    "local a = ai.config.resolve() "
                    "return a.command .. '|' .. tostring(ai.config.blocked(a))")
          == "mycli --print|nil");
  REQUIRE(state.run("ai.store.ai_adapter = 'openai'; ai.store.ai_key_env = 'JOT_TEST_AI_KEY' "
                    "return ai.config.describe(ai.config.resolve())")
          == "openai  gpt-4o-mini  https://api.openai.com/v1");
}

TEST_CASE("AI context references attach the code they name", "[ai][lua]")
{
  AiState state;
  state.exec("ai.buffer.lines = { 'local x = 1', 'local y = 2' } "
             "ai.buffer.text = 'local x = 1\\nlocal y = 2\\n' "
             "ai.files['/ws/src/b.lua'] = 'return 1\\n'");

  const std::string expanded = state.probe("expand", {"#buffer fix this"});
  REQUIRE(has(expanded, "(src/a.lua) fix this"));      // the sentence stays readable
  REQUIRE(has(expanded, "--- src/a.lua ---"));         // and the code travels with it
  // The fence hugs the code: the file's own trailing newline does not become a
  // blank line inside the block.
  REQUIRE(has(expanded, "```lua\nlocal x = 1\nlocal y = 2\n```"));
  REQUIRE(has(expanded, "src/a.lua: 2 lines"));

  // A context command on its own line is dropped from the prompt and its file
  // is attached, which is how a path gets in without breaking the sentence.
  const std::string file_prompt =
      state.probe("expand", {"/file src/b.lua\nwhy is this here"});
  REQUIRE(has(file_prompt, "why is this here"));
  REQUIRE_FALSE(has(file_prompt, "/file"));
  REQUIRE(has(file_prompt, "--- src/b.lua ---\n```lua\nreturn 1\n```"));

  // A word this feature does not know is a word, not a missing reference:
  // `#include` in a pasted line must survive as typed.
  REQUIRE(state.run("local p = ai.context.expand('#include <vector>') return p")
          == "#include <vector>");
  REQUIRE(state.run("local p = ai.context.expand('/notacommand me') return p") == "/notacommand me");

  // A reference with nothing behind it is reported in the notes and left as
  // typed, so the send still happens with the question intact.
  state.exec("ai.buffer.index = 0");
  REQUIRE(has(state.probe("expand", {"#buffer hello"}),
              "#buffer hello\n---\nnothing to read for #buffer"));
  REQUIRE(state.run("return tostring(ai.buffer.index)") == "0");
  state.exec("ai.buffer.index = 1");

  // A huge file is capped, and the prompt says how much was dropped.
  std::string big;
  for (int i = 0; i < 500; i++)
  {
    big += "line\n";
  }
  state.probe("set_file", {"/ws/big.txt", big});
  const std::string capped = state.probe("expand", {"#file:big.txt go"});
  REQUIRE(has(capped, "first 400 of 500 lines"));

  // A file that does not exist is reported and the reference is left as typed.
  REQUIRE(has(state.probe("expand", {"#file:src/nope.lua go"}), "cannot read src/nope.lua"));
}

TEST_CASE("AI inline diff and fence stripping", "[ai][lua]")
{
  AiState state;

  // A changed line reads as a diff: the old one removed, the new one added.
  REQUIRE(state.probe("diff_rows", {"a\nb\nc\n", "a\nB\nc\n"}) == "same:a|del:b|add:B|same:c");
  REQUIRE(state.probe("diff_rows", {"a\n", "a\nb\n"}) == "same:a|add:b");
  REQUIRE(state.probe("diff_rows", {"a\nb\n", "a\n"}) == "same:a|del:b");

  // A trailing newline ends the last line instead of diffing as an added blank
  // one - which is how a model's answer usually ends.
  REQUIRE(state.probe("diff_rows", {"int a = 1;\n", "int a = 1;\n"}) == "same:int a = 1;");
  REQUIRE(state.probe("diff_rows", {"a\n", "a\n"}) == "same:a");

  // Past the LCS budget the diff is a plain before/after listing, flagged so
  // the preview can say why it is not aligned.
  std::string big;
  std::string other;
  for (int i = 0; i < 300; i++)
  {
    big += "x\n";
    other += "y\n";
  }
  REQUIRE(state.probe("diff_rows", {big, other}).substr(0, 3) == "del");
  REQUIRE(has(state.probe("diff_rows", {big, other}), "|cut"));

  // The code a model returns is often fenced anyway; the fence is not code.
  REQUIRE(state.probe("strip_fence", {"```cpp\nint a = 1;\n```"}) == "int a = 1;");
  REQUIRE(state.probe("strip_fence", {"  int a = 1;  "}) == "int a = 1;");
  REQUIRE(state.probe("strip_fence", {"```\nplain\n```"}) == "plain");
  REQUIRE(state.probe("strip_fence", {"no fence at all"}) == "no fence at all");
}

TEST_CASE("AI chat transcript renders and reads back", "[ai][lua]")
{
  AiState state;

  // The buffer the user types into: header, the turns, then the prompt region.
  const std::string rendered = state.probe("render", {"q", "a1\na2", "ready", "typed"});
  REQUIRE(has(rendered, "# Chat · openai · gpt-4o-mini · ready"));
  REQUIRE(has(rendered, "## user\n> q\n"));
  REQUIRE(has(rendered, "\n## assistant\na1\na2\n"));
  REQUIRE(has(rendered, "## user\n> typed"));
  // A streamed answer often ends with a newline; it must not land a blank line
  // of its own in the transcript.
  REQUIRE(state.probe("render", {"q", "a1\n", "ready", ""})
          == state.probe("render", {"q", "a1", "ready", ""}));

  // A prompt several lines long, under the marker of the last user turn.
  REQUIRE(state.probe("prompt_from", {"# Chat\n## user\n> first\nsecond\n## assistant\nanswer\n"
                                       "## user\n> next\nmore"})
          == "next\nmore");

  // Once sent, the render has no prompt region: the reply needs somewhere to
  // stream into instead.
  const std::string answering = state.probe("render", {"q", "", "answering…", "__answering__"});
  REQUIRE_FALSE(has(answering, "## user\n>\n"));
  REQUIRE(answering.rfind("## assistant") != std::string::npos);

  // Hand-edited headers do not silently swallow the prompt.
  REQUIRE(state.probe("prompt_from", {"## user was deleted\n> still mine"}) == "still mine");
}

TEST_CASE("AI commands and the keymap family are registered", "[ai][lua]")
{
  AiState state;

  for (const char *name : {"CodeCompanionChat", "CodeCompanionNewChat", "CodeCompanionSend",
                           "CodeCompanionStop", "CodeCompanionActions", "CodeCompanionStatus",
                           "CodeCompanionPrompt", "CodeCompanion", "Cc"})
  {
    REQUIRE(state.run(std::string("return ai.rec.commands['") + name + "'] and 'yes' or 'no'") == "yes");
  }

  // The Alt+Shift+A family, with the family key itself bound so the held-key
  // helper can list it.
  REQUIRE(state.run("local out = {} for _, k in ipairs(ai.rec.keymaps) do "
                    "out[#out + 1] = k.chord end return table.concat(out, '|')")
          == "Alt+Shift+A|Alt+Shift+A C|Alt+Shift+A N|Alt+Shift+A S|Alt+Shift+A X|Alt+Shift+A A|"
             "Alt+Shift+A P|Alt+Shift+A I|Alt+Shift+A H");

  // The Lua surface other plugins and the user config reach for.
  REQUIRE(state.run("return type(jot.ai.chat.send) .. type(jot.ai.inline) .. type(jot.ai.adapters)")
          == "functionfunctionfunction");
  REQUIRE(state.run("return jot.ai.active().kind") == "openai");
}

TEST_CASE("AI inline runs the request it was asked for", "[ai][lua]")
{
  AiState state;
  ::setenv("JOT_TEST_AI_KEY", "tok-2", 1);
  state.exec("ai.store.ai_key_env = 'JOT_TEST_AI_KEY' "
             "ai.buffer.selection = { active = true, start_line = 1, end_line = 1, "
             "start_col = 1, end_col = 12 }");

  REQUIRE(state.run("return tostring(ai.inline.run('make it better'))") == "true");

  // The request is a curl for the selected code, aimed at the configured
  // adapter: the job binding is only recorded here, so nothing leaves the
  // process, but the command line it builds is the one a real run executes.
  REQUIRE(state.run("local c = ai.rec.jobs[#ai.rec.jobs].command "
                    "return (c:find('https://api.openai.com/v1/chat/completions', 1, true) "
                    "and 'url' or 'other') .. '|' "
                    ".. (c:find(\"Authorization: Bearer tok-2\", 1, true) and 'auth' or 'no-auth')")
          == "url|auth");

  // Non-streaming: the diff needs the whole replacement before it can be
  // computed or shown. The body is the scratch file curl would post.
  REQUIRE(state.run("local body = io.open((ai.rec.jobs[#ai.rec.jobs].command:match(\"@'([^']+)'\")), "
                    "'rb') "
                    "local text = body:read('a') body:close() "
                    "return (text:find('\"stream\":false', 1, true) and 'nostream' or 'stream') "
                    ".. '|' .. (text:find('local x = 1', 1, true) and 'selection' or 'missing')")
          == "nostream|selection");
  state.exec("os.remove(ai.rec.jobs[#ai.rec.jobs].command:match(\"@'([^']+)'\"))");

  // A prompt about a buffer with nothing in it says so instead of sending an
  // empty rewrite.
  state.exec("ai.buffer.selection = { active = false } ai.buffer.lines = { '' } "
             "ai.buffer.path = 'untitled'");
  REQUIRE(state.run("return tostring(ai.inline.run('go'))") == "false");
  REQUIRE(has(state.run("return ai.rec.messages[#ai.rec.messages]"), "nothing to rewrite"));

  // And a missing prompt is a question, not a request.
  REQUIRE(state.run("return tostring(ai.inline.run(''))") == "false");
  REQUIRE(has(state.run("return ai.rec.messages[#ai.rec.messages]"), "what should the code become"));
}

TEST_CASE("Bundled AI runtime is not loaded while disabled", "[ai]")
{
  // The assistant ships disabled for now. api_bindings.cpp holds the only call
  // to load_ai_runtime, and uncommenting it is what brings the feature back, so
  // this pins the disable against an accidental re-enable and fails at the
  // moment someone does restore it (retire this case with that commit).
  std::ifstream in(std::string(JOT_SOURCE_DIR) + "/src/lua/api/api_bindings.cpp");
  REQUIRE(in.good());
  std::string line;
  bool loaded = false;
  while (std::getline(in, line))
  {
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 2, "//") == 0)
    {
      continue;
    }
    if (line.find("load_ai_runtime(L);") != std::string::npos)
    {
      loaded = true;
    }
  }
  REQUIRE_FALSE(loaded);
}
