-- npm manager: installs a package into the isolated package dir and links
-- its .bin entries into <root>/bin (mason.nvim's npm manager, simplified).

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "npm")
---@param dirs table { root, dir, bin_dir }
---@return string[] install script lines (POSIX sh)
function M.install_lines(entry, dirs)
  local lines = {
    "npm install --prefix " .. sh_quote(dirs.dir) .. " " .. sh_quote(entry.pkg) .. "@latest",
  }
  for _, b in ipairs(entry.bin) do
    lines[#lines + 1] = "ln -sf " .. sh_quote(dirs.dir .. "/node_modules/.bin/" .. b) .. " "
      .. sh_quote(dirs.bin_dir .. "/" .. b)
  end
  return lines
end

return M