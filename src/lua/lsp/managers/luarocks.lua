-- luarocks manager: `luarocks install --tree <dir>` installs a rock into the
-- isolated tree; executables land in <dir>/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "luarocks")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local spec = entry.pkg
  if entry.version and entry.version ~= "" then
    spec = spec .. " " .. entry.version
  end
  local line = "luarocks install --tree " .. sh_quote(dirs.dir) .. " " .. sh_quote(spec)
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    if not runs[b] then
      runs[b] = { kind = "", hint = "bin/" .. b }
    end
  end
  entry.runs = runs
  return { line }
end

return M
