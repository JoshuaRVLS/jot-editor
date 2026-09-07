-- opam manager: opam has no isolated install; the package is installed into
-- the active switch and a wrapper for each public bin is placed under
-- <root>/bin pointing at `opam exec` so the current switch is used.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "opam")
---@param dirs table { root, dir, bin_dir, dl_dir }
function M.install_lines(entry, dirs)
  local spec = entry.pkg
  if entry.version and entry.version ~= "" then
    spec = spec .. "." .. entry.version
  end
  entry.no_bin_link = true
  local lines = { "mkdir -p " .. sh_quote(dirs.dir),
                  "opam install --yes --no-depext " .. sh_quote(spec) }
  for _, b in ipairs(entry.bin or {}) do
    lines[#lines + 1] = "printf '#!/bin/sh\\nexec opam exec -- " .. b .. " \"$@\"\\n' > "
      .. sh_quote(dirs.bin_dir .. "/" .. b)
    lines[#lines + 1] = "chmod +x " .. sh_quote(dirs.bin_dir .. "/" .. b)
  end
  return lines
end

return M
