-- Base64, standard alphabet and padding, so what is encoded here is what any
-- other tool decodes.
local M = {}

local ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local INDEX = {}
for i = 1, #ALPHABET do
  INDEX[ALPHABET:sub(i, i)] = i - 1
end

function M.encode(text)
  local bytes = { tostring(text):byte(1, #tostring(text)) }
  local out = {}
  for i = 1, #bytes, 3 do
    local a, b, c = bytes[i], bytes[i + 1], bytes[i + 2]
    local triple = a * 65536 + (b or 0) * 256 + (c or 0)
    local cells = {
      math.floor(triple / 262144) % 64,
      math.floor(triple / 4096) % 64,
      math.floor(triple / 64) % 64,
      triple % 64,
    }
    out[#out + 1] = ALPHABET:sub(cells[1] + 1, cells[1] + 1)
    out[#out + 1] = ALPHABET:sub(cells[2] + 1, cells[2] + 1)
    out[#out + 1] = b and ALPHABET:sub(cells[3] + 1, cells[3] + 1) or "="
    out[#out + 1] = c and ALPHABET:sub(cells[4] + 1, cells[4] + 1) or "="
  end
  return table.concat(out)
end

-- Returns the decoded text, or nil and why the input is not base64.
function M.decode(text)
  local clean = tostring(text):gsub("%s", "")
  if #clean % 4 ~= 0 then
    return nil, "length is not a multiple of 4"
  end
  local out = {}
  for i = 1, #clean, 4 do
    local chunk = clean:sub(i, i + 3)
    local pad = 0
    if chunk:sub(4, 4) == "=" then pad = 1 end
    if chunk:sub(3, 3) == "=" then pad = 2 end
    local triple = 0
    for j = 1, 4 - pad do
      local value = INDEX[chunk:sub(j, j)]
      if not value then
        return nil, "not a base64 character at " .. j
      end
      triple = triple * 64 + value
    end
    -- A padded group holds fewer than 24 bits: shift the value back up so the
    -- byte boundaries are the triple's.
    for _ = 1, pad do
      triple = triple * 64
    end
    local first = math.floor(triple / 65536) % 256
    local second = math.floor(triple / 256) % 256
    local third = triple % 256
    out[#out + 1] = string.char(first)
    if pad < 2 then out[#out + 1] = string.char(second) end
    if pad < 1 then out[#out + 1] = string.char(third) end
  end
  return table.concat(out)
end

return M
