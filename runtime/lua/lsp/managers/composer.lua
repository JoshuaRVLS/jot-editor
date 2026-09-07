-- composer manager: installs a PHP package into an isolated composer project
-- dir; executables land in <dir>/vendor/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "composer")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local lines = {
    "mkdir -p " .. sh_quote(dirs.dir),
    "printf '{\"name\": \"jot/install\", \"minimum-stability\": \"dev\", \"prefer-stable\": true}\\n' > "
      .. sh_quote(dirs.dir .. "/composer.json"),
    "composer require --working-dir=" .. sh_quote(dirs.dir) .. " --no-interaction --no-progress "
      .. sh_quote(entry.pkg),
  }
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    if not runs[b] then
      runs[b] = { kind = "", hint = "vendor/bin/" .. b }
    end
  end
  entry.runs = runs
  return lines
end

return M
