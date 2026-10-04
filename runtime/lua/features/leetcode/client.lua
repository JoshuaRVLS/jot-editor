local json = require("jot_lc.json")
local config = require("jot_lc.config")
local cache = require("jot_lc.cache")
local M = {}

M.pending_requests = 0
M.feedback = nil

-- A request that fails silently leaves the user guessing whether anything
-- happened, so failures are reported here. Successful requests are not: the
-- live status segment (ui.lua) already shows the work in flight, and a toast
-- per request was noise the user asked to drop. ui.lua wires this to the
-- message line.
function M.set_feedback(handler)
  M.feedback = handler
end

function M.pending()
  return M.pending_requests
end

local function report(text)
  if type(M.feedback) == "function" then M.feedback(text) end
end

local function label(method, url)
  return method .. " " .. tostring(url):gsub("^https?://[^/]+", "")
end

M.queries = {
  auth = [[query globalData { userStatus { id: userId name: username is_signed_in: isSignedIn is_premium: isPremium is_verified: isVerified } }]],
  daily = [[query questionOfToday { today: activeDailyCodingChallengeQuestion { question { title_slug: titleSlug } } }]],
  random = [[query randomQuestion($categorySlug: String, $filters: QuestionListFilterInput) { question: randomQuestion(categorySlug: $categorySlug, filters: $filters) { title_slug: titleSlug paid_only: isPaidOnly } }]],
  list = [[query problemsetQuestionList($categorySlug: String, $limit: Int, $skip: Int, $filters: QuestionListFilterInput) { problemsetQuestionList: questionList(categorySlug: $categorySlug, limit: $limit, skip: $skip, filters: $filters) { total: totalNum questions: data { frontend_id: questionFrontendId title title_slug: titleSlug difficulty status paid_only: isPaidOnly topic_tags: topicTags { name slug } } } }]],
  tags = [[query questionTopicTags { topicTags: questionTopicTags { edges { node { name slug } } } }]],
  companies = [[query companyTags { companyTags { name slug } }]],
  favorite_lists = [[query favoriteLists { myCreatedFavoriteList { favorites { name slug } } }]],
  favorite_questions = [[query favoriteQuestionList($favoriteSlug: String!, $limit: Int, $skip: Int) { favoriteQuestionList(favoriteSlug: $favoriteSlug, limit: $limit, skip: $skip) { total: totalLength has_more: hasMore questions { frontend_id: questionFrontendId title title_slug: titleSlug difficulty status paid_only: paidOnly topic_tags: topicTags { name slug } } } }]],
  question = [[query questionData($titleSlug: String!) { question(titleSlug: $titleSlug) { id: questionId frontend_id: questionFrontendId title title_slug: titleSlug is_paid_only: isPaidOnly difficulty status content code_snippets: codeSnippets { lang lang_slug: langSlug code } testcase_list: exampleTestcaseList stats topic_tags: topicTags { name slug } hints } }]],
  profile = [[query userStats($username: String!) { matchedUser(username: $username) { username submit_stats: submitStatsGlobal { acSubmissionNum { difficulty count } } } }]],
}

local function host()
  return config.get("endpoint") == "leetcode.cn" and "leetcode.cn" or "leetcode.com"
end

local function base_url()
  local base = config.get("api_base")
  if type(base) == "string" and base ~= "" then return base:gsub("/+$", "") end
  return "https://" .. host()
end

local function official_url(url)
  return url:match("^https://leetcode%.com/") ~= nil
         or url:match("^https://leetcode%.cn/") ~= nil
end

local function session_cookie()
  local result = jot.leetcode.credential_get()
  if not result.ok then return nil, result.error ~= "" and result.error or "sign in first" end
  if result.value == "" then return nil, "sign in first" end
  return result.value
end

-- Favorites are per-user, but the list query is public and simply comes back
-- empty without a session. Sending it anonymously when nothing is stored keeps
-- a signed-out user on a custom api_base from seeing a domain refusal instead
-- of the plain "sign in to see your favorites" answer.
local function has_session()
  local result = jot.leetcode.credential_get()
  return result.ok and result.available and result.value ~= ""
end

