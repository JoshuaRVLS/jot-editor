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
  const char *kPrelude = R"LUA(
    local dir = ...
    store, files, calls = {}, {}, {}
    workspace_path = "/tmp/jot-lc-workspace"
    float_lines, float_callbacks, float_spans = {}, {}, nil
    jot = {
      config = {
        get = function(k, d) local v=store[k]; if v==nil then return d end return v end,
        get_number = function(k, d) local v=store[k]; if v==nil then return d end return tonumber(v) end,
        set = function(k,v) store[k]=v end,
        has = function(k) return store[k] ~= nil end,
      },
      workspace = {path = function() return workspace_path end},
      file = {
        read = function(path) return files[path] end,
        write = function(path,text) files[path]=text; return true end,
        list = function() return {} end,
        remove = function() return true end,
        open = function(path) calls.open=path end,
      },
      leetcode = {
        credential_get = function() return {ok=true, available=true, value="LEETCODE_SESSION=fake; csrftoken=csrf"} end,
        credential_set = function(value) calls.cookie=value; return {ok=true, available=true} end,
        credential_delete = function() return {ok=true, available=true} end,
        request = function(req, cb) calls.request=req; calls.callback=cb; return true end,
      },
      ui = {
        show_message=function(m) calls.message=m; calls.messages=calls.messages or {}; calls.messages[#calls.messages+1]=m end,
        popup=function(text,title) calls.popup={text=text,title=title} end,
        picker=function(title,items,cb) calls.picker={title=title,items=items,callback=cb} end,
        -- The LeetCode dock panel: register_panel/panel/request_redraw are the
        -- three calls the feature makes against the dock.
        register_panel=function(name,fn,title) calls.panel={name=name,fn=fn,title=title} end,
        panel=function(name) calls.panel_shown=name end,
        buffer={create=function() return 1 end, set_lines=function(_,_,_,_,lines) float_lines=lines end},
        float={open=function(buf, config)
                 -- Mirror the real binding's luaL_checktype: the float options
                 -- are argument #2 and the Neovim (buffer, enter, config)
                 -- shape must keep failing here instead of only in the editor.
                 if type(config) ~= "table" then error("jot.ui.float.open expects (buffer, config)") end
                 calls.float_open={buffer=buf, config=config}
                 return 1
               end,
               is_valid=function() return false end,
               configure=function(_,config) calls.float_configure=config end,
               on_key=function(_,fn) float_callbacks.key=fn end,
               on_paste=function(_,fn) float_callbacks.paste=fn end,
               set_lines=function(_,lines) float_lines=lines end,
               set_spans=function(_,line,spans) float_spans={line=line, spans=spans} end,
               close=function() end},
      },
      keymap={set=function(key,fn,detail) calls.keys=calls.keys or {}; calls.keys[#calls.keys+1]=key end,
              remove=function() end},
      status={register=function(name, spec) calls.status={name=name, spec=spec} end,
              unregister=function() end},
      clipboard={set=function(text) calls.clipboard=text end},
      theme={palette=function() return {} end},
      command=function(name,fn,detail) calls.commands=calls.commands or {}; calls.commands[name]=fn end,
      editor={request_redraw=function() calls.redraw=(calls.redraw or 0)+1 end},
      timer={set_timeout=function(ms,fn) calls.timer=fn; return 1 end, clear=function() end},
      shell_quote=function(value) return "'"..value.."'" end,
      viewport={info=function() return {window={width=120,height=40},
                                       right_panel={visible=true, width=42}} end},
    }
    package.loaded["jot_lc.json"] = assert(loadfile(dir .. "json.lua"))()
    package.loaded["jot_lc.config"] = assert(loadfile(dir .. "config.lua"))()
    package.loaded["jot_lc.cache"] = assert(loadfile(dir .. "cache.lua"))()
    package.loaded["jot_lc.client"] = assert(loadfile(dir .. "client.lua"))()
    package.loaded["jot_lc.solution"] = assert(loadfile(dir .. "solution.lua"))()
    package.loaded["jot_lc.panel"] = assert(loadfile(dir .. "panel.lua"))()
    package.loaded["jot_lc.ui"] = assert(loadfile(dir .. "ui.lua"))()
    package.loaded["jot_lc.init"] = assert(loadfile(dir .. "init.lua"))()
    feature = package.loaded["jot_lc.init"]
  )LUA";

  struct State
  {
    lua_State *L = luaL_newstate();
    State()
    {
      REQUIRE(L != nullptr);
      luaL_openlibs(L);
      const std::string dir = std::string(JOT_LUA_SOURCE_DIR) + "/features/leetcode/";
      REQUIRE(luaL_loadstring(L, kPrelude) == LUA_OK);
      lua_pushlstring(L, dir.data(), dir.size());
      if (lua_pcall(L, 1, 0, 0) != LUA_OK)
      {
        const std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
        FAIL("LeetCode Lua prelude failed: " << error);
      }
    }
    ~State() { lua_close(L); }
    std::string run(const std::string &source)
    {
      REQUIRE(luaL_loadstring(L, source.c_str()) == LUA_OK);
      if (lua_pcall(L, 0, 1, 0) != LUA_OK)
      {
        const std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
        FAIL("LeetCode Lua chunk failed: " << error);
      }
      std::string value = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
      lua_pop(L, 1);
      return value;
    }
  };
}

