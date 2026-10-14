-- JSON: the one encoder and decoder the bundled features share.
--
-- The decoder is strict about what JSON allows and generous about what a
-- provider actually sends: whitespace anywhere, any key order, and `\u`
-- escapes including the surrogate pairs a reply about emoji contains.
--
-- A decoded `null` is nil by default, which is what a caller reading a stream
-- wants: `if not delta.content then skip end`. A caller that has to tell null
-- from a missing field asks for the sentinel instead, `decode(text, {keep_null
-- = true})`, and compares against `json.null`. The snippet engine keeps its own
-- decoder: it reads VSCode packs, not JSON documents, and is deliberately
-- smaller than this one.
local M = {}

-- The value a decoded `null` comes back as when the caller asks for it.
M.null = setmetatable({}, { __tostring = function() return "null" end })

local ESCAPES = {
  ['"'] = '\\"',
  ["\\"] = "\\\\",
  ["\b"] = "\\b",
  ["\f"] = "\\f",
  ["\n"] = "\\n",
  ["\r"] = "\\r",
  ["\t"] = "\\t",
}

-- A control byte, a quote or a backslash. The range starts at %z rather than a
-- literal \0 because a Lua 5.1 pattern stops at an embedded zero (LuaJIT's
-- matcher reads the pattern as a C string) and %z is that byte -- the same
-- spelling features/leetcode/json.lua uses.
local function quote(s)
  return '"' .. (s:gsub('[%z\1-\31"\\]', function(c)
    return ESCAPES[c] or ("\\u%04x"):format(c:byte())
  end)) .. '"'
end

-- Lua tables hold both lists and maps, and the two encode differently: an
-- empty table is an object, a table with a `1` key is an array (JSON arrays
-- are dense, so one integer key at 1 is what tells them apart).
local function is_array(value)
  if value[1] == nil then
    return false
  end
  local count = 0
  for key in pairs(value) do
    if type(key) ~= "number" then
      return false
    end
    count = count + 1
  end
  return count == #value
end

