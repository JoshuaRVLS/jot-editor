-- utf8 for a runtime that does not ship it.
--
-- LuaJIT is Lua 5.1 plus the JIT, so it has no utf8 library -- and the bundled
-- features read text by codepoint in nine files (cell widths, word wrap, toast
-- truncation, JSON escapes). Rather than wrap every one of those call sites in
-- a runtime check, the Lua host loads this file when the engine it booted on
-- does not provide utf8 itself (install_bundled_utf8, lua/api/api_internal.h),
-- so bundled code writes utf8.* either way and gets the C library on a stock
-- build.
--
-- The surface and the outcomes are Lua 5.4's, because a feature that walks
-- malformed bytes has to behave the same on both engines: an invalid sequence
-- makes codes / codepoint error, len reports nil plus the byte offset where it
-- stopped, and offset reports nil for a character that is not in the subject.
-- Overlong forms, surrogates and codepoints above U+10FFFF are invalid here
-- for the same reason they are invalid there, while char still writes the old
-- five- and six-byte forms for wide integers, exactly as that library does.
--
-- Source for the semantics: Lua 5.4's lutf8lib.c (utf8_decode, utf8_len,
-- byteoffset, iter_aux, pushutfchar).
local M = {}

local byte, schar, concat = string.byte, string.char, table.concat
local floor, type, select = math.floor, type, select
-- 5.1 already unpacks through the global; 5.4 only through the table.
local unpack = table.unpack or unpack

-- Lua 5.4's charpattern, verbatim: one lead byte, then its continuations.
M.charpattern = "[\0-\x7F\xC2-\xFD][\x80-\xBF]*"

-- The byte at 0-based position `pos` (string.byte is 1-based), or nil past the
-- end. Every walk below is written 0-based so it can be read next to the C it
-- stands in for.
local function byte_at(s, pos)
  return byte(s, pos + 1)
end

-- True when the byte at 0-based `pos` continues a sequence.
local function is_cont(s, pos)
  local b = byte_at(s, pos)
  return b ~= nil and b >= 0x80 and b <= 0xBF
end

-- Decodes one character starting at 1-based `i` (the caller has checked that
-- the byte is there). Returns the codepoint and its length in bytes, or nil for
-- a sequence Lua 5.4 would reject.
local function decode(s, i)
  local b1 = byte(s, i)
  if b1 < 0x80 then
    return b1, 1
  end
  if b1 >= 0xC2 and b1 <= 0xDF then
    local b2 = byte(s, i + 1)
    if b2 == nil or b2 < 0x80 or b2 > 0xBF then
      return nil
    end
    return (b1 - 0xC0) * 0x40 + (b2 - 0x80), 2
  end
  if b1 >= 0xE0 and b1 <= 0xEF then
    local b2, b3 = byte(s, i + 1), byte(s, i + 2)
    if b2 == nil or b2 < 0x80 or b2 > 0xBF or b3 == nil or b3 < 0x80 or b3 > 0xBF then
      return nil
    end
    local cp = (b1 - 0xE0) * 0x1000 + (b2 - 0x80) * 0x40 + (b3 - 0x80)
    -- Overlong, or a surrogate half: not a character.
    if cp < 0x800 or (cp >= 0xD800 and cp <= 0xDFFF) then
      return nil
    end
    return cp, 3
  end
  if b1 >= 0xF0 and b1 <= 0xF4 then
    local b2, b3, b4 = byte(s, i + 1), byte(s, i + 2), byte(s, i + 3)
    if b2 == nil or b2 < 0x80 or b2 > 0xBF
        or b3 == nil or b3 < 0x80 or b3 > 0xBF
        or b4 == nil or b4 < 0x80 or b4 > 0xBF then
      return nil
    end
    local cp = (b1 - 0xF0) * 0x40000 + (b2 - 0x80) * 0x1000 + (b3 - 0x80) * 0x40 + (b4 - 0x80)
    if cp < 0x10000 or cp > 0x10FFFF then
      return nil
    end
    return cp, 4
  end
  return nil
end

-- One character's bytes. `arg` is the caller's argument number, so the range
-- error points at the value that was out of range.
local function encode(cp, arg)
  if cp < 0x80 then
    return schar(cp)
  end
  if cp < 0x800 then
    return schar(0xC0 + floor(cp / 0x40), 0x80 + cp % 0x40)
  end
  if cp < 0x10000 then
    return schar(0xE0 + floor(cp / 0x1000), 0x80 + floor(cp / 0x40) % 0x40, 0x80 + cp % 0x40)
  end
  -- Four bytes hold 21 bits, not 0x10FFFF: the encoder writes those bits and
  -- leaves validity to the decoder, which is how Lua 5.4's utf8.char behaves
  -- for the values no character has.
  if cp < 0x200000 then
    return schar(0xF0 + floor(cp / 0x40000),
                 0x80 + floor(cp / 0x1000) % 0x40,
                 0x80 + floor(cp / 0x40) % 0x40,
                 0x80 + cp % 0x40)
  end
  -- The five- and six-byte forms, kept for what they always were: the encoding
  -- of wide integer values in a Lua string. Lua 5.4's utf8.char writes them
  -- (up to 0x7FFFFFFF) and its decoder rejects them, and so does this one.
  if cp < 0x4000000 then
    return schar(0xF8 + floor(cp / 0x1000000),
                 0x80 + floor(cp / 0x40000) % 0x40,
                 0x80 + floor(cp / 0x1000) % 0x40,
                 0x80 + floor(cp / 0x40) % 0x40,
                 0x80 + cp % 0x40)
  end
  if cp <= 0x7FFFFFFF then
    return schar(0xFC + floor(cp / 0x40000000),
                 0x80 + floor(cp / 0x1000000) % 0x40,
                 0x80 + floor(cp / 0x40000) % 0x40,
                 0x80 + floor(cp / 0x1000) % 0x40,
                 0x80 + floor(cp / 0x40) % 0x40,
                 0x80 + cp % 0x40)
  end
  error("bad argument #" .. arg .. " to 'char' (value out of range)", 3)