TEST_CASE("LeetCode JSON parser decodes data without evaluating it", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run("local v=package.loaded['jot_lc.json'].decode('{\\\"data\\\":{\\\"ok\\\":true}}'); return tostring(v.data.ok)") == "true");
  REQUIRE(state.run("local v,e=package.loaded['jot_lc.json'].decode('{oops'); return tostring(v)..'|'..tostring(e)").find("invalid") != std::string::npos);
  REQUIRE(state.run("local v=package.loaded['jot_lc.json'].decode('{\\\"x\\\":1} os.execute(\\\"touch /tmp/jot-lc-json-executed\\\")'); return tostring(v)") == "nil");
}

TEST_CASE("LeetCode runtime registers a complete command family", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run("return type(calls.commands.Leet)..'|'..type(calls.commands.LeetRun)..'|'..type(calls.commands.LeetSubmit)") == "function|function|function");
  REQUIRE(state.run("return "
                    "type(calls.commands.LeetConsole)..'|'..type(calls.commands.LeetTabs)..'|'.."
                    "type(calls.commands.LeetYank)..'|'..type(calls.commands.LeetInfo)")
          == "function|function|function|function");
  REQUIRE(state.run("return type(calls.commands.LeetPanel)") == "function");
  REQUIRE(state.run("return jot.leetcode_feature.config.get('endpoint')") == "leetcode.com");
  REQUIRE(state.run("return calls.status.name..'|'..calls.status.spec.side") == "leetcode|right");
}

TEST_CASE("LeetCode list pages send filters and normalize status", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    -- The checklist glyphs and the inks the theme drives them with; the list
    -- reads as a checklist: done, in progress, or plain.
    local check = string.char(0xE2, 0x9C, 0x93)
    local dot = string.char(0xE2, 0x80, 0xA2)
    jot.theme.palette = function() return {git_added={fg=2}, status_warning={fg=3}} end
    local body = json.encode({data={problemsetQuestionList={total=3, questions={
      {frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy', status='ac'},
      {frontend_id='2', title='Hard One', title_slug='hard-one', difficulty='Hard'},
      {frontend_id='3', title='Attempted One', title_slug='attempted-one', difficulty='Medium', status='notac'},
    }}}})
    ui.list('difficulty=easy, status=solved')
    local live = calls.status.spec.text()
    local sent = json.decode(calls.request.body).variables
    calls.callback({ok=true, status=200, body=body, error=''})
    local items = calls.picker.items()
    return table.concat({
      tostring(live:find('loading list') ~= nil),
      tostring(sent.categorySlug == 'all-code-essentials' and sent.skip == 0 and sent.limit == 100),
      tostring(sent.filters.difficulty == 'EASY' and sent.filters.status == 'AC'),
      tostring(items[1] and items[1].value == 'easy-one'),
      tostring(items[1] and items[1].detail:find('solved') ~= nil),
      tostring(items[2] and items[2].detail:find('todo') ~= nil),
      tostring(tostring(calls.message):find('Loaded 3 of 3') ~= nil),
      tostring(items[1] and items[1].label:find(check, 1, true) == 1 and items[1].fg == 2),
      tostring(items[2] and items[2].label:find(check, 1, true) == nil
               and items[2].label:find(dot, 1, true) == nil and items[2].fg == nil),
      tostring(items[3] and items[3].label:find(dot, 1, true) == 1 and items[3].fg == 3),
    }, '|')
  )LUA") == "true|true|true|true|true|true|true|true|true|true");
}