local function request(method, url, body, authenticated, callback)
  local headers = {
    {name="Content-Type", value="application/json"},
    {name="Accept", value="application/json"},
    {name="Referer", value=base_url() .. "/"},
    {name="Origin", value=base_url() .. "/"},
    {name="User-Agent", value="Mozilla/5.0"},
  }
  local target = label(method, url)
  if authenticated then
    if not official_url(url) then
      report("<! " .. target .. " blocked: credentials are restricted to official LeetCode domains")
      callback(nil, "session credentials are restricted to official LeetCode domains")
      return false
    end
    local cookie, err = session_cookie()
    if not cookie then
      report("<! " .. target .. " stopped: " .. tostring(err))
      callback(nil, err)
      return false
    end
    if not cookie:find("LEETCODE_SESSION=", 1, true) then cookie = "LEETCODE_SESSION=" .. cookie end
    headers[#headers + 1] = {name="Cookie", value=cookie}
    local csrf = cookie:match("csrftoken=([^;]+)")
    if csrf then headers[#headers + 1] = {name="x-csrftoken", value=csrf} end
  end
  M.pending_requests = M.pending_requests + 1
  local queued, err = jot.leetcode.request({
    method=method,
    url=url,
    timeout=config.get("timeout"),
    headers=headers,
    body=body or "",
  }, function(response)
    M.pending_requests = math.max(0, M.pending_requests - 1)
    if not response.ok then
      report("<- " .. target .. " failed: "
             .. (response.error ~= "" and response.error
                 or ("HTTP " .. tostring(response.status))))
      callback(nil, response.error ~= "" and response.error or ("HTTP " .. tostring(response.status)))
      return
    end
    local payload, decode_error = json.decode(response.body)
    if not payload then callback(nil, decode_error); return end
    if payload.errors and payload.errors[1] then
      callback(nil, payload.errors[1].message or "LeetCode API error")
      return
    end
    callback(payload)
  end)
  if not queued then
    M.pending_requests = math.max(0, M.pending_requests - 1)
    report("<! " .. target .. " not queued: " .. tostring(err))
    callback(nil, err or "request could not be queued")
    return false
  end
  return true
end

local function graphql(query, variables, authenticated, callback)
  return request("POST", base_url() .. "/graphql/",
                 json.encode({query=query, variables=variables or {}}), authenticated,
                 function(response, err)
                   if err then callback(nil, err); return end
                   callback(response.data)
                 end)
end

local function rest_url(path)
  return base_url() .. path
end

local function url_encode(text)
  return (tostring(text):gsub("([^%w%-_%.~])", function(char)
    return ("%%%02X"):format(char:byte())
  end))
end

-- Upstream reports progress as ac/notac/null; the reference filter vocabulary
-- is solved/attempted/todo, so normalize here once for the list and for the
-- single-question fetch the dock's completed marker reads.
local function normalize_status(status)
  if status == json.null or status == nil then return "todo" end
  if status == "ac" then return "solved" end
  if status == "notac" then return "attempted" end
  return status
end

function M.auth(callback)
  return graphql(M.queries.auth, {}, true, function(data, err)
    if err then callback(nil, err); return end
    local user = data and data.userStatus
    if not user or not user.is_signed_in or user.id == json.null then
      callback(nil, "session is not signed in"); return
    end
    if not user.is_verified then callback(nil, "verify the LeetCode account email first"); return end
    callback(user)
  end)
end

function M.question(slug, callback, refresh)
  if not refresh then
    local cached = cache.get(slug)
    if cached then
      -- Entries cached before the status field existed (or by an older build)
      -- read as todo rather than as a stale answer.
      cached.status = normalize_status(cached.status)
      callback(cached, nil, true)
      return true
    end
  end
  return graphql(M.queries.question, {titleSlug=slug}, false, function(data, err)
    local question = data and data.question
    if not question then callback(nil, err or "question not found"); return end
    question.status = normalize_status(question.status)
    cache.put(slug, question)
    callback(question, nil, false)
  end)
end

function M.daily(callback)
  return graphql(M.queries.daily, {}, false, function(data, err)
    local record = data and data.today
    local slug = record and record.question and record.question.title_slug
    if not slug then callback(nil, err or "daily question unavailable"); return end
    M.question(slug, callback)
  end)
end

function M.random(callback, filters)
  -- filters is required by the resolver even when empty; omitting the variable
  -- fails with "resolve_question_list() missing ... 'filters'" from the API.
  local variables = {categorySlug="all-code-essentials", filters=filters or {}}
  return graphql(M.queries.random, variables, false, function(data, err)
    local question = data and data.question
    if not question or not question.title_slug then callback(nil, err or "random question unavailable"); return end
    M.question(question.title_slug, callback)
  end)
end

-- One page of the problem set. The REST dump the reference caches is already
-- several megabytes; decoding it with the bundled pure-Lua JSON parser froze
-- the editor for seconds, so the paginated GraphQL query is the list path and
-- the filters are applied upstream. "all-code-essentials" is the category slug
-- the site itself uses now ("algorithms" is gone from this query).
function M.list(filters, skip, callback)
  local variables = {categorySlug="all-code-essentials", limit=config.get("list_limit"),
                     skip=skip or 0, filters=filters or {}}
  return graphql(M.queries.list, variables, false, function(data, err)
    if err then callback(nil, nil, err); return end
    local page = data and data.problemsetQuestionList
    local questions = page and page.questions
    if type(questions) ~= "table" then callback(nil, nil, err or "problem list unavailable"); return end
    local rows = {}
    for _, item in ipairs(questions) do
      local status = normalize_status(item.status)
      rows[#rows + 1] = {
        id=item.frontend_id,
        frontend_id=item.frontend_id,
        title=item.title,
        title_slug=item.title_slug,
        difficulty=item.difficulty or "Unknown",
        status=status,
        paid_only=item.paid_only == true,
        topic_tags=item.topic_tags,
      }
    end
    callback(rows, page.total or #rows, nil)
  end)
end

-- The tag and company pickers fetch their whole choice list in one public
-- query (the live API returns all 75 topics and all companies unpaginated).
function M.tags(callback)
  return graphql(M.queries.tags, {}, false, function(data, err)
    if err then callback(nil, err); return end
    local rows = {}
    for _, edge in ipairs((data and data.topicTags and data.topicTags.edges) or {}) do
      local node = edge.node or {}
      if node.slug then rows[#rows + 1] = {name=node.name or node.slug, slug=node.slug} end
    end
    callback(rows, nil)
  end)
end

function M.companies(callback)
  return graphql(M.queries.companies, {}, false, function(data, err)
    if err then callback(nil, err); return end
    local rows = {}
    for _, company in ipairs((data and data.companyTags) or {}) do
      if company.slug then rows[#rows + 1] = {name=company.name or company.slug, slug=company.slug} end
    end
    callback(rows, nil)
  end)
end

function M.favorite_lists(callback)
  return graphql(M.queries.favorite_lists, {}, has_session(), function(data, err)
    if err then callback(nil, err); return end
    local lists = data and data.myCreatedFavoriteList and data.myCreatedFavoriteList.favorites
    local rows = {}
    for _, list in ipairs(lists or {}) do
      if list.slug then rows[#rows + 1] = {name=list.name or list.slug, slug=list.slug} end
    end
    callback(rows, nil)
  end)
end

-- One page of a favorite list's questions, shaped like M.list rows so the
-- picker renders and paginates it the same way.
function M.favorite_questions(slug, skip, callback)
  local variables = {favoriteSlug=slug, limit=config.get("list_limit"), skip=skip or 0}
  return graphql(M.queries.favorite_questions, variables, has_session(), function(data, err)
    if err then callback(nil, nil, err); return end
    local page = data and data.favoriteQuestionList
    local questions = page and page.questions
    if type(questions) ~= "table" then callback(nil, nil, err or "favorite list unavailable"); return end
    local rows = {}
    for _, item in ipairs(questions) do
      rows[#rows + 1] = {
        id=item.frontend_id,
        frontend_id=item.frontend_id,
        title=item.title,
        title_slug=item.title_slug,
        difficulty=item.difficulty or "Unknown",
        status=normalize_status(item.status),
        paid_only=item.paid_only == true,
        topic_tags=item.topic_tags,
      }
    end
    callback(rows, page.total or #rows, nil)
  end)
end

function M.profile(callback)
  M.auth(function(user, err)
    if err then callback(nil, err); return end
    graphql(M.queries.profile, {username=user.name}, true, function(data, request_error)
      callback(data and data.matchedUser, request_error)
    end)
  end)
  return true
end

function M.latest(question, callback)
  local lang = question.lang_slug or config.get("language")
  local path = "/submissions/latest/?qid=" .. url_encode(question.id) .. "&lang=" .. url_encode(lang)
  return request("GET", rest_url(path), "", true, function(response, err)
    if err then callback(nil, err); return end
    callback(response, nil)
  end)
end

function M.judge(kind, question, code, testcase, callback)
  local submit = kind == "submit"
  local path = "/problems/" .. question.title_slug .. (submit and "/submit/" or "/interpret_solution/")
  local data = {lang=question.lang_slug, typed_code=code, question_id=question.id}
  if not submit then data.data_input = testcase or "" end
  request("POST", rest_url(path), json.encode(data), true, function(response, err)
    if err then callback(nil, err); return end
    local id = submit and response.submission_id or response.interpret_id
    if not id then callback(nil, response.msg or "judge did not return a request id"); return end
    local attempts = 0
    local function poll()
      attempts = attempts + 1
      local check_path = "/submissions/detail/" .. url_encode(id) .. "/check/"
      request("GET", rest_url(check_path), "", true, function(result, poll_error)
        if poll_error then callback(nil, poll_error); return end
        if result.status_code then callback(result); return end
        if attempts >= 20 then callback(nil, "judge polling timed out"); return end
        jot.timer.set_timeout(math.min(450 * attempts, 1500), poll)
      end)
    end
    poll()
  end)
  return true
end

return M
