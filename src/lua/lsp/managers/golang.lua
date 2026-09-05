-- golang manager: `go install` with GOBIN pointed at the isolated package
-- dir, then the built binary is linked into <root>/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "golang")
---@param dirs table { root, dir, bin_dir }
---@return string[] install script lines (POSIX sh)
function M.install_lines(entry, dirs)
  local lines = {
    "GOBIN=" .. sh_quote(dirs.dir .. "/bin") .. " go install " .. sh_quote(entry.pkg) .. "@latest",
  }
  for _, b in ipairs(entry.bin) do
    lines[#lines + 1] = "ln -sf " .. sh_quote(dirs.dir .. "/bin/" .. b) .. " "
      .. sh_quote(dirs.bin_dir .. "/" .. b)
  end
  return lines
end

return M