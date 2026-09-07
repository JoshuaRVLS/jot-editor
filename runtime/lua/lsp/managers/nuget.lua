-- nuget manager: `dotnet tool update --tool-path <dir>` installs a .NET tool
-- into the isolated package dir. The produced apphost carries the same name
-- as the package's bin entry.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "nuget")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local cmd = "dotnet tool update --tool-path " .. sh_quote(dirs.dir)
  if entry.version and entry.version ~= "" then
    cmd = cmd .. " --version " .. sh_quote(entry.version)
  end
  cmd = cmd .. " " .. sh_quote(entry.pkg)
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    if not runs[b] then
      runs[b] = { kind = "", hint = b }
    end
  end
  entry.runs = runs
  return { cmd }
end

return M