end

-- The string argument every function here takes first.
local function check_string(s, name)
  if type(s) ~= "string" then
    -- 5.4 coerces a number, and that is worth keeping: a Lua caller that hands
    -- a count straight to utf8.len should not have to tostring it.
    if type(s) == "number" then
      return tostring(s)
    end
    error("bad argument #1 to '" .. name .. "' (string expected, got " .. type(s) .. ")", 3)
  end
  return s
end

-- A byte position argument: negative counts from the end, as in 5.4.
local function relative(pos, len)
  if pos < 0 then
    return len + pos + 1
  end
  return pos
end

function M.len(s, i, j)
  s = check_string(s, "len")
  local len = #s
  i = relative(i or 1, len)
  if i < 1 or i > len + 1 then
    error("bad argument #2 to 'len' (initial position out of bounds)", 2)
  end
  j = relative(j or -1, len)
  if j > len then
    error("bad argument #3 to 'len' (final position out of bounds)", 2)
  end
  local count = 0
  local pos = i
  while pos <= j do
    local _, size = decode(s, pos)
    if size == nil then
      -- Text that is not UTF-8: how far the count got, and no number. The
      -- caller that cares about the position gets it.
      return nil, pos
    end
    pos = pos + size
    count = count + 1
  end
  return count
end

function M.codepoint(s, i, j)
  s = check_string(s, "codepoint")
  local len = #s
  i = relative(i or 1, len)
  if i < 1 then
    error("bad argument #2 to 'codepoint' (out of bounds)", 2)
  end
  j = relative(j or i, len)
  if j > len then
    error("bad argument #3 to 'codepoint' (out of bounds)", 2)
  end
  local out = {}
  local pos = i
  while pos <= j do
    local cp, size = decode(s, pos)
    if cp == nil then
      error("invalid UTF-8 code", 2)
    end
    out[#out + 1] = cp
    pos = pos + size
  end
  return unpack(out)
end

-- Lua 5.4's algorithm, 0-based internally: `posi` walks the bytes and `n`
-- counts the characters left to cross, so `n == 0` at the end means the
-- position was found.
function M.offset(s, n, i)
  s = check_string(s, "offset")
  local len = #s
  if type(n) ~= "number" or n ~= floor(n) then
    error("bad argument #2 to 'offset' (number expected, got " .. type(n) .. ")", 2)
  end
  local posi = n >= 0 and 1 or (len + 1)
  if i ~= nil then
    if type(i) ~= "number" or i ~= floor(i) then
      error("bad argument #3 to 'offset' (number expected, got " .. type(i) .. ")", 2)
    end
    posi = relative(i, len)
  end
  if posi < 1 or posi > len + 1 then
    error("bad argument #3 to 'offset' (position out of bounds)", 2)
  end
  posi = posi - 1
  if n == 0 then
    -- The special case: the start of the character byte `posi` sits in.
    while posi > 0 and is_cont(s, posi) do
      posi = posi - 1
    end
  else
    if is_cont(s, posi) then
      error("initial position is a continuation byte", 2)
    end
    if n < 0 then
      while n < 0 and posi > 0 do
        repeat
          posi = posi - 1
        until posi == 0 or not is_cont(s, posi)
        n = n + 1
      end
    else
      n = n - 1
      while n > 0 and posi < len do
        posi = posi + 1
        while is_cont(s, posi) do
          posi = posi + 1
        end
        n = n - 1
      end
    end
  end
  if n ~= 0 then
    -- Neither in the subject nor right after its end.
    return nil
  end
  return posi + 1
end

-- 5.4's three-value return: the iterator, the string as the loop state, and
-- the control value it is first called with. `pos` is the 1-based start of the
-- character just read, so -1 lands on the next character's first byte (or on
-- byte 0 for the first call) and the continuation bytes are skipped from there.
function M.codes(s)
  s = check_string(s, "codes")
  if is_cont(s, 0) then
    -- 5.4 rejects this up front rather than walking into a continuation byte.
    error("bad argument #1 to 'codes' (invalid UTF-8 code)", 2)
  end
  return function(state, pos)
    local len = #state
    local n = (pos or 0) - 1
    if n < 0 then
      n = 0
    elseif n < len then
      n = n + 1
      while is_cont(state, n) do
        n = n + 1
      end
    end
    if n >= len then
      return nil
    end
    local cp = decode(state, n + 1)
    if cp == nil then
      error("invalid UTF-8 code", 2)
    end
    return n + 1, cp
  end, s, 0
end

function M.char(...)
  local n = select("#", ...)
  local out = {}
  for k = 1, n do
    local value = select(k, ...)
    local cp = tonumber(value)
    if cp == nil then
      error("bad argument #" .. k .. " to 'char' (number expected, got " .. type(value) .. ")", 2)
    end
    if cp ~= floor(cp) then
      error("bad argument #" .. k .. " to 'char' (number has no integer representation)", 2)
    end
    if cp < 0 or cp > 0x7FFFFFFF then
      error("bad argument #" .. k .. " to 'char' (value out of range)", 2)
    end
    out[#out + 1] = encode(cp, k)
  end
  return concat(out)
end

-- The host loads this file through the bundled-file loader, which drops the
-- chunk's result, so the library is installed here as well as returned; the
-- callers reach for the global.
_G.utf8 = M
return M
