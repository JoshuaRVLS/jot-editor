-- A readable form of any Lua value, for a message or a panel that has to show a
-- table it cannot lay out itself.
--
-- A table that holds itself is noted rather than followed, and nesting stops at
-- `opts.depth` (3 by default) with a count of what is left below it: a feature
-- printing a decoded reply should not be able to hang the frame.
local M = {}

local function key_order(a, b)
  if type(a) == type(b) and type(a) == "number" then
    return a < b
  end
  if type(a) == type(b) then
    return tostring(a) < tostring(b)
  end
  return tostring(type(a)) < tostring(type(b))
end

local function write(value, out, depth, seen, limit)
  local kind = type(value)
  if kind == "string" then
    out[#out + 1] = ("%q"):format(value)
    return
  end
  if kind ~= "table" then
    out[#out + 1] = tostring(value)
    return
  end
  if seen[value] then
    out[#out + 1] = "<cycle>"
    return
  end
  if depth <= 0 then
    out[#out + 1] = "{...}"
    return
  end
  seen[value] = true

  local keys = {}
  for key in pairs(value) do
    keys[#keys + 1] = key
  end
  table.sort(keys, key_order)

  out[#out + 1] = "{"
  for index, key in ipairs(keys) do
    if index > 1 then
      out[#out + 1] = ", "
    end
    if index > limit then
      out[#out + 1] = ("... %d more"):format(#keys - limit)
      break
    end
    if type(key) == "string" and key:match("^[%a_][%w_]*$") then
      out[#out + 1] = key .. " = "
    else
      out[#out + 1] = "[" .. tostring(key) .. "] = "
    end
    write(value[key], out, depth - 1, seen, limit)
  end
  out[#out + 1] = "}"
  seen[value] = nil
end

function M.inspect(value, opts)
  local depth = (type(opts) == "table" and opts.depth) or 3
  local limit = (type(opts) == "table" and opts.limit) or 40
  local out = {}
  write(value, out, depth, {}, limit)
  return table.concat(out)
end

return M
