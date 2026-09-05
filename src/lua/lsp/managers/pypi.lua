-- pypi manager: pip installs into the isolated package dir (console scripts
-- land in <dir>/bin) and those entries are linked into <root>/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "pypi")
---@param dirs table { root, dir, bin_dir }
---@return string[] install script lines (POSIX sh)
function M.install_lines(entry, dirs)
  local lines = {
    "python3 -m pip install --target " .. sh_quote(dirs.dir) .. " " .. sh_quote(entry.pkg),
  }
  for _, b in ipairs(entry.bin) do
    lines[#lines + 1] = "ln -sf " .. sh_quote(dirs.dir .. "/bin/" .. b) .. " "
      .. sh_quote(dirs.bin_dir .. "/" .. b)
  end
  return lines
end

return M