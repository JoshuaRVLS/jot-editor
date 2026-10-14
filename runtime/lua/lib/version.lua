-- Release numbers, for the two questions a feature asks about one: is the
-- running build new enough for me, and is this language server or tool older
-- than the release I need.
--
-- The grammar is the one tools actually print: "1.2.3", "v1.2", "1.2.3-rc.1",
-- "1.2.3+build.4", and a version with fewer than three numbers ("0.10" from a
-- language server) counts as if the missing ones were zero. A prerelease sorts
-- before its release, so "1.0.0-rc1" < "1.0.0", which is what a version
-- comparison is for.
local M = {}

local function pieces(text)
  local out = {}
  for piece in tostring(text):gmatch("[^%.]+") do
    out[#out + 1] = piece
  end
  return out
end

-- A version as a table, or nil and why the text is not one. `major`, `minor`
-- and `patch` are always there; `prerelease` is the string after the first
-- dash (nil when there is none), and `build` the metadata after a plus, which
-- is kept but never compared.
function M.parse(text)
  if type(text) ~= "string" or text == "" then
    return nil, "not a version string"
  end
  local body = text:gsub("^[vV]", "")
  local build
  local plus = body:find("+", 1, true)
  if plus then
    build = body:sub(plus + 1)
    body = body:sub(1, plus - 1)
  end
  local prerelease
  local dash = body:find("-", 1, true)
  if dash then
    prerelease = body:sub(dash + 1)
    body = body:sub(1, dash - 1)
  end
  local fields = pieces(body)
  local numbers = {}
  for index, field in ipairs(fields) do
    local number = tonumber(field)
    if not number or number < 0 or number % 1 ~= 0 then
      return nil, ("%q is not a number"):format(field)
    end
    numbers[index] = number
  end
  if #numbers == 0 then
    return nil, "no numbers in it"
  end
  return {
    major = numbers[1] or 0,
    minor = numbers[2] or 0,
    patch = numbers[3] or 0,
    prerelease = prerelease,
    build = build,
    text = text,
  }
end

-- The list form of parse(), for a version that has already been parsed.
local function normalize(value)
  if type(value) == "string" then
    return M.parse(value)
  end
  return value
end

-- Prerelease tags in the order semver orders them: a numeric field compares as
-- a number, a word as text, and a prerelease always loses to the release.
local function compare_prerelease(a, b)
  if a == nil and b == nil then
    return 0
  end
  if a == nil then
    return 1
  end
  if b == nil then
    return -1
  end
  local left, right = pieces(a), pieces(b)
  for i = 1, math.max(#left, #right) do
    local x, y = left[i], right[i]
    if x == nil then
      return -1
    end
    if y == nil then
      return 1
    end
    local xn, yn = tonumber(x), tonumber(y)
    if xn and yn then
      if xn ~= yn then
        return xn < yn and -1 or 1
      end
    elseif x ~= y then
      return x < y and -1 or 1
    end
  end
  return 0
end

-- -1, 0 or 1. A string is accepted on either side; an unparsable one is not
-- comparable, which raises rather than picking an order.
function M.compare(a, b)
  local left, right = normalize(a), normalize(b)
  if not left or not right then
    error("compare needs two version strings", 2)
  end
  for _, field in ipairs({ "major", "minor", "patch" }) do
    if left[field] ~= right[field] then
      return left[field] < right[field] and -1 or 1
    end
  end
  return compare_prerelease(left.prerelease, right.prerelease)
end

function M.eq(a, b)
  return M.compare(a, b) == 0
end

function M.lt(a, b)
  return M.compare(a, b) < 0
end

function M.le(a, b)
  return M.compare(a, b) <= 0
end

function M.gt(a, b)
  return M.compare(a, b) > 0
end

function M.ge(a, b)
  return M.compare(a, b) >= 0
end

-- "1.2" -> "1.2.0": the shortest spelling that names every field it has. A
-- parsed version keeps the text it came from, so this is what a caller prints
-- when it built one by hand.
function M.tostring(value)
  local version = normalize(value)
  if not version then
    return tostring(value)
  end
  local out = ("%d.%d.%d"):format(version.major, version.minor, version.patch)
  if version.prerelease then
    out = out .. "-" .. version.prerelease
  end
  return out
end

-- Whether `value` falls in a range written the way a dependency spec is:
-- ">=1.2.0", "1.2.0 - 2.0.0", ">=1.2 <2.0.0", "^1.2" (same major) or "~1.2"
-- (same minor). Returns nil and why when the spec is not one of those.
function M.range(spec, value)
  local version = normalize(value)
  if not version then
    return nil, "not a version string"
  end
  local text = tostring(spec):gsub("^%s+", ""):gsub("%s+$", "")
  if text == "" or text == "*" then
    return true
  end

  -- "1.2.0 - 2.0.0": both ends included.
  local low, high = text:match("^([^%-%s]+)%s+%-%s+([^%s]+)$")
  if low and high then
    return M.ge(version, low) and M.le(version, high)
  end

  -- The operator is read off the front by hand: Lua patterns have no
  -- alternation, so there is no `^(>=|<=|>)` to write. The order matters, which
  -- is why ">=" is tested before ">".
  local OPERATORS = { ">=", "<=", "^", "~", ">", "<", "=" }
  local all = true
  for clause in text:gmatch("[^%s]+") do
    local op, number = "", clause
    for _, candidate in ipairs(OPERATORS) do
      if clause:sub(1, #candidate) == candidate then
        op, number = candidate, clause:sub(#candidate + 1)
        break
      end
    end
    local bound = normalize(number)
    if not bound then
      return nil, ("%q is not a version"):format(number)
    end
    local holds
    if op == ">=" then
      holds = M.ge(version, bound)
    elseif op == "<=" then
      holds = M.le(version, bound)
    elseif op == ">" then
      holds = M.gt(version, bound)
    elseif op == "<" then
      holds = M.lt(version, bound)
    elseif op == "^" then
      holds = version.major == bound.major and M.ge(version, bound)
    elseif op == "~" then
      holds = version.major == bound.major and version.minor == bound.minor
          and M.ge(version, bound)
    else
      holds = M.eq(version, bound)
    end
    all = all and holds
  end
  return all
end

return M
