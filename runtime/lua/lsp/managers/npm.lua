-- npm manager: installs a package into the isolated package dir with pinned
-- version (mason registry pins CI-verified releases) and optional extra
-- packages.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "npm")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local spec = entry.pkg
  if entry.version and entry.version ~= "" then
    spec = spec .. "@" .. entry.version
  else
    spec = spec .. "@latest"
  end
  local line = "npm install --prefix " .. sh_quote(dirs.dir) .. " " .. sh_quote(spec)
  for _, extra in ipairs(entry.extra_pkgs or {}) do
    line = line .. " " .. sh_quote(extra)
  end
  -- Public bins live under node_modules/.bin inside the isolated prefix.
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    if not runs[b] then
      runs[b] = { kind = "", hint = "node_modules/.bin/" .. b }
    end
  end
  entry.runs = runs
  return { line }
end

return M
