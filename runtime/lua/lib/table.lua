-- Table helpers: the shapes a caller has to guess at without them.
--
-- `is_list` is the one worth explaining: Lua has one table type for both lists
-- and records, and the two are told apart by their keys, not by a marker. A
-- table is a list when its keys are exactly 1..#t; an empty table is a record,
-- because an empty list and an empty record are the same table and the JSON
-- encoder has to pick one.
local M = {}

function M.deepcopy(value, seen)
  if type(value) ~= "table" then
    return value
  end
  seen = seen or {}
  if seen[value] then
    return seen[value]
  end
  local out = {}
  seen[value] = out
  for key, item in pairs(value) do
    out[M.deepcopy(key, seen)] = M.deepcopy(item, seen)
  end
  return out
end

function M.is_list(value)
  if type(value) ~= "table" then
    return false
  end
  local count = 0
  for key in pairs(value) do
    if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then
      return false
    end
    count = count + 1
  end
  return count == #value and count > 0
end

-- The keys of a table in a stable order: numbers first and in order, then
-- everything else by its string form. A panel that lists a record reads the
-- same way twice this way.
function M.keys(value)
  local numbers, others = {}, {}
  for key in pairs(value) do
    if type(key) == "number" then
      numbers[#numbers + 1] = key
    else
      others[#others + 1] = key
    end
  end
  table.sort(numbers)
  table.sort(others, function(a, b) return tostring(a) < tostring(b) end)
  for _, key in ipairs(others) do
    numbers[#numbers + 1] = key
  end
  return numbers
end

-- The values, in that same key order.
function M.values(value)
  local out = {}
  for _, key in ipairs(M.keys(value)) do
    out[#out + 1] = value[key]
  end
  return out
end

function M.count(value)
  local total = 0
  for _ in pairs(value) do
    total = total + 1
  end
  return total
end

function M.has(value, key)
  return value[key] ~= nil
end

-- Appends the entries of `source` to `target` and hands `target` back, so a
-- builder can chain.
function M.append(target, source)
  for _, item in ipairs(source or {}) do
    target[#target + 1] = item
  end
  return target
end

-- `defaults` under `value`: the keys `value` does not carry. A key that is
-- there as false stays false, which is why this is not `value[k] or d[k]`.
function M.defaults(value, defaults)
  local out = {}
  for key, item in pairs(defaults) do
    out[key] = item
  end
  for key, item in pairs(value or {}) do
    out[key] = item
  end
  return out
end

return M
