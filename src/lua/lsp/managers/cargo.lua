-- cargo manager: `cargo install --root <dir>` builds the crate into the
-- isolated package dir; binaries land in <dir>/bin.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "cargo")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local extras = entry.extras or {}
  local features = extras.features
  local git = extras.repository_url
  local cmd = "cargo install --root " .. sh_quote(dirs.dir) .. " --locked"
  if git and git ~= "" then
    cmd = cmd .. " --git " .. sh_quote(git)
  else
    if entry.version and entry.version ~= "" then
      cmd = cmd .. " --version " .. sh_quote(entry.version)
    end
    if features and features ~= "" then
      cmd = cmd .. " --features " .. sh_quote(features)
    end
    cmd = cmd .. " " .. sh_quote(entry.pkg)
  end
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    if not runs[b] then
      runs[b] = { kind = "", hint = "bin/" .. b }
    end
  end
  entry.runs = runs
  return { cmd }
end

return M
