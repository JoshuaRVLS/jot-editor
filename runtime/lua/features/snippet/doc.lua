-- Pure document-offset math for the snippet engine.
--
-- The engine tracks every tabstop as a byte range in the buffer. Doing that
-- with (line, column) pairs alone gets messy the moment an edit inserts or
-- removes newlines, so everything is converted to a single flat byte offset
-- over the document's line array; this module owns that conversion plus the
-- range splices. It touches no state and calls no `jot.*` function, so it is
-- the piece the tests drive directly.
local M = {}

-- 1-based line starts from the line array (lines[1] starts at offset 0).
function M.line_starts(lines)
  local starts = {}
  local offset = 0
  for i = 1, #lines do
    starts[i] = offset
    offset = offset + #lines[i] + 1 -- + '\n'
  end
  starts[#lines + 1] = offset
  return starts
end

-- Total document length in bytes including the newline separators.
function M.total(starts, lines)
  if #lines == 0 then
    return 0
  end
  return starts[#lines] + #lines[#lines]
end

-- (1-based line, 1-based col) -> flat offset (0-based).
function M.offset(starts, lines, line, col)
  line = math.max(1, math.min(line or 1, math.max(1, #lines)))
  local base = starts[line] or 0
  local width = #(lines[line] or "")
  col = math.max(1, math.min(col or 1, width + 1))
  return base + col - 1
end

-- Flat offset -> { line, col } (1-based). `starts` must have the sentinel
-- entry appended by line_starts().
function M.position(starts, lines, offset)
  offset = math.max(0, offset or 0)
  local n = #lines
  if n == 0 then
    return { line = 1, col = 1 }
  end
  local lo, hi = 1, n
  while lo < hi do
    local mid = math.floor((lo + hi + 1) / 2)
    if (starts[mid] or 0) <= offset then
      lo = mid
    else
      hi = mid - 1
    end
  end
  local line = lo
  local col = offset - (starts[line] or 0) + 1
  col = math.max(1, math.min(col, #(lines[line] or "") + 1))
  return { line = line, col = col }
end

-- The text between two flat offsets (newlines included).
function M.text_between(starts, lines, from, to)
  if to <= from then
    return ""
  end
  local doc = table.concat(lines, "\n")
  return doc:sub(from + 1, to)
end

-- Replaces [from, to) with `text` and returns the updated document plus the
-- new caret offset (end of the inserted text).
function M.splice(starts, lines, from, to, text)
  local doc = table.concat(lines, "\n")
  local new_doc = doc:sub(1, from) .. (text or "") .. doc:sub(to + 1)
  local new_lines = {}
  local pos = 1
  while true do
    local nl = new_doc:find("\n", pos, true)
    if not nl then
      new_lines[#new_lines + 1] = new_doc:sub(pos)
      break
    end
    new_lines[#new_lines + 1] = new_doc:sub(pos, nl - 1)
    pos = nl + 1
  end
  return new_lines, from + #(text or "")
end

-- Shifts a flat offset through an edit that replaced [from, to) with text of
-- length `inserted`.
function M.shift(offset, from, to, inserted)
  if offset <= from then
    return offset
  end
  if offset >= to then
    return offset + inserted - (to - from)
  end
  -- Inside the replaced window: collapse to the end of the insertion.
  return from + inserted
end

return M
