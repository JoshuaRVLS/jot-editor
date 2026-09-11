-- Minimal JSON decoder.
--
-- Only what snippet packs need: VSCode `.code-snippets` / `.json` files and
-- `package.json` manifests. Kept dependency-free on purpose — there is no
-- JSON binding in the Lua runtime and adding one for two file formats would
-- be the wrong trade.
local M = {}

local escapes = {
  ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f",
  n = "\n", r = "\r", t = "\t",
}

local function skip_space(text, i)
  local _, last = text:find("^[ \t\r\n]*", i)
  return (last or i - 1) + 1
end

local parse_value

local function parse_string(text, i)
  i = i + 1 -- opening quote
  local out = {}
  while i <= #text do
    local c = text:sub(i, i)
    if c == '"' then
      return table.concat(out), i + 1
    elseif c == "\\" then
      local nxt = text:sub(i + 1, i + 1)
      if nxt == "u" then
        local code = tonumber(text:sub(i + 2, i + 5), 16) or 0
        i = i + 6
        if code >= 0xD800 and code <= 0xDBFF then
          local low = tonumber(text:sub(i + 2, i + 5), 16) or 0
          if text:sub(i, i + 1) == "\\u" and low >= 0xDC00 then
            code = 0x10000 + (code - 0xD800) * 0x400 + (low - 0xDC00)
            i = i + 6
          end
        end
        out[#out + 1] = utf8.char(code)
      else
        out[#out + 1] = escapes[nxt] or nxt
        i = i + 2
      end
    else
      out[#out + 1] = c
      i = i + 1
    end
  end
  return nil, i
end

local function parse_number(text, i)
  local s, e = text:find("^%-?%d+%.?%d*[eE]?[%+%-]?%d*", i)
  if not s then
    return nil, i
  end
  return tonumber(text:sub(s, e)), e + 1
end

parse_value = function(text, i)
  i = skip_space(text, i)
  local c = text:sub(i, i)
  if c == "{" then
    local out = {}
    i = skip_space(text, i + 1)
    if text:sub(i, i) == "}" then
      return out, i + 1
    end
    while true do
      local key
      key, i = parse_string(text, skip_space(text, i))
      if key == nil then
        return nil, i
      end
      i = skip_space(text, i)
      if text:sub(i, i) ~= ":" then
        return nil, i
      end
      local value
      value, i = parse_value(text, i + 1)
      out[key] = value
      i = skip_space(text, i)
      local sep = text:sub(i, i)
      if sep == "," then
        i = i + 1
      elseif sep == "}" then
        return out, i + 1
      else
        return nil, i
      end
    end
  elseif c == "[" then
    local out = {}
    i = skip_space(text, i + 1)
    if text:sub(i, i) == "]" then
      return out, i + 1
    end
    while true do
      local value
      value, i = parse_value(text, i)
      out[#out + 1] = value
      i = skip_space(text, i)
      local sep = text:sub(i, i)
      if sep == "," then
        i = i + 1
      elseif sep == "]" then
        return out, i + 1
      else
        return nil, i
      end
    end
  elseif c == '"' then
    return parse_string(text, i)
  elseif text:sub(i, i + 3) == "true" then
    return true, i + 4
  elseif text:sub(i, i + 4) == "false" then
    return false, i + 5
  elseif text:sub(i, i + 3) == "null" then
    return nil, i + 4
  end
  return parse_number(text, i)
end

-- decode(text) -> value | nil, error
function M.decode(text)
  if type(text) ~= "string" then
    return nil, "not a string"
  end
  local ok, value = pcall(function()
    local parsed = parse_value(text, 1)
    return parsed
  end)
  if not ok then
    return nil, "invalid json"
  end
  return value
end

return M
