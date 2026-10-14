-- Paths and the filesystem, over the file bindings the host already exposes.
--
-- Paths are written with forward slashes: a path handed back by the host on
-- Windows keeps its backslashes, so normalize() and join() read a backslash as
-- a separator and write a slash. Everything that takes a path back (file.open,
-- file.read, the write bindings) takes the slash spelling on every platform.
--
-- The walk in glob() reads one directory at a time through jot.file.list, so it
-- is depth-first, case-sensitive and follows no symlink: what it is for is a
-- feature looking through the workspace for files it knows by name, not a
-- filesystem library.
local M = {}

local function parts(path)
  local out = {}
  for piece in tostring(path):gsub("\\", "/"):gmatch("[^/]+") do
    out[#out + 1] = piece
  end
  return out
end

-- "a", "b", "" -> "a/b"; a part of nil is skipped so an optional argument can
-- be passed through.
function M.join(...)
  local out = {}
  for i = 1, select("#", ...) do
    local piece = select(i, ...)
    if piece ~= nil and tostring(piece) ~= "" then
      out[#out + 1] = tostring(piece):gsub("\\", "/"):gsub("^/+", ""):gsub("/+$", "")
    end
  end
  local text = table.concat(out, "/")
  -- A leading slash (an absolute path) survives: it was trimmed off the first
  -- part above and is put back here exactly once.
  local first = select(1, ...)
  if first and tostring(first):sub(1, 1) == "/" then
    return "/" .. text
  end
  return text
end

-- Collapses //, /./ and /../, and drops a trailing slash. The result is the
-- same path in the shortest spelling, not a path that was checked on disk.
function M.normalize(path)
  local text = tostring(path):gsub("\\", "/")
  local absolute = text:sub(1, 1) == "/"
  local out = {}
  for _, piece in ipairs(parts(text)) do
    if piece == "." then
      -- The directory the walk is already in: nothing to write down.
    elseif piece == ".." and #out > 0 and out[#out] ~= ".." then
      table.remove(out)
    elseif piece ~= ".." or not absolute then
      out[#out + 1] = piece
    end
  end
  local joined = table.concat(out, "/")
  if absolute then
    return "/" .. joined
  end
  return joined == "" and "." or joined
end

function M.is_absolute(path)
  local text = tostring(path):gsub("\\", "/")
  return text:sub(1, 1) == "/" or text:match("^%a:/") ~= nil
end

-- The last piece of a path: "a/b/c.lua" -> "c.lua".
function M.name(path)
  local out = tostring(path):gsub("\\", "/"):gsub("/+$", "")
  return out:match("[^/]*$") or out
end

function M.parent(path)
  local out = tostring(path):gsub("\\", "/"):gsub("/+$", "")
  local head = out:match("^(.*)/[^/]*$")
  if head == nil or head == "" then
    -- A bare name has no directory of its own; the current one is the answer.
    return out:sub(1, 1) == "/" and "/" or "."
  end
  return head
end

-- The text after the last dot of the name, without the dot: "main.cpp" is
-- "cpp", "Makefile" is "".
function M.extension(path)
  return (M.name(path):match("%.([^.]+)$")) or ""
end

local function listing(dir)
  local ok, entries = pcall(jot.file.list, dir)
  if not ok or type(entries) ~= "table" then
    return {}
  end
  return entries
end

local function entry_for(path)
  local wanted = M.name(path)
  if wanted == "" then
    return nil
  end
  for _, entry in ipairs(listing(M.parent(path))) do
    if entry.name == wanted then
      return entry
    end
  end
  return nil
end

function M.exists(path)
  return entry_for(path) ~= nil
end

function M.is_dir(path)
  local entry = entry_for(path)
  return entry ~= nil and entry.dir == true
end

function M.read(path)
  return jot.file.read(path)
end

function M.write(path, text)
  return jot.file.write(path, text)
end

function M.mkdir(path)
  return jot.file.mkdir(path)
end

function M.remove(path)
  return jot.file.remove(path)
end

-- The workspace root, or nil when no directory was opened. It is the base
-- relative() resolves against, and the start of a default glob().
function M.workspace()
  return jot.workspace.path()
end

-- `path` as it reads from `root`, which defaults to the workspace root. A path
-- outside the root comes back normalized instead: "../../etc/passwd" says more
-- than the absolute path does.
function M.relative(path, root)
  local base = M.normalize(root or M.workspace() or "")
  local target = M.normalize(path)
  if base == "" or base == "." then
    return target
  end
  if target == base then
    return "."
  end
  if target:sub(1, #base + 1) == base .. "/" then
    return target:sub(#base + 2)
  end
  return target
end

-- Walks `root` (default: the workspace) and answers the paths whose name
-- matches a Lua pattern, sorted. The pattern is matched against the name, not
-- the whole path, so it reads as `glob("%.cpp$")`; `opts.all` also descends
-- into hidden directories, which are skipped by default. `opts.depth` bounds
-- the walk (default 12) and `opts.limit` the result count (default 500), so a
-- walk over a large tree reports rather than takes the frame.
function M.glob(pattern, root, opts)
  local options = type(opts) == "table" and opts or {}
  local depth_cap = options.depth or 12
  local limit = options.limit or 500
  local out = {}
  if depth_cap <= 0 or limit <= 0 then
    return out
  end

  local function walk(dir, depth)
    if depth > depth_cap or #out >= limit then
      return
    end
    for _, entry in ipairs(listing(dir)) do
      if #out >= limit then
        return
      end
      if entry.dir then
        if not options.files_only then
          if entry.name:match(pattern) then
            out[#out + 1] = entry.path
          end
        end
        -- A hidden directory is skipped rather than walked (.git): the walk is
        -- for source files, and the exclusion is the one every tool makes.
        if not options.all and entry.name:sub(1, 1) ~= "." then
          walk(entry.path, depth + 1)
        end
      elseif entry.name:match(pattern) then
        out[#out + 1] = entry.path
      end
    end
  end

  walk(root or M.workspace() or ".", 1)
  table.sort(out)
  return out
end

return M
