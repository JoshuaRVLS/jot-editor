-- golang manager: `go install` with GOBIN pointed at the isolated package
-- dir, then the built binary is linked into <root>/bin. Registry pins are
-- already canonical module versions (vX.Y.Z).

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "golang")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local target = entry.pkg
  if entry.version and entry.version ~= "" then
    target = target .. "@" .. entry.version
  else
    target = target .. "@latest"
  end
  local line = "GOBIN=" .. sh_quote(dirs.dir .. "/bin") .. " go install " .. sh_quote(target)
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