local function write_number(value, out)
  if value ~= value or value == math.huge or value == -math.huge then
    out[#out + 1] = "null" -- JSON has no NaN or infinity
    return
  end
  if value == math.floor(value) and math.abs(value) < 1e15 then
    out[#out + 1] = ("%d"):format(value)
    return
  end
  out[#out + 1] = ("%.10g"):format(value)
end

local function write_value(value, out)
  local kind = type(value)
  if value == nil or value == M.null then
    out[#out + 1] = "null"
  elseif kind == "boolean" then
    out[#out + 1] = value and "true" or "false"
  elseif kind == "number" then
    write_number(value, out)
  elseif kind == "string" then
    out[#out + 1] = quote(value)
  elseif kind == "table" then
    if is_array(value) then
      out[#out + 1] = "["
      for i = 1, #value do
        if i > 1 then
          out[#out + 1] = ","
        end
        write_value(value[i], out)
      end
      out[#out + 1] = "]"
    else
      out[#out + 1] = "{"
      -- Sorted keys: a request body is easier to read back in a test, and it
      -- nudges every serialisation of the same table to the same bytes.
      local keys = {}
      for key in pairs(value) do
        keys[#keys + 1] = tostring(key)
      end
      table.sort(keys)
      for i, key in ipairs(keys) do
        if i > 1 then
          out[#out + 1] = ","
        end
        out[#out + 1] = quote(key)
        out[#out + 1] = ":"
        -- A key read back by name, or by its number when the table mixed
        -- numeric keys in. `value[key] or fallback` would turn a stored false
        -- into the fallback, which is how `stream = false` reached a provider
        -- as null.
        local stored = value[key]
        if stored == nil then
          stored = value[tonumber(key)]
        end
        write_value(stored, out)
      end
      out[#out + 1] = "}"
    end
  else
    out[#out + 1] = "null"
  end
end

function M.encode(value)
  local out = {}
  write_value(value, out)
  return table.concat(out)
end

-- Decodes one JSON value. Returns (value) or (nil, message).
local function parse(text, keep_null)
  local pos = 1
  local len = #text

  local function fail(what)
    return nil, ("invalid JSON: %s at byte %d"):format(what, pos)
  end

  local function skip_space()
    while pos <= len do
      local c = text:sub(pos, pos)
      if c ~= " " and c ~= "\t" and c ~= "\n" and c ~= "\r" then
        return
      end
      pos = pos + 1
    end
  end

  local parse_value

  local function parse_string()
    -- Opening quote already consumed.
    local out = {}
    while true do
      if pos > len then
        return fail("unterminated string")
      end
      local c = text:sub(pos, pos)
      if c == '"' then
        pos = pos + 1
        return table.concat(out)
      end
      if c == "\\" then
        local esc = text:sub(pos + 1, pos + 1)
        if esc == "u" then
          local hex = text:sub(pos + 2, pos + 5)
          local code = tonumber(hex, 16)
          if not code then
            return fail("bad \\u escape")
          end
          pos = pos + 6
          -- A surrogate pair is two escapes for one character; a lone one is
          -- dropped rather than turned into an invalid byte sequence.
          if code >= 0xD800 and code <= 0xDBFF then
            local low = tonumber(text:sub(pos + 2, pos + 5), 16)
            if low and low >= 0xDC00 and low <= 0xDFFF and text:sub(pos, pos + 1) == "\\u" then
              code = 0x10000 + (code - 0xD800) * 0x400 + (low - 0xDC00)
              pos = pos + 6
            else
              code = 0xFFFD
            end
          elseif code >= 0xDC00 and code <= 0xDFFF then
            code = 0xFFFD
          end
          out[#out + 1] = utf8.char(code)
        else
          local simple = {
            ['"'] = '"',
            ["\\"] = "\\",
            ["/"] = "/",
            b = "\b",
            f = "\f",
            n = "\n",
            r = "\r",
            t = "\t",
          }
          if not simple[esc] then
            return fail("bad escape")
          end
          out[#out + 1] = simple[esc]
          pos = pos + 2
        end
      else
        -- Copy the run up to the next quote or backslash in one go.
        local stop = text:find('["\\]', pos)
        if not stop then
          return fail("unterminated string")
        end
        out[#out + 1] = text:sub(pos, stop - 1)
        pos = stop
      end
    end
  end

  local function parse_number()
    local matched = text:sub(pos):match("^%-?%d+%.?%d*[eE]?[%+%-]?%d*")
    local value = matched and tonumber(matched)
    if not value then
      return fail("bad number")
    end
    pos = pos + #matched
    return value
  end

  local function parse_array()
    pos = pos + 1 -- [
    local out = {}
    -- The length is kept here rather than read back with `#`: a `null` element
    -- is nil, so it leaves a hole, and `#out + 1` then appends *over* the
    -- element before it. A provider sends null (a delta with no content, a
    -- tools array) often enough that a reply would arrive shifted by one.
    local index = 0
    skip_space()
    if text:sub(pos, pos) == "]" then
      pos = pos + 1
      return out
    end
    while true do
      local value, err = parse_value()
      if err then
        return nil, err
      end
      index = index + 1
      out[index] = value
      skip_space()
      local c = text:sub(pos, pos)
      if c == "," then
        pos = pos + 1
        skip_space()
      elseif c == "]" then
        pos = pos + 1
        return out
      else
        return fail("expected , or ]")
      end
    end
  end

  local function parse_object()
    pos = pos + 1 -- {
    local out = {}
    skip_space()
    if text:sub(pos, pos) == "}" then
      pos = pos + 1
      return out
    end
    while true do
      skip_space()
      if text:sub(pos, pos) ~= '"' then
        return fail("expected a key")
      end
      pos = pos + 1
      local key, err = parse_string()
      if err then
        return nil, err
      end
      skip_space()
      if text:sub(pos, pos) ~= ":" then
        return fail("expected :")
      end
      pos = pos + 1
      skip_space()
      local value
      value, err = parse_value()
      if err then
        return nil, err
      end
      out[key] = value
      skip_space()
      local c = text:sub(pos, pos)
      if c == "," then
        pos = pos + 1
        skip_space()
      elseif c == "}" then
        pos = pos + 1
        return out
      else
        return fail("expected , or }")
      end
    end
  end

  parse_value = function()
    skip_space()
    if pos > len then
      return fail("unexpected end of input")
    end
    local c = text:sub(pos, pos)
    if c == "{" then
      return parse_object()
    elseif c == "[" then
      return parse_array()
    elseif c == '"' then
      pos = pos + 1
      return parse_string()
    elseif c == "t" and text:sub(pos, pos + 3) == "true" then
      pos = pos + 4
      return true
    elseif c == "f" and text:sub(pos, pos + 4) == "false" then
      pos = pos + 5
      return false
    elseif c == "n" and text:sub(pos, pos + 3) == "null" then
      pos = pos + 4
      return keep_null and M.null or nil
    elseif c == "-" or c:match("%d") then
      return parse_number()
    end
    return fail("unexpected character '" .. c .. "'")
  end

  local value, err = parse_value()
  if err then
    return nil, err
  end
  -- One document and nothing else: a reply that carries a second one, or text
  -- after the closing brace, is reported instead of read as the first value.
  -- A caller streaming frames decodes one frame per call, so the byte after
  -- the value is the only place that can say the frame was malformed.
  skip_space()
  if pos <= len then
    return fail("trailing data")
  end
  return value
end

-- Decodes one complete JSON document (or one SSE payload, which is the same
-- thing). Returns (value) or (nil, message), and the message starts with
-- "invalid JSON" so a caller can tell a malformed frame from a missing one.
-- Text after the document is an error: the reply is one document, and reading
-- the first value out of a longer body hides what went wrong with the rest.
-- `opts.keep_null` keeps a null as `M.null` instead of folding it into nil.
function M.decode(text, opts)
  if type(text) ~= "string" or text == "" then
    return nil, "empty input"
  end
  local keep_null = type(opts) == "table" and opts.keep_null or false
  local ok, value, err = pcall(parse, text, keep_null)
  if not ok then
    return nil, tostring(value)
  end
  if err then
    return nil, err
  end
  if value == nil then
    -- A bare `null` document: a reply the caller cannot read anything out of.
    return nil, "no value"
  end
  return value
end

return M
