-- pypi manager: pip installs into the isolated package dir (console scripts
-- land in <dir>/bin) and those entries are linked into <root>/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "pypi")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local spec = entry.pkg
  local extra = (entry.extras or {}).extra
  if extra and extra ~= "" then
    spec = spec .. "[" .. extra .. "]"
  end
  if entry.version and entry.version ~= "" then
    spec = spec .. "==" .. entry.version
  end
  local line = "python3 -m pip install --quiet --disable-pip-version-check --target "
    .. sh_quote(dirs.dir) .. " " .. sh_quote(spec)
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
