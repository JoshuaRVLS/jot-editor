-- Helpers over lists: a feature that walks lines or entries writes the same
-- three loops in every file otherwise.
--
-- These are functions over a list (or over anything with a `pairs` walk), not
-- a chainable iterator object: every call answers a table, so a caller can
-- hand the result to another function or print it. `each` is the one that takes
-- a raw iterator instead, for a walk a caller does not want materialized.
local M = {}

-- An iterator over a list, a table (every key) or a function already, in the
-- pairs shape: `for _, value in iter.each(source) do`. The list walk answers
-- the index and the value and ends by answering nil first, which is how the
-- generic for knows to stop; a plain `return source[i]` in the value position
-- would loop on the growing index instead.
function M.each(source)
  local kind = type(source)
  if kind == "function" then
    return source
  end
  if kind == "table" then
    if source[1] ~= nil then
      local i = 0
      return function()
        i = i + 1
        local value = source[i]
        if value == nil then
          return nil
        end
        return i, value
      end
    end
    return pairs(source)
  end
  return function()
    return nil
  end
end

-- Everything `source` yields, as a list.
function M.list(source)
  local out = {}
  for _, value in M.each(source) do
    out[#out + 1] = value
  end
  return out
end

function M.map(source, fn)
  local out = {}
  for _, value in M.each(source) do
    out[#out + 1] = fn(value)
  end
  return out
end

function M.filter(source, keep)
  local out = {}
  for _, value in M.each(source) do
    if keep(value) then
      out[#out + 1] = value
    end
  end
  return out
end

function M.count(source, pred)
  local total = 0
  for _, value in M.each(source) do
    if pred == nil or pred(value) then
      total = total + 1
    end
  end
  return total
end

-- The first value the predicate accepts, or nil.
function M.find(source, pred)
  for _, value in M.each(source) do
    if pred(value) then
      return value
    end
  end
  return nil
end

-- `fn` over every value, for the side effect.
function M.for_each(source, fn)
  for _, value in M.each(source) do
    fn(value)
  end
end

-- `fn` over the values from the first one, keeping the running total: a fold,
-- which is what the same three loops in the features turn out to be.
function M.reduce(source, fn, initial)
  local total = initial
  for _, value in M.each(source) do
    total = fn(total, value)
  end
  return total
end

-- The values, sorted, as a new list: the caller's table is left alone and the
-- order can be given as `less`.
function M.sorted(source, less)
  local copy = M.list(source)
  table.sort(copy, less)
  return copy
end

function M.reverse(source)
  local out = {}
  for i = #source, 1, -1 do
    out[#out + 1] = source[i]
  end
  return out
end

-- `from` to `to`, both counting from 1 and both optional: slice(list, 2)
-- drops the first entry, slice(list, -3) keeps the last three.
function M.slice(source, from, to)
  local n = #source
  local first, last = from or 1, to or n
  if first < 0 then
    first = math.max(n + first + 1, 1)
  end
  if last < 0 then
    last = n + last + 1
  end
  local out = {}
  for i = first, math.min(last, n) do
    out[#out + 1] = source[i]
  end
  return out
end

-- The values of several lists, end to end.
function M.concat(...)
  local out = {}
  for i = 1, select("#", ...) do
    local source = select(i, ...)
    if type(source) == "table" then
      for _, value in ipairs(source) do
        out[#out + 1] = value
      end
    end
  end
  return out
end

-- Split text into lines, dropping the empty last one a trailing newline leaves:
-- the same shape `jot.lib.text.split_lines` answers with.
function M.lines(text)
  local out = {}
  for line in (tostring(text) .. "\n"):gmatch("(.-)\n") do
    out[#out + 1] = line
  end
  if #out > 1 and out[#out] == "" then
    table.remove(out)
  end
  return out
end

return M
