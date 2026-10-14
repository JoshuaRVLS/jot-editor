-- Text helpers: the string forms a feature asks for over and over, the hex
-- spelling of bytes, and a line diff.
--
-- The diff is what any feature that shows changes needs. The undo tree diffs a
-- state against the buffer to list what going back there would bring, and the
-- inline assistant diffs an answer against the selection; both had their own
-- copy of this walk before the library existed.
local M = {}


-- Lines of `text`, without the empty last one a trailing newline leaves. A
-- file that ends with a newline ends with its last line, not with a blank one.
function M.split_lines(text)
  local out = {}
  for line in (tostring(text) .. "\n"):gmatch("(.-)\n") do
    out[#out + 1] = line
  end
  if #out > 1 and out[#out] == "" then
    table.remove(out)
  end
  return out
end

function M.join_lines(lines)
  return table.concat(lines, "\n")
end

-- `text` cut at every `sep`, keeping the empty pieces between two of them.
function M.split(text, sep)
  local out = {}
  text = tostring(text)
  sep = sep or ","
  if sep == "" then
    -- No separator to cut on: one piece per byte, as splitting by an empty
    -- pattern effectively is.
    for i = 1, #text do
      out[#out + 1] = text:sub(i, i)
    end
    return out
  end
  local start = 1
  while true do
    local at = text:find(sep, start, true)
    if not at then
      out[#out + 1] = text:sub(start)
      return out
    end
    out[#out + 1] = text:sub(start, at - 1)
    start = at + #sep
  end
end

function M.trim(text)
  return (tostring(text):gsub("^%s+", ""):gsub("%s+$", ""))
end

function M.starts_with(text, prefix)
  return text:sub(1, #prefix) == prefix
end

function M.ends_with(text, suffix)
  return suffix == "" or text:sub(-#suffix) == suffix
end

function M.hexencode(text)
  return (tostring(text):gsub(".", function(c) return ("%02x"):format(c:byte()) end))
end

-- Returns the bytes a hex string spells, or nil and why it is not hex.
function M.hexdecode(text)
  local clean = tostring(text):gsub("%s", "")
  if #clean % 2 ~= 0 then
    return nil, "odd number of digits"
  end
  local out = {}
  for i = 1, #clean, 2 do
    local byte = tonumber(clean:sub(i, i + 1), 16)
    if not byte then
      return nil, "not a hex pair at " .. i
    end
    out[#out + 1] = string.char(byte)
  end
  return table.concat(out)
end

-- The lines that differ between two lists of lines, oldest first, as
-- `{kind = "same"|"add"|"del", text = line}`.
--
-- The walk is the usual longest-common-subsequence table, which costs one cell
-- per line pair: two files far apart in size are refused with nil and a reason
-- rather than taking a frame (or a session) to answer. `opts.max_cells`
-- overrides the cap.
function M.diff(before, after, opts)
  local a, b = before or {}, after or {}
  local cap = (type(opts) == "table" and opts.max_cells) or 250000
  if #a * #b > cap then
    return nil, ("%d lines against %d is past what a diff is worth"):format(#a, #b)
  end
  local lcs = {}
  for i = 0, #a do
    lcs[i] = {}
  end
  for i = 1, #a do
    local row, above = lcs[i], lcs[i - 1]
    for j = 1, #b do
      if a[i] == b[j] then
        row[j] = (above[j - 1] or 0) + 1
      else
        row[j] = math.max(above[j] or 0, row[j - 1] or 0)
      end
    end
  end

  -- Read the table back from the end: same lines are walked through, an added
  -- or a removed line is emitted on whichever side holds the longer run.
  local reversed = {}
  local i, j = #a, #b
  while i > 0 and j > 0 do
    if a[i] == b[j] then
      reversed[#reversed + 1] = { kind = "same", text = a[i] }
      i, j = i - 1, j - 1
    elseif (lcs[i - 1][j] or 0) > (lcs[i][j - 1] or 0) then
      reversed[#reversed + 1] = { kind = "del", text = a[i] }
      i = i - 1
    else
      reversed[#reversed + 1] = { kind = "add", text = b[j] }
      j = j - 1
    end
  end
  while i > 0 do
    reversed[#reversed + 1] = { kind = "del", text = a[i] }
    i = i - 1
  end
  while j > 0 do
    reversed[#reversed + 1] = { kind = "add", text = b[j] }
    j = j - 1
  end

  local ordered = {}
  for k = #reversed, 1, -1 do
    ordered[#ordered + 1] = reversed[k]
  end
  return ordered
end

return M
