local config = require("jot_lc.config")
local client = require("jot_lc.client")
local solution = require("jot_lc.solution")
local cache = require("jot_lc.cache")
local panel = require("jot_lc.panel")
local M = {current=nil, busy=false, cache_hit=false, testcase=1, testcases={}, tabs={}, console_output=nil}

local function notify(text)
  jot.ui.show_message("[leet] " .. text)
end

client.set_feedback(notify)

-- The status line carries a live marker while anything is in flight: an
-- operation label set by the caller, or the raw in-flight HTTP count. Without
-- it a slow request looks exactly like a dead command.
local activity = ""
local function begin_work(label)
  activity = label
end
local function end_work()
  activity = ""
end

pcall(function()
  jot.status.register("leetcode", {
    side = "right",
    priority = 22,
    text = function()
      if activity ~= "" then return " leet: " .. activity end
      local pending = client.pending()
      if pending > 0 then return " leet: " .. pending .. " request(s)" end
      return ""
    end,
  })
end)

local function parse_filters(value)
  local filters = {}
  for raw_name, raw_setting in tostring(value or ""):gmatch("([%w_]+)%s*=%s*([^,]+)") do
    local name = raw_name:lower()
    local setting = raw_setting:gsub("^%s+", ""):gsub("%s+$", ""):lower()
    if name == "difficulty" then filters.difficulty = setting
    elseif name == "status" then filters.status = setting
    elseif name == "tags" or name == "tag" then filters.tags = setting
    elseif name == "company" or name == "companies" then
      local companies = {}
      for company in setting:gmatch("[^,%s]+") do companies[#companies + 1] = company end
      if #companies > 0 then filters.companies = companies end
    elseif name == "page" then filters.page = tonumber(setting)
    end
  end
  return filters
end

local status_enum = {solved="AC", todo="NOT_STARTED"}

-- The list and random queries take QuestionListFilterInput. Difficulty is
-- uppercased, tags are slugs, but the status vocabulary is not the site's:
-- UserQuestionStatus only accepts AC and NOT_STARTED (verified against the
-- live API, which rejects SOLVED/ATTEMPTED/TODO), so solved and todo are
-- mapped here and any other value is reported as unsupported.
local function graphql_filters(filters)
  local input = {}
  local unsupported
  if filters.difficulty then input.difficulty = filters.difficulty:upper() end
  if filters.status then
    local mapped = status_enum[filters.status]
    if mapped then input.status = mapped else unsupported = filters.status end
  end
  if filters.tags then
    local tags = {}
    for tag in tostring(filters.tags):gmatch("[^,%s]+") do tags[#tags + 1] = tag end
    if #tags > 0 then input.tags = tags end
  end
  if filters.companies and next(filters.companies) ~= nil then input.companies = filters.companies end
  if next(input) == nil then return nil, unsupported end
  return input, unsupported
end

local function palette()
  local theme = {}
  pcall(function() theme = jot.theme.palette() or {} end)
  local function pick(slot, field, fallback)
    local value = type(theme[slot]) == "table" and theme[slot][field]
    if type(value) == "number" then return value end
    return fallback
  end
  return {
    fg = pick("default", "fg", -1),
    bg = pick("command", "bg", pick("default", "bg", -1)),
    border = pick("panel_border", "fg", -1),
    accent = pick("status_info", "fg", -1),
    comment = pick("comment", "fg", -1),
    selection_fg = pick("selection", "fg", -1),
    selection_bg = pick("selection", "bg", -1),
    ok = pick("git_added", "fg", -1),
    warn = pick("status_warning", "fg", -1),
  }
end

-- Floats paint over the editor, but the sidebar and pane chrome are floats
-- too and are recreated every frame, so they repaint over any part of a
-- panel that reaches into their columns (the console's left border and first
-- title cells vanished under a visible sidebar). Center inside the focused
-- pane and let callers keep the box above the chrome layer (zindex 60).
local function centered_over_pane(width, row)
  local ok, viewport = pcall(function() return jot.viewport.info() end)
  if not ok or type(viewport) ~= "table" then viewport = {} end
  local window = viewport.window or {}
  local window_width = tonumber(window.width) or 100
  local pane = viewport.pane or {}
  local pane_x = tonumber(pane.x) or 0
  local pane_width = tonumber(pane.width) or (window_width - pane_x)
  local w = math.max(24, math.min(width, pane_width - 4))
  local x = pane_x + math.max(1, math.floor((pane_width - w) / 2))
  return x, row, w
end

local function remember_tab(question)
  if type(question) ~= "table" or type(question.title_slug) ~= "string" then return end
  for _, entry in ipairs(M.tabs) do
    if entry.question.title_slug == question.title_slug then
      entry.question = question
      entry.language = question.lang_slug
      entry.path = question.solution_path
      return
    end
  end
  M.tabs[#M.tabs + 1] = {question=question, language=question.lang_slug, path=question.solution_path}
end

local function popup(title, text)
  jot.ui.popup(text, title)
end

local function plain_description(html)
  local text = tostring(html or "")
  text = text:gsub("<br%s*/?>", "\n")
  text = text:gsub("</p%s*>", "\n\n"):gsub("</pre%s*>", "\n\n")
  text = text:gsub("</li%s*>", "\n"):gsub("<li[^>]*>", "  - ")
  text = text:gsub("<[^>]->", "")
  text = text:gsub("&nbsp;", " "):gsub("&lt;", "<"):gsub("&gt;", ">")
  text = text:gsub("&amp;", "&"):gsub("&quot;", '\"'):gsub("&#39;", "'")
  text = text:gsub("\n[ \t]+\n", "\n\n"):gsub("\n\n\n+", "\n\n")
  return text
end

local function language_picker(question)
  local rows = {}
  for _, snippet in ipairs(question.code_snippets or {}) do
    rows[#rows + 1] = {label=snippet.lang, value=snippet.lang_slug, detail=snippet.lang_slug}
  end
  if #rows == 0 then notify("This question has no language templates"); return end
  jot.ui.picker("LeetCode language", function() return rows end, function(lang)
    local ok, value = solution.open(question, lang)
    if not ok then popup("LeetCode error", tostring(value)); return end
    M.current = question
    M.testcases = solution.testcases(question)
    M.testcase = 1
    remember_tab(question)
    notify("Opened " .. value)
    -- The question's dock panel carries its title, its run/submit rows and the
    -- judge console, so opening the problem is what brings it up.
    M.show_panel()
  end)
end

function M.open_question(slug, refresh)
  if M.busy then notify("A question request is already running"); return end
  M.busy = true
  begin_work("loading " .. tostring(slug))
  notify("Loading question " .. tostring(slug) .. "...")
  client.question(slug, function(question, err, cached)
    M.busy = false
    end_work()
    if err then popup("LeetCode error", "Question failed to load.\n\n" .. err); return end
    if type(question) ~= "table" then popup("LeetCode", "No question data was returned."); return end
    M.current = question
    M.cache_hit = cached == true
    M.testcases = solution.testcases(question)
    M.testcase = 1
    notify(tostring(question.frontend_id or "") .. ". " .. tostring(question.title or slug)
           .. " · " .. tostring(question.difficulty or "Unknown"))
    language_picker(question)
  end, refresh)
end

local list_state = nil

-- The picker rows for the accumulated list. GraphQL caps one page at 100
-- questions, so a full problem set cannot be fetched in a single request
-- without the multi-megabyte decode that froze the editor; the last row
-- loads the next page and reopens the picker with everything fetched so far.
-- The list doubles as the checklist: a solved problem leads with a check and
-- the success ink, an attempted one with a dot in the warning colour, and a
-- fresh one carries no mark so the eye follows what is left to do.
local function status_mark(status, colors)
  if status == "solved" then return "\u{2713} ", colors.ok end
  if status == "attempted" then return "\u{2022} ", colors.warn end
  return "", nil
end

local function show_list()
  local colors = palette()
  local state = list_state
  local items = {}
  for _, question in ipairs(state.rows) do
    local mark, ink = status_mark(question.status, colors)
    items[#items + 1] = {label=mark .. string.format("%s. %s", question.frontend_id or "?", question.title or question.title_slug or ""),
                         value=question.title_slug,
                         fg=ink,
                         detail=tostring(question.difficulty or "") .. (question.status and " · " .. question.status or "")}
  end
  local total = tonumber(state.total) or #state.rows
  if #state.rows < total then
    items[#items + 1] = {label=state.favorite and "Load more favorites" or "Load more problems",
                         value="__more__",
                         detail=tostring(#state.rows) .. " of " .. tostring(total) .. " loaded"}
  end
  if #items == 0 then notify("No problems in this result"); return end
  jot.ui.picker(state.title, function() return items end, function(value)
    if value == "__more__" then M.load_more() else M.open_question(value) end
  end)
end

local function fetch_list_page(page, replace)
  local state = list_state
  local page_size = math.min(100, tonumber(config.get("list_limit")) or 100)
  local filter_input, unsupported = graphql_filters(state.filters)
  M.busy = true
  begin_work("loading list page " .. page)
  notify("Loading problem list (page " .. page .. ")...")
  if unsupported then
    notify("status=" .. unsupported .. " has no LeetCode filter; use solved or todo")
  end
  client.list(filter_input, (page - 1) * page_size, function(rows, total, err)
    M.busy = false
    end_work()
    if err then
      jot.picker.close()
      popup("LeetCode error", "Problem list failed to load.\n\n" .. err)
      return
    end
    if not rows or #rows == 0 then
      jot.picker.close()
      if state.filters.companies then
        popup("LeetCode companies", "No problems were returned for this company.\n\n"
              .. "Company problem lists can require a LeetCode premium account.")
      else
        popup("LeetCode", "No problem matches that page or filter.")
      end
      return
    end
    if replace then state.rows = rows
    else
      for _, row in ipairs(rows) do state.rows[#state.rows + 1] = row end
    end
    state.page = page
    state.total = total or state.total or #state.rows
    notify("Loaded " .. #state.rows .. " of " .. tostring(state.total) .. " problem(s)")
    show_list()
  end)
end

-- The list command without filters opens five groups instead of dumping every
-- problem: all problems, one difficulty level, one topic tag, one company, or
-- a saved favorite list. Each group answers with a second picker (levels, the
-- tag list, the company list, the favorite lists) whose choice becomes the
-- filter of the usual paginated list.
local function start_list(filters, title)
  if M.busy then notify("A request is already running"); return end
  local page = math.max(1, math.floor(filters.page or 1))
  list_state = {filters=filters, page=page, rows={}, title=title or "LeetCode problems"}
  fetch_list_page(page, true)
end

local difficulty_levels = {
  {label="All difficulties", value="all", detail="No difficulty filter"},
  {label="Easy", value="easy", detail="Show only easy problems"},
  {label="Medium", value="medium", detail="Show only medium problems"},
  {label="Hard", value="hard", detail="Show only hard problems"},
}

local function choose_difficulty()
  local rows = {}
  for _, level in ipairs(difficulty_levels) do rows[#rows + 1] = level end
  jot.ui.picker("LeetCode difficulty", function() return rows end, function(value)
    if value == "all" then start_list({}); return end
    local label = value
    for _, level in ipairs(difficulty_levels) do
      if level.value == value then label = level.label end
    end
    start_list({difficulty=value}, "LeetCode problems · " .. label)
  end)
end

-- The tag and company pickers fetch their choices first: the items callback
-- runs once, when the picker opens, so the list has to be in hand by then.
-- Both sources come back in no particular order, so sort them for browsing.
local function sort_names(rows)
  table.sort(rows, function(a, b)
    return tostring(a.name or a.slug):lower() < tostring(b.name or b.slug):lower()
  end)
end

local function choose_tag()
  if M.busy then notify("A request is already running"); return end
  M.busy = true
  begin_work("loading tags")
  notify("Loading LeetCode tags...")
  client.tags(function(rows, err)
    M.busy = false
    end_work()
    if err then popup("LeetCode error", "Tags failed to load.\n\n" .. tostring(err)); return end
    if not rows or #rows == 0 then notify("No LeetCode tags were returned"); return end
    sort_names(rows)
    local names, items = {}, {}
    for _, tag in ipairs(rows) do
      names[tag.slug] = tag.name or tag.slug
      items[#items + 1] = {label=tostring(tag.name or tag.slug), value=tag.slug}
    end
    jot.ui.picker("LeetCode tag", function() return items end, function(slug)
      if not slug or slug == "" then return end
      start_list({tags=slug}, "LeetCode problems · " .. tostring(names[slug] or slug))
    end)
  end)
end

local function choose_company()
  if M.busy then notify("A request is already running"); return end
  M.busy = true
  begin_work("loading companies")
  notify("Loading LeetCode companies...")
  client.companies(function(rows, err)
    M.busy = false
    end_work()
    if err then popup("LeetCode error", "Companies failed to load.\n\n" .. tostring(err)); return end
    if not rows or #rows == 0 then notify("No LeetCode companies were returned"); return end
    sort_names(rows)
    local names, items = {}, {}
    for _, company in ipairs(rows) do
      names[company.slug] = company.name or company.slug
      items[#items + 1] = {label=tostring(company.name or company.slug), value=company.slug}
    end
    jot.ui.picker("LeetCode company", function() return items end, function(slug)
      if not slug or slug == "" then return end
      start_list({companies={slug}}, "LeetCode problems · " .. tostring(names[slug] or slug))
    end)
  end)
end

local function fetch_favorite_page(page, replace)
  local state = list_state
  local page_size = math.min(100, tonumber(config.get("list_limit")) or 100)
  M.busy = true
  begin_work("loading favorites page " .. page)
  notify("Loading favorite problems (page " .. page .. ")...")
  client.favorite_questions(state.favorite, (page - 1) * page_size, function(rows, total, err)
    M.busy = false
    end_work()
    if err then
      jot.picker.close()
      popup("LeetCode error", "Favorite problems failed to load.\n\n" .. tostring(err))
      return
    end
    if not rows or #rows == 0 then
      jot.picker.close()
      popup("LeetCode favorites", "That favorite list has no problems.")
      return
    end
    if replace then state.rows = rows
    else
      for _, row in ipairs(rows) do state.rows[#state.rows + 1] = row end
    end
    state.page = page
    state.total = total or state.total or #state.rows
    notify("Loaded " .. #state.rows .. " of " .. tostring(state.total) .. " favorite problem(s)")
    show_list()
  end)
end

local function start_favorites(list)
  if M.busy then notify("A request is already running"); return end
  list_state = {filters={}, page=1, rows={},
                title="LeetCode favorites · " .. tostring(list.name or list.slug),
                favorite=list.slug}
  fetch_favorite_page(1, true)
end

local function choose_favorite()
  if M.busy then notify("A request is already running"); return end
  M.busy = true
  begin_work("loading favorite lists")
  notify("Loading LeetCode favorite lists...")
  client.favorite_lists(function(rows, err)
    M.busy = false
    end_work()
    if err then
      if tostring(err):find("sign in") then
        notify("Sign in first (:LeetCookie) to see your LeetCode favorites")
      else
        popup("LeetCode favorites", "Favorite lists failed to load.\n\n" .. tostring(err))
      end
      return
    end
    if not rows or #rows == 0 then
      notify("No favorite lists found; add one on leetcode.com or sign in with :LeetCookie")
      return
    end
    local items = {}
    for _, list in ipairs(rows) do
      items[#items + 1] = {label=tostring(list.name or list.slug), value=list.slug}
    end
    jot.ui.picker("LeetCode favorites", function() return items end, function(slug)
      if not slug or slug == "" then return end
      local chosen = {slug=slug}
      for _, list in ipairs(rows) do
        if list.slug == slug then chosen = list end
      end
      start_favorites(chosen)
    end)
  end)
end

function M.description()
  if not M.current then notify("Open a question first"); return end
  local question = M.current
  local lines = {
    tostring(question.frontend_id or "") .. ". " .. tostring(question.title or question.title_slug),
    "Difficulty: " .. tostring(question.difficulty or "Unknown"),
    "",
    "Language templates: " .. tostring(#(question.code_snippets or {})),
    "Examples: " .. tostring(#M.testcases),
    M.cache_hit and "Question data: cached" or "Question data: refreshed",
  }
  if type(question.topic_tags) == "table" and #question.topic_tags > 0 then
    local tags = {}
    for _, tag in ipairs(question.topic_tags) do tags[#tags + 1] = tag.name end
    lines[#lines + 1] = "Topics: " .. table.concat(tags, ", ")
  end
  lines[#lines + 1] = ""
  lines[#lines + 1] = plain_description(question.content)
  popup("LeetCode problem", table.concat(lines, "\n"))
end

function M.list(value)
  if M.busy then notify("A request is already running"); return end
  local filters = parse_filters(value)
  if next(filters) ~= nil then
    start_list(filters)
    return
  end
  -- The details stay clear of the second-level pickers' own row names, so a
  -- filtered search in the group list cannot look like one of them.
  local groups = {
    {label="All problems", value="all", detail="Every problem in the set"},
    {label="Difficulty", value="difficulty", detail="Choose a level"},
    {label="Tag", value="tag", detail="Pick a topic"},
    {label="By company", value="company", detail="Pick a company"},
    {label="Favorites", value="favorite", detail="Your saved lists"},
  }
  jot.ui.picker("LeetCode problems", function() return groups end, function(group)
    if group == "all" then start_list({})
    elseif group == "difficulty" then choose_difficulty()
    elseif group == "tag" then choose_tag()
    elseif group == "company" then choose_company()
    elseif group == "favorite" then choose_favorite() end
  end)
end

function M.load_more()
  local state = list_state
  if not state or M.busy then return end
  local total = tonumber(state.total) or #state.rows
  if #state.rows >= total then notify("All problems are already loaded"); return end
  if state.favorite then fetch_favorite_page(state.page + 1, false)
  else fetch_list_page(state.page + 1, false) end
end

function M.daily()
  if M.busy then notify("A request is already running"); return end
  M.busy = true
  begin_work("loading daily problem")
  notify("Loading today's problem...")
  client.daily(function(question, err)
    M.busy = false
    end_work()
    if err then popup("LeetCode error", "Daily problem failed to load.\n\n" .. err); return end
    M.current = question
    M.testcases = solution.testcases(question)
    M.testcase = 1
    notify(tostring(question.title) .. " · " .. tostring(question.difficulty or "Unknown"))
    language_picker(question)
  end)
end

function M.random(value)
  if M.busy then notify("A request is already running"); return end
  local filters = parse_filters(value)
  local filter_input, unsupported = graphql_filters(filters)
  M.busy = true
  begin_work("choosing a random problem")
  notify("Choosing a random problem...")
  if unsupported then
    notify("status=" .. unsupported .. " has no LeetCode filter; use solved or todo")
  end
  client.random(function(question, err)
    M.busy = false
    end_work()
    if err then popup("LeetCode error", "Random problem failed.\n\n" .. err); return end
    M.current = question
    M.testcases = solution.testcases(question)
    M.testcase = 1
    notify(tostring(question.title) .. " · " .. tostring(question.difficulty or "Unknown"))
    language_picker(question)
  end, filter_input)
end

function M.profile()
  begin_work("loading profile")
  popup("LeetCode profile", "Loading profile statistics...")
  client.profile(function(data, err)
    end_work()
    if err then popup("LeetCode error", "Profile request failed.\n\n" .. err); return end
    local rows = {}
    for _, item in ipairs((data and data.submit_stats and data.submit_stats.acSubmissionNum) or {}) do
      rows[#rows + 1] = tostring(item.difficulty) .. ": " .. tostring(item.count)
    end
    popup("LeetCode profile", #rows > 0 and table.concat(rows, "\n") or "No profile statistics were returned.")
  end)
end

-- Judge console: run and submit answers carry a status, per-case counts and
-- often a compile or runtime error, and the selected example they ran
-- against. It lives in the right dock's LeetCode panel (panel.lua) as rows
-- under the question, so the outcome sits next to the code instead of in a
-- floating window the user has to close.
local console = {progress=nil, report=nil, result=nil, error=nil}

local function join_output(value)
  if type(value) == "table" then
    local parts = {}
    for _, entry in ipairs(value) do parts[#parts + 1] = tostring(entry) end
    if #parts == 0 then return nil end
    return table.concat(parts, "\n")
  end
  if type(value) == "string" and value ~= "" then return value end
  return nil
end

local function judge_report(result, kind)
  local lines = {"Status: " .. tostring(result.status_msg or "judge finished without a status")}
  if result.total_correct ~= nil or result.total_testcases ~= nil then
    lines[#lines + 1] = "Correct cases: " .. tostring(result.total_correct or "?")
                         .. " / " .. tostring(result.total_testcases or "?")
  end
  if result.runtime ~= nil then lines[#lines + 1] = "Runtime: " .. tostring(result.runtime) .. " ms" end
  if result.memory ~= nil then lines[#lines + 1] = "Memory: " .. tostring(result.memory) end
  local input = join_output(result.input_formatted) or result.last_testcase
  if input then lines[#lines + 1] = "Input:\n" .. tostring(input) end
  local output = join_output(result.code_output) or join_output(result.code_answer)
  if output then lines[#lines + 1] = "Output:\n" .. output end
  local expected = join_output(result.expected_output) or join_output(result.expected_code_answer)
  if expected then lines[#lines + 1] = "Expected:\n" .. expected end
  local stdout = join_output(result.std_output)
  if stdout then lines[#lines + 1] = "Stdout:\n" .. stdout end
  if type(result.full_runtime_error) == "string" and result.full_runtime_error ~= "" then
    lines[#lines + 1] = "Runtime error:\n" .. result.full_runtime_error
  end
  if type(result.full_compile_error) == "string" and result.full_compile_error ~= "" then
    lines[#lines + 1] = "Compile error:\n" .. result.full_compile_error
  end
  return table.concat(lines, "\n")
end

-- The dock panel reads the live workflow state through this provider; a
-- click on a row comes back through the action handlers below, so the panel
-- is the surface that runs and reports the judge.
local function panel_state()
  return {
    question=M.current,
    testcases=M.testcases,
    testcase=M.testcase,
    cache_hit=M.cache_hit,
    console=console,
  }
end

panel.bind(panel_state, {run=function() M.run("run") end,
                         submit=function() M.run("submit") end,
                         next=function() M.next_case() end})

function M.show_panel()
  panel.open()
end

function M.run(kind)
  local question = M.current
  if not question then notify("Open a question first"); return end
  local path = question.solution_path or solution.path(question, question.lang_slug or config.get("language"))
  local code = jot.file.read(path)
  if not code then notify("Save the solution file before running it"); return end
  code = solution.submission_code(question, question.lang_slug or config.get("language"), code)
  local case = M.testcases[M.testcase] or ""
  local submit = kind == "submit"
  console.progress = submit and "Submitting solution..."
                          or ("Running example " .. M.testcase .. " of " .. math.max(1, #M.testcases) .. "...")
  console.report = nil
  console.result = nil
  console.error = nil
  -- The dock panel is where the outcome shows; open it so a run started from
  -- the command line is visible without hunting for the tab.
  panel.open()
  begin_work(submit and "submitting solution" or "running selected example")
  client.judge(kind, question, code, case, function(result, err)
    end_work()
    console.progress = nil
    if err then
      console.error = tostring(err)
      console.report = "Judge request failed.\n\n" .. tostring(err)
      notify("Judge request failed: " .. tostring(err))
    else
      console.result = result
      console.report = judge_report(result, kind)
      notify(tostring(result.status_msg or "Judge completed"))
    end
    panel.refresh()
  end)
end

function M.next_case()
  if #M.testcases == 0 then notify("This question has no examples"); return end
  M.testcase = M.testcase % #M.testcases + 1
  notify("Example " .. M.testcase .. " of " .. #M.testcases)
  panel.refresh()
end

function M.change_language()
  if not M.current then notify("Open a question first"); return end
  language_picker(M.current)
end

function M.reset()
  if not M.current then notify("Open a question first"); return end
  local ok, err = solution.reset(M.current, M.current.lang_slug or config.get("language"))
  notify(ok and "Solution reset to the platform template" or err)
end

function M.restore_latest()
  if not M.current then notify("Open a question first"); return end
  solution.restore_latest(M.current, function(ok, value)
    notify(ok and ("Restored " .. value) or value)
  end)
end

function M.open_browser()
  if not M.current then notify("Open a question first"); return end
  local url = config.problem_url(M.current.title_slug)
  local opened = require("jot_md.browser").open(url, jot.config.get("leetcode_browser", ""))
  if not opened then notify("Browser launch is disabled; URL: " .. url) end
end

function M.auth_status()
  local result = jot.leetcode.credential_get()
  if not result.available then notify(result.error); return end
  if not result.ok then notify("Session store error: " .. result.error); return end
  if result.value == "" then notify("No LeetCode session stored"); return end
  begin_work("checking session")
  notify("Checking the stored LeetCode session...")
  client.auth(function(user, err)
    end_work()
    if err then notify(err); return end
    notify("Signed in as " .. tostring(user.name or "LeetCode user"))
  end)
end

function M.sign_out()
  local result = jot.leetcode.credential_delete()
  notify(result.ok and "Session removed from the OS credential store" or result.error)
end

function M.status()
  popup("LeetCode", table.concat({
    M.current and (tostring(M.current.frontend_id or "") .. ". " .. tostring(M.current.title))
      or "No active problem",
    "Difficulty: " .. tostring(M.current and M.current.difficulty or "n/a"),
    "Endpoint: " .. config.get("endpoint"),
    "Language: " .. tostring(M.current and M.current.lang_slug or config.get("language")),
    "Solution: " .. tostring(M.current and M.current.solution_path or "not opened"),
    "On disk: " .. tostring(M.current and jot.file.read(M.current.solution_path or "") ~= nil or false),
    "Examples: " .. tostring(#M.testcases),
    "Tabs: " .. tostring(#M.tabs),
    "Cache TTL: " .. tostring(config.get("cache_ttl")) .. " seconds",
    "",
    "Use :Leet list, :Leet daily, or :Leet random to choose a problem.",
  }, "\n"))
end

function M.actions()
  local items = {
    {label="Browse problem list", detail="Filter and open a problem"},
    {label="Daily problem", detail="Load today's question"},
    {label="Random problem", detail="Choose an algorithm problem"},
    {label="Profile statistics", detail="Requires a stored session"},
    {label="Session status", detail="Check OS credential store"},
    {label="Sign out", detail="Remove stored session"},
    {label="Clear question cache", detail="Remove public cached questions"},
    {label="Refresh active question", detail="Fetch question data again"},
    {label="Reset solution", detail="Restore the platform template"},
    {label="Show active problem status", detail="Endpoint, language and testcase"},
  }
  if #M.tabs > 0 then
    items[#items + 1] = {label="Switch LeetCode tab", detail="Reopen a question you already opened"}
  end
  if M.current then
    items[#items + 1] = {label="Run selected example", detail="Send code and selected testcase"}
    items[#items + 1] = {label="Submit solution", detail="Send code to LeetCode judge"}
    items[#items + 1] = {label="Show testcase", detail="Cycle to the next example"}
    items[#items + 1] = {label="Restore latest submission", detail="Open the latest accepted code"}
    items[#items + 1] = {label="Open problem in browser", detail="Open the official problem page"}
    items[#items + 1] = {label="Show description", detail="Display statement and examples"}
    items[#items + 1] = {label="Change language", detail="Open a solution template in another language"}
    items[#items + 1] = {label="Copy solution to clipboard", detail="Yank the code section"}
    items[#items + 1] = {label="Open judge console", detail="Show the last run or submit output"}
  end
  jot.ui.picker("LeetCode", function() return items end, function(action)
    if action == "Browse problem list" then M.list()
    elseif action == "Daily problem" then M.daily()
    elseif action == "Random problem" then M.random()
    elseif action == "Profile statistics" then M.profile()
    elseif action == "Session status" then M.auth_status()
    elseif action == "Sign out" then M.sign_out()
    elseif action == "Clear question cache" then notify("Removed " .. cache.clear() .. " cached question(s)")
    elseif action == "Refresh active question" and M.current then M.open_question(M.current.title_slug, true)
    elseif action == "Reset solution" then M.reset()
    elseif action == "Show active problem status" then M.status()
    elseif action == "Run selected example" then M.run("run")
    elseif action == "Submit solution" then M.run("submit")
    elseif action == "Show testcase" then M.next_case()
    elseif action == "Restore latest submission" then M.restore_latest()
    elseif action == "Open problem in browser" then M.open_browser()
    elseif action == "Show description" then M.description()
    elseif action == "Change language" then M.change_language()
    elseif action == "Copy solution to clipboard" then M.yank()
    elseif action == "Open judge console" then M.console()
    elseif action == "Switch LeetCode tab" then M.open_tabs() end
  end)
end

function M.open_cookie_prompt()
  local result = jot.leetcode.credential_get()
  if not result.available then notify(result.error); return end
  local value = ""
  local colors = palette()
  local width = 60
  local function prompt_lines()
    return {
      "",
      "Open LeetCode in your browser.",
      "Copy the Cookie header value only.",
      "",
      "Session  " .. string.rep("*", math.min(#value, 48)),
      "",
    }
  end
  local function render()
    local win = M.cookie_win
    local lines = prompt_lines()
    if not win or not jot.ui.float.is_valid(win) then
      local x, y, prompt_width = centered_over_pane(width, 4)
      local buffer = jot.ui.buffer.create(false, true)
      jot.ui.buffer.set_lines(buffer, 0, -1, false, lines)
      win = jot.ui.float.open(buffer, {
        relative="editor", col=x, row=y, width=prompt_width, height=9,
        border="single", focusable=true, zindex=60,
        title="LeetCode sign-in", footer="enter saves · esc cancels",
        fg=colors.fg, bg=colors.bg, border_fg=colors.border,
        title_fg=colors.accent, footer_fg=colors.comment,
      })
      M.cookie_win = win
      -- Highlight the session row like the UI kit's input rows: the full-line
      -- span paints the selection background behind the masked value.
      jot.ui.float.set_spans(win, 5, {{start=0, len=65535, fg=colors.selection_fg, bg=colors.selection_bg}})
      jot.ui.float.on_paste(win, function(text)
        local pasted = tostring(text or ""):gsub(string.char(13), ""):gsub(string.char(10), "")
        value = value .. pasted
        jot.ui.float.set_lines(win, prompt_lines())
      end)
      jot.ui.float.on_key(win, function(event)
        local key = event.key
        if key == 27 then
          value = ""
          jot.ui.float.close(win, true); M.cookie_win=nil; return true
        elseif key == 13 or key == 10 then
          if #value == 0 then notify("Nothing to save yet: paste the Cookie header first"); return true end
          local saved = jot.leetcode.credential_set(value)
          value = ""
          jot.ui.float.close(win, true); M.cookie_win=nil
          if not saved.ok then notify(saved.error); return true end
          notify("Session stored, checking sign-in...")
          M.auth_status(); return true
        elseif key == 127 or key == 8 then
          value = value:sub(1, -2)
        elseif type(key) == "number" and key >= 32 and key <= 126 and not event.ctrl and not event.alt then
          value = value .. string.char(key)
        end
        jot.ui.float.set_lines(win, prompt_lines())
        return true
      end)
    else
      jot.ui.float.set_lines(win, lines)
    end
  end
  render()
end

function M.console()
  -- The judge console is the dock panel's bottom section now; the command
  -- brings that panel up instead of toggling a float.
  M.show_panel()
end

function M.open_tabs()
  if #M.tabs == 0 then notify("No LeetCode questions opened yet"); return end
  local items = {}
  for index, entry in ipairs(M.tabs) do
    local question = entry.question
    items[#items + 1] = {
      label=string.format("%d. %s", index, tostring(question.title or question.title_slug)),
      detail=tostring(entry.language or "") .. (entry.path and (" · " .. entry.path) or ""),
      value=index,
    }
  end
  jot.ui.picker("LeetCode tabs", function() return items end, function(index)
    local entry = M.tabs[tonumber(index) or 0]
    if not entry then return end
    local question = entry.question
    question.lang_slug = entry.language
    question.solution_path = entry.path
    M.current = question
    M.testcases = solution.testcases(question)
    M.testcase = 1
    if entry.path then jot.file.open(entry.path) end
    notify("Switched to " .. tostring(question.title or question.title_slug))
    M.show_panel()
  end)
end

function M.yank()
  if not M.current then notify("Open a question first"); return end
  local language = M.current.lang_slug or config.get("language")
  local path = M.current.solution_path or solution.path(M.current, language)
  local code = jot.file.read(path)
  if not code then notify("Nothing to copy: save the solution file first"); return end
  if not (jot.clipboard and jot.clipboard.set) then notify("Clipboard API is unavailable"); return end
  jot.clipboard.set(solution.submission_code(M.current, language, code))
  notify("Copied " .. path .. " to the clipboard")
end

function M.setup(options)
  config.setup(options)
end

-- Register the dock panel at load time so :LeetRun and the language picker
-- can bring it up; registering twice is harmless (the host replaces the
-- entry), which keeps a runtime reload working.
pcall(panel.register)

M.notify = notify

return M