TEST_CASE("LeetCode cookie prompt opens masked and strips pasted line breaks", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run(R"LUA(
    package.loaded['jot_lc.ui'].open_cookie_prompt()
    local opened = calls.float_open
    local intro = float_lines[2]
    float_callbacks.paste("LEETCODE_SESSION=pasted-cookie; " .. string.char(13, 10) .. "csrftoken=csrf")
    local visible = table.concat(float_lines, string.char(10))
    float_callbacks.key({key=13})
    return tostring(opened ~= nil) .. "|" .. tostring(opened.config.title == "LeetCode sign-in") .. "|" ..
           tostring(opened.config.focusable == true) .. "|" .. tostring(intro) .. "|" ..
           tostring(visible:find("pasted%-cookie") == nil) .. "|" ..
           tostring(visible:find("%*%*%*%*%*%*%*%*%*%*%*%*", 1) ~= nil) .. "|" ..
           tostring(calls.cookie == "LEETCODE_SESSION=pasted-cookie; csrftoken=csrf") .. "|" ..
           tostring(store.leetcode_session == nil) .. "|" ..
           tostring(opened.config.border == "single") .. "|" ..
           tostring(float_spans ~= nil and float_spans.line == 5)
  )LUA") == "true|true|true|Open LeetCode in your browser.|true|true|true|true|true|true");
}

TEST_CASE("LeetCode tabs, yank and judge console keep the opened question usable",
          "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    local body = json.encode({data={problemsetQuestionList={total=1, questions={
      {frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy', status='ac'},
    }}}})
    -- The unfiltered list is a group-picker entry now, so page=1 is the
    -- filter-free path that goes straight to it.
    ui.list('page=1')
    -- The resolver requires the filters variable even when empty; omitting it
    -- fails the request with "resolve_question_list() missing ... 'filters'".
    local unfiltered = json.decode(calls.request.body).variables
    calls.callback({ok=true, status=200, body=body, error=''})
    calls.picker.callback('easy-one')
    local question = json.encode({data={question={
      id='1', frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy',
      content='<p>Add them.</p>', testcase_list={'[1,2]', '[3,4]'},
      code_snippets={{lang='C++', lang_slug='cpp', code='int twoSum() { return 0; }'}},
    }}})
    calls.callback({ok=true, status=200, body=question, error=''})
    calls.picker.callback('cpp')
    local wrote_template = calls.open ~= nil and files[calls.open] ~= nil
                           and files[calls.open]:find('int twoSum') ~= nil
    ui.yank()
    ui.open_tabs()
    local tabs = calls.picker.items()
    ui.run('run')
    -- The judge console lives in the dock panel: the rows the panel callback
    -- returns are what the native renderer paints, so the test reads them the
    -- way the host does, with (name, nil).
    local function panel_rows(event)
      return calls.panel.fn('LeetCode', event)
    end
    local function joined(rows)
      local out = {}
      for _, row in ipairs(rows) do
        out[#out + 1] = tostring(row.text or '') .. '|' .. tostring(row.icon or '')
                       .. '|' .. tostring(row.action or '') .. '|' .. tostring(row.icon_fg or '')
      end
      return table.concat(out, string.char(10))
    end
    local running = joined(panel_rows(nil))
    calls.callback({ok=true, status=200, body=json.encode({interpret_id=42}), error=''})
    calls.callback({ok=true, status=200, body=json.encode({
      status_code=10, status_msg='Accepted',
      total_correct=1, total_testcases=1,
      runtime=4, memory='10.2 MB',
      code_answer={'[0,1]'},
      expected_code_answer={'[0,1]'},
    }), error=''})
    local finished = joined(panel_rows(nil))
    -- A click arrives as (name, {action=...}); the callback runs the handler
    -- and still returns the rows, so the next frame renders the result.
    local clicked = panel_rows({action='next', index=6})
    return table.concat({
      tostring(unfiltered.filters ~= nil),
      tostring(calls.clipboard ~= nil and calls.clipboard:find('twoSum') ~= nil),
      tostring(tabs[1] and tabs[1].value == 1),
      tostring(wrote_template),
      tostring(calls.panel_shown == 'LeetCode' and calls.panel.name == 'LeetCode'),
      tostring(running:find('Running example 1 of 2') ~= nil),
      tostring(finished:find('Accepted') ~= nil and finished:find('Output:') ~= nil),
      tostring(finished:find('correct  1 / 1') ~= nil),
      tostring(finished:find('Run test|' ) ~= nil and finished:find('|run|') ~= nil),
      tostring(finished:find('Submit|') ~= nil and finished:find('|submit|') ~= nil),
      tostring(#clicked > 0 and clicked[1].text:find('Easy One') ~= nil),
    }, '|')
  )LUA") == "true|true|true|true|true|true|true|true|true|true|true");
}

TEST_CASE("LeetCode list load-more accumulates pages instead of stopping at one", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    local function page(rows, total)
      return json.encode({data={problemsetQuestionList={total=total, questions=rows}}})
    end
    ui.list('difficulty=easy')
    calls.callback({ok=true, status=200, body=page({
      {frontend_id='1', title='One', title_slug='one', difficulty='Easy'},
      {frontend_id='2', title='Two', title_slug='two', difficulty='Easy'},
    }, 3), error=''})
    local first = calls.picker.items()
    local more = first[#first]
    calls.picker.callback('__more__')
    local sent = json.decode(calls.request.body).variables
    calls.callback({ok=true, status=200, body=page({
      {frontend_id='3', title='Three', title_slug='three', difficulty='Easy'},
    }, 3), error=''})
    local second = calls.picker.items()
    return table.concat({
      tostring(more.value == '__more__' and more.detail:find('2 of 3') ~= nil),
      tostring(sent.skip == 100 and sent.limit == 100),
      tostring(#second == 3 and second[3].value == 'three'),
      tostring(second[#second].value ~= '__more__'),
      tostring(tostring(calls.message):find('Loaded 3 of 3') ~= nil),
    }, '|')
  )LUA") == "true|true|true|true|true");
}

TEST_CASE("LeetCode list groups filter by difficulty, tag, company and favorite", "[leetcode][lua]")
{
  // :LeetList without filters opens five groups instead of dumping every
  // problem: all problems, a difficulty level, one topic tag, one company, or
  // a favorite list. The tag and company sources are the public queries the
  // live API answers anonymously; favorites carry the session when one is
  // stored and stay anonymous (empty) when it is not.
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    jot.theme.palette = function() return {git_added={fg=2}, status_warning={fg=3}} end
    local failures = {}
    local function check(name, value)
      if not value then failures[#failures + 1] = name end
    end
    local function respond(payload)
      calls.callback({ok=true, status=200, body=json.encode(payload), error=''})
    end
    local function list_payload(rows)
      return {data={problemsetQuestionList={total=#rows, questions=rows}}}
    end
    local function row(slug, title, difficulty, status)
      return {frontend_id='1', title=title, title_slug=slug, difficulty=difficulty, status=status}
    end

    -- No filters: five groups instead of every problem.
    ui.list('')
    local groups = calls.picker.items()
    check('five groups', calls.picker.title == 'LeetCode problems' and #groups == 5)
    check('group labels', groups[1].label == 'All problems' and groups[2].label == 'Difficulty'
                          and groups[3].label == 'Tag' and groups[4].label == 'By company'
                          and groups[5].label == 'Favorites')

    -- Difficulty -> all levels -> the chosen one becomes the filter.
    calls.picker.callback('difficulty')
    local levels = calls.picker.items()
    check('difficulty levels', #levels == 4 and levels[1].value == 'all'
                               and levels[2].value == 'easy' and levels[3].value == 'medium'
                               and levels[4].value == 'hard')
    calls.picker.callback('medium')
    local medium_sent = json.decode(calls.request.body).variables
    check('difficulty filter', medium_sent.filters.difficulty == 'MEDIUM')
    respond(list_payload({row('medium-one', 'Medium One', 'Medium')}))
    check('difficulty list', calls.picker.items()[1].value == 'medium-one'
                             and calls.picker.title:find('Medium') ~= nil)

    -- Tag -> one public tag query, then the list carries the slug.
    ui.list('')
    calls.picker.callback('tag')
    check('tag query', json.decode(calls.request.body).query:find('questionTopicTags', 1, true) ~= nil)
    respond({data={topicTags={edges={{node={name='Backtracking', slug='backtracking'}},
                                    {node={name='Array', slug='array'}}}}}})
    local tags = calls.picker.items()
    check('tags sorted', tags[1].label == 'Array' and tags[2].label == 'Backtracking')
    calls.picker.callback('array')
    local tag_sent = json.decode(calls.request.body).variables
    respond(list_payload({row('tag-one', 'Tag One', 'Easy')}))
    check('tag filter', tag_sent.filters.tags[1] == 'array'
                        and calls.picker.title:find('Array') ~= nil)

    -- By company -> the public company query, then the list carries it too.
    ui.list('')
    calls.picker.callback('company')
    check('company query', json.decode(calls.request.body).query:find('companyTags', 1, true) ~= nil)
    respond({data={companyTags={{name='Google', slug='google'}, {name='Amazon', slug='amazon'}}}})
    local companies = calls.picker.items()
    check('companies sorted', companies[1].label == 'Amazon' and companies[2].label == 'Google')
    calls.picker.callback('amazon')
    local company_sent = json.decode(calls.request.body).variables
    respond(list_payload({row('amazon-one', 'Amazon One', 'Hard')}))
    check('company filter', company_sent.filters.companies[1] == 'amazon'
                            and calls.picker.title:find('Amazon') ~= nil)

    -- Favorites -> the stored session makes the list query carry the cookie;
    -- the chosen list's questions render as a list with its marks and pages.
    ui.list('')
    calls.picker.callback('favorite')
    local favorite_request = json.decode(calls.request.body)
    check('favorite lists query', favorite_request.query:find('myCreatedFavoriteList', 1, true) ~= nil)
    check('favorite lists authed', calls.request.headers[6] ~= nil
                                   and calls.request.headers[6].name == 'Cookie')
    respond({data={myCreatedFavoriteList={favorites={{name='My List', slug='my-list'}}}}})
    check('favorite list picker', calls.picker.items()[1].label == 'My List')
    calls.picker.callback('my-list')
    local favorite_sent = json.decode(calls.request.body).variables
    respond({data={favoriteQuestionList={total=2, has_more=true, questions={
      row('favorite-one', 'Favorite One', 'Easy', 'ac')}}}})
    local favorite_items = calls.picker.items()
    check('favorite query', favorite_sent.favoriteSlug == 'my-list' and favorite_sent.skip == 0
                            and favorite_sent.limit == 100)
    check('favorite rows', favorite_items[1].value == 'favorite-one'
                           and favorite_items[1].label:find(string.char(0xE2, 0x9C, 0x93), 1, true) == 1
                           and favorite_items[1].fg == 2)
    check('favorite more', favorite_items[2].value == '__more__'
                           and favorite_items[2].label == 'Load more favorites')
    check('favorite title', calls.picker.title:find('My List') ~= nil)
    calls.picker.callback('__more__')
    local favorite_next = json.decode(calls.request.body).variables
    respond({data={favoriteQuestionList={total=2, has_more=false, questions={
      row('favorite-two', 'Favorite Two', 'Medium')}}}})
    check('favorite next page', favorite_next.skip == 100 and #calls.picker.items() == 2
                                and calls.picker.items()[2].value == 'favorite-two')

    -- Signed out: the list query stays anonymous and the empty answer points
    -- at the sign-in command instead of failing.
    local credentials = jot.leetcode.credential_get
    jot.leetcode.credential_get = function() return {ok=true, available=true, value=''} end
    ui.list('')
    calls.picker.callback('favorite')
    check('signed-out favorite anonymous', calls.request.headers[6] == nil)
    respond({data={myCreatedFavoriteList={favorites={}}}})
    check('signed-out favorite message', tostring(calls.message):find('No favorite lists found') ~= nil)
    jot.leetcode.credential_get = credentials

    -- The company filter also has the direct command form.
    ui.list('company=amazon')
    local direct = json.decode(calls.request.body).variables
    respond(list_payload({row('direct-one', 'Direct One', 'Easy')}))
    check('company command', direct.filters.companies[1] == 'amazon')

    return #failures == 0 and 'ok' or table.concat(failures, ', ')
  )LUA") == "ok");
}

TEST_CASE("LeetCode status filter maps to the API enum", "[leetcode][lua]")
{
  // solved maps to AC, the enum the live API accepts; an unsupported status
  // (attempted has no server-side filter) is dropped with a message instead
  // of being sent as SOLVED/ATTEMPTED, which the API rejects with an error.
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    ui.list('status=solved')
    local solved = json.decode(calls.request.body).variables.filters
    calls.callback({ok=true, status=200, body=json.encode({data={problemsetQuestionList={total=1, questions={
      {frontend_id='1', title='One', title_slug='one', difficulty='Easy'},
    }}}}), error=''})
    ui.list('status=attempted')
    local attempted = json.decode(calls.request.body).variables.filters
    local warned = table.concat(calls.messages or {}, '|')
    return tostring(solved.status == 'AC' and attempted.status == nil)
           .. '|' .. tostring(warned:find('has no LeetCode filter') ~= nil)
  )LUA") == "true|true");
}

TEST_CASE("LeetCode testcase list accepts the live array shape", "[leetcode][lua]")
{
  // The live API returns exampleTestcaseList as an array of case strings; the
  // old string-only parser concatenated it and threw inside the question
  // callback, so the language picker never opened after the loading toast.
  State state;
  REQUIRE(state.run(R"LUA(
    local solution = package.loaded['jot_lc.solution']
    local live = solution.testcases({testcase_list={'[2,7,11,15]' .. string.char(10) .. '9', '[3,3]' .. string.char(10) .. '6'}})
    local legacy = solution.testcases({testcase_list='[1,2]' .. string.char(10) .. string.char(10) .. '[3,4]'})
    local missing = solution.testcases({})
    local empty = solution.testcases({testcase_list={}})
    return table.concat({
      tostring(#live == 2 and live[1]:find('2,7,11,15') ~= nil),
      tostring(#legacy == 2 and legacy[2] == '[3,4]'),
      tostring(#missing == 0 and #empty == 0),
    }, '|')
  )LUA") == "true|true|true");
}

TEST_CASE("LeetPanel opens the dock's LeetCode panel on its own", "[leetcode][lua]")
{
  // The dock comes up by itself when a question or a run needs it; :LeetPanel
  // is the command for bringing it up first. It makes the same
  // jot.ui.panel("LeetCode") call the rest of the feature does and asks for a
  // repaint, and a second call is harmless (the host activates the tab that is
  // already there), which is what makes it safe to reach for at any time.
  State state;
  REQUIRE(state.run("calls.panel_shown = nil; calls.redraw = 0; "
                    "calls.commands.LeetPanel(); "
                    "calls.commands.LeetPanel(); "
                    "return tostring(calls.panel_shown) .. '|' .. "
                    "tostring(calls.panel and calls.panel.name) .. '|' .. "
                    "tostring((calls.redraw or 0) >= 2)") == "LeetCode|LeetCode|true");
}

TEST_CASE("LeetCode dock panel rows carry actions and colored judge outcomes", "[leetcode][lua]")
{
  // The panel replaced the floating judge console: its rows are what the dock
  // renders, and a click comes back as (name, {action}). A row with no action
  // must stay flat (no hover, no click), or every console line would look like
  // a button.
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    local function rows(event) return calls.panel.fn('LeetCode', event) end
    local function find(rows_, prefix)
      for _, row in ipairs(rows_) do
        if tostring(row.text or ''):find(prefix, 1, true) then return row end
      end
      return nil
    end
    -- No problem open: the panel says so and offers no actions.
    local empty = rows(nil)
    local empty_actions = 0
    for _, row in ipairs(empty) do if row.action then empty_actions = empty_actions + 1 end end

    -- Open a question, then run: the panel shows the question and the actions.
    local body = json.encode({data={problemsetQuestionList={total=1, questions={
      {frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy'},
    }}}})
    ui.list('page=1')
    calls.callback({ok=true, status=200, body=body, error=''})
    calls.picker.callback('easy-one')
    calls.callback({ok=true, status=200, body=json.encode({data={question={
      id='1', frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy',
      testcase_list={'[1,2]', '[3,4]'},
      code_snippets={{lang='C++', lang_slug='cpp', code='int twoSum() { return 0; }'}},
    }}}), error=''})
    calls.picker.callback('cpp')
    local opened = rows(nil)
    local run = find(opened, 'Run test')
    local submit = find(opened, 'Submit')
    local next_row = find(opened, 'Next example')

    -- A failing run paints the failure rows red with a cross icon; a passing
    -- one paints them green with a check.
    ui.run('run')
    calls.callback({ok=true, status=200, body=json.encode({interpret_id=7}), error=''})
    calls.callback({ok=true, status=200, body=json.encode({
      status_code=11, status_msg='Wrong Answer', total_correct=1, total_testcases=3,
      code_answer={'[9]'}, expected_code_answer={'[0,1]'},
    }), error=''})
    local failed = rows(nil)
    local fail_status = find(failed, 'Wrong Answer')
    local fail_cases = find(failed, 'correct  1 / 3')

    ui.run('run')
    calls.callback({ok=true, status=200, body=json.encode({interpret_id=8}), error=''})
    calls.callback({ok=true, status=200, body=json.encode({
      status_code=10, status_msg='Accepted', total_correct=3, total_testcases=3,
      code_answer={'[0,1]'}, expected_code_answer={'[0,1]'},
    }), error=''})
    local passed = rows(nil)
    local ok_status = find(passed, 'Accepted')
    local ok_cases = find(passed, 'correct  3 / 3')

    -- Clicking the next-example row advances the selection and repaints.
    local before = find(rows(nil), 'Next example').detail
    local after = find(rows({action='next'}), 'Next example').detail

    return table.concat({
      tostring(#empty == 3 and empty_actions == 0),
      tostring(run and run.action == 'run' and submit and submit.action == 'submit'
               and next_row and next_row.action == 'next'),
      tostring(run.icon ~= '' and submit.icon ~= '' and run.icon ~= submit.icon),
      tostring(fail_status and fail_status.icon ~= '' and fail_status.icon_fg ~= nil
               and ok_status and ok_status.icon_fg ~= nil
               and fail_status.icon_fg ~= ok_status.icon_fg),
      tostring(fail_cases and fail_cases.fg == fail_status.fg),
      tostring(ok_status and ok_status.text:find('Accepted') ~= nil and ok_cases ~= nil),
      tostring(before == '1/2' and after == '2/2'),
      tostring(calls.panel_shown == 'LeetCode'),
    }, '|')
  )LUA") == "true|true|true|true|true|true|true|true");
}

TEST_CASE("LeetCode panel shows the statement and bordered examples", "[leetcode][lua]")
{
  // The panel carries the problem itself: the statement prose wrapped to the
  // dock, then the selected example's labelled values in bordered blocks, the
  // way the site shows them. The content's own <pre> blocks are what carry the
  // labels, so a question without them still falls back to the testcase text.
  State state;
  REQUIRE(state.run(R"LUA(
    local panel = package.loaded['jot_lc.panel']
    local json = package.loaded['jot_lc.json']
    local content = '<p>Given an array of integers <b>nums</b> and an integer target.</p>'
                    .. '<p>You may assume exactly one solution.</p>'
                    .. '<pre>' .. string.char(10)
                    .. '<strong>Input:</strong> nums = [2,7,11,15], target = 9' .. string.char(10)
                    .. '<strong>Output:</strong> [0,1]' .. string.char(10)
                    .. '<strong>Explanation:</strong> Because nums[0] + nums[1] == 9, we return [0, 1].'
                    .. string.char(10) .. '</pre>'
                    .. '<pre>' .. string.char(10)
                    .. '<strong>Input:</strong> nums = [3,2,4], target = 6' .. string.char(10)
                    .. '<strong>Output:</strong> [1,2]' .. string.char(10) .. '</pre>'
    local question = {id='1', frontend_id='1', title='Two Sum', title_slug='two-sum',
                      difficulty='Easy', content=content,
                      testcase_list={'[2,7,11,15]' .. string.char(10) .. '9', '[3,2,4]' .. string.char(10) .. '6'},
                      code_snippets={{lang='C++', lang_slug='cpp', code='class Solution {};'}}}
    local ui = package.loaded['jot_lc.ui']
    ui.list('page=1')
    calls.callback({ok=true, status=200, body=json.encode({data={problemsetQuestionList={total=1, questions={question}}}}), error=''})
    calls.picker.callback('two-sum')
    calls.callback({ok=true, status=200, body=json.encode({data={question=question}}), error=''})
    calls.picker.callback('cpp')
    local function joined(rows)
      local out = {}
      for _, row in ipairs(rows) do out[#out + 1] = tostring(row.text or '') end
      return table.concat(out, string.char(10))
    end
    local first = calls.panel.fn('LeetCode', nil)
    local body = joined(first)
    -- The second example only appears once Next example selects it.
    local second = joined(calls.panel.fn('LeetCode', {action='next'}))
    local labels = {}
    for _, row in ipairs(first) do
      local label = tostring(row.text or ''):match('^ (Input)$') or tostring(row.text or ''):match('^ (Output)$')
                            or tostring(row.text or ''):match('^ (Explanation)$')
      if label then labels[#labels + 1] = label end
    end
    return table.concat({
      -- The statement wraps to the dock, so the sentence is split across rows.
      tostring(body:find('Given an array of integers nums and an', 1, true) ~= nil
               and body:find('integer target', 1, true) ~= nil),
      tostring(body:find('You may assume exactly one solution', 1, true) ~= nil),
      tostring(body:find(string.char(0xE2, 0x94, 0x8C), 1, true) ~= nil
               and body:find(string.char(0xE2, 0x94, 0x94), 1, true) ~= nil),
      tostring(#labels == 3 and labels[1] == 'Input' and labels[2] == 'Output'
               and labels[3] == 'Explanation'),
      tostring(body:find('nums = [2,7,11,15], target = 9', 1, true) ~= nil
               and body:find('[0,1]', 1, true) ~= nil),
      tostring(body:find('nums = [3,2,4]', 1, true) == nil),
      tostring(second:find('nums = [3,2,4]', 1, true) ~= nil
               and second:find('nums = [2,7,11,15]', 1, true) == nil),
    }, '|')
  )LUA") == "true|true|true|true|true|true|true");
}

TEST_CASE("LeetCode panel marks a completed problem", "[leetcode][lua]")
{
  // Both surfaces answer "have I done this one?": the platform reports
  // ac/notac on the list and on the single-question fetch (verified against
  // the live API), and the dock renders it as a check or a dot with the
  // theme's success / warning ink.
  State state;
  REQUIRE(state.run(R"LUA(
    local ui = package.loaded['jot_lc.ui']
    local json = package.loaded['jot_lc.json']
    jot.theme.palette = function() return {git_added={fg=2}, status_warning={fg=3}} end
    local function question(status)
      return json.encode({data={question={
        id='1', frontend_id='1', title='Easy One', title_slug='easy-one', difficulty='Easy',
        status=status, content='<p>Add them.</p>', testcase_list={'[1,2]'},
        code_snippets={{lang='C++', lang_slug='cpp', code='int twoSum() { return 0; }'}},
      }}})
    end
    local sent = {}
    local function fetch(slug, status)
      ui.open_question(slug)
      sent[#sent + 1] = json.decode(calls.request.body).query
      calls.callback({ok=true, status=200, body=question(status), error=''})
      calls.picker.callback('cpp')
    end
    local function row(rows, needle)
      for _, row in ipairs(rows) do
        if tostring(row.text or '') == needle then return row end
      end
      return nil
    end
    fetch('easy-one', 'ac')
    local solved_status = tostring(ui.current.status)
    local solved = row(calls.panel.fn('LeetCode', nil), 'Completed')
    fetch('hard-one', 'notac')
    local attempted = row(calls.panel.fn('LeetCode', nil), 'Attempted')
    return table.concat({
      tostring(solved_status == 'solved'),
      -- The marker is only as good as the fetch: the single-question query has
      -- to ask for status (the live API accepts it, verified once by hand).
      tostring(sent[1] ~= nil and sent[1]:find(' status ', 1, true) ~= nil),
      tostring(solved ~= nil and solved.icon == string.char(0xEF, 0x80, 0x8C)
               and solved.icon_fg == 2),
      tostring(attempted ~= nil and attempted.icon_fg == 3),
      tostring(row(calls.panel.fn('LeetCode', nil), 'Completed') == nil),
    }, '|')
  )LUA") == "true|true|true|true|true");
}

TEST_CASE("LeetCode solutions open inside the workspace unless configured", "[leetcode][lua]")
{
  // The user opens a problem while working in a project, so the template
  // belongs in that project: the path follows the workspace root unless an
  // explicit leetcode_solution_dir is set, which still wins.
  State state;
  REQUIRE(state.run(R"LUA(
    local solution = package.loaded['jot_lc.solution']
    local question = {title_slug='two-sum'}
    local in_workspace = solution.path(question, 'cpp')
    store['leetcode_solution_dir'] = '/custom/solutions'
    local configured = solution.path(question, 'cpp')
    store['leetcode_solution_dir'] = nil
    return table.concat({
      tostring(in_workspace == '/tmp/jot-lc-workspace/leetcode/two-sum.cpp'),
      tostring(configured == '/custom/solutions/two-sum.cpp'),
    }, '|')
  )LUA") == "true|true");
}

TEST_CASE("LeetCode authenticated requests keep cookies in request headers", "[leetcode][lua]")
{
  State state;
  REQUIRE(state.run("package.loaded['jot_lc.client'].auth(function() end); "
                    "local h=calls.request.headers; return h[6].name..'|'..h[6].value..'|'..h[7].name..'|'..h[7].value")
          == "Cookie|LEETCODE_SESSION=fake; csrftoken=csrf|x-csrftoken|csrf");
  REQUIRE(state.run("return tostring(store.leetcode_session)") == "nil");
}
