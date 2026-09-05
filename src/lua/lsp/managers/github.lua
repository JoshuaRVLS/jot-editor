-- github manager: downloads the release asset for the current platform. The
-- catalog pins a released version, but tag naming varies between repos
-- (v1.2.3 vs 1.2.3 vs clangd-…), so candidate tags are probed and the asset
-- name is matched against the catalog's per-platform regex instead of being
-- guessed. Extracted binaries are found by basename at link time.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "github")
---@param dirs table { root, dir, bin_dir, dl_dir }
---@param platform string "linux" | "mac" | "win"
function M.install_lines(entry, dirs, platform)
  local spec = entry.asset and entry.asset[platform]
  if not spec then
    return nil
  end
  local repo = entry.repo
  local version = entry.version or ""
  local bare = version:gsub("^v", "")

  -- Candidate release refs: pinned version (common), with/without 'v', then
  -- the repo's latest release as a final fallback.
  local refs = {}
  for _, v in ipairs({ version, bare, "v" .. bare }) do
    if v ~= "" and not refs[v] then
      refs[#refs + 1] = v
    end
  end
  local match = spec.match
  local primary = (entry.bin and entry.bin[1]) or "app"

  local L = {
    "mkdir -p " .. sh_quote(dirs.dl_dir),
    "_asset=''",
    "_tag=''",
    -- _probe <api-path> e.g. releases/tags/v1.2.3 or releases/latest
    "_probe() {",
    '  _r=$(curl -fsSL "https://api.github.com/repos/' .. repo .. '/$1" 2>/dev/null || true)',
    '  if [ -z "$_r" ]; then return 1; fi',
    '  _names=$(printf \'%s\' "$_r" | grep \'"name":\' | sed -E \'s/.*"name": *"([^"]*)".*/\\1/\')',
    "  _cand=$(printf '%s\\n' \"$_names\" | grep -E " .. sh_quote("^" .. match .. "$") .. " || true)",
    '  if [ -z "$_cand" ]; then return 1; fi',
    '  _asset=$(printf \'%s\\n\' "$_cand" | grep -E \'x86_64|x64|amd64|aarch64|arm64\' | head -n1)',
    "  if [ -z \"$_asset\" ]; then _asset=$(printf '%s\\n' \"$_cand\" | head -n1); fi",
    '  _tag=$(printf \'%s\' "$_r" | sed -n \'s/.*"tag_name": *"\\([^"]*\\)".*/\\1/p\' | head -n1)',
    "  return 0",
    "}",
  }

  local probes = {}
  for _, ref in ipairs(refs) do
    probes[#probes + 1] = "_probe releases/tags/" .. ref
  end
  probes[#probes + 1] = "_probe releases/latest"
  L[#L + 1] = table.concat(probes, " && [ -n \"$_asset\" ] || ") .. " || true"
  L[#L + 1] = 'if [ -z "$_asset" ]; then'
  L[#L + 1] = '  echo "github: no release asset for ' .. repo .. ' matching ' .. match .. '" >&2'
  L[#L + 1] = "  exit 1"
  L[#L + 1] = "fi"
  L[#L + 1] = "curl -fsSL -o " .. sh_quote(dirs.dl_dir .. "/$_asset")
    .. " \"https://github.com/" .. repo .. "/releases/download/$_tag/$_asset\""

  local archive = spec.archive
  local runs = entry.runs or {}
  if archive == "zip" then
    L[#L + 1] = "(cd " .. sh_quote(dirs.dir) .. " && unzip -oq " .. sh_quote(dirs.dl_dir .. "/$_asset") .. ")"
    L[#L + 1] = "rm -f " .. sh_quote(dirs.dl_dir .. "/$_asset")
  elseif archive == "tar.gz" or archive == "tar" then
    L[#L + 1] = "(cd " .. sh_quote(dirs.dir) .. " && tar -xzf " .. sh_quote(dirs.dl_dir .. "/$_asset") .. ")"
    L[#L + 1] = "rm -f " .. sh_quote(dirs.dl_dir .. "/$_asset")
  elseif archive == "gz" then
    L[#L + 1] = "gzip -dc " .. sh_quote(dirs.dl_dir .. "/$_asset") .. " > "
      .. sh_quote(dirs.dir .. "/" .. primary)
    L[#L + 1] = "rm -f " .. sh_quote(dirs.dl_dir .. "/$_asset")
    L[#L + 1] = "chmod +x " .. sh_quote(dirs.dir .. "/" .. primary)
  else
    -- Direct file (jar / phar / binary): keep it under the public bin name so
    -- the linker can find it by basename.
    L[#L + 1] = "mv -f " .. sh_quote(dirs.dl_dir .. "/$_asset") .. " " .. sh_quote(dirs.dir .. "/" .. primary)
    L[#L + 1] = "chmod +x " .. sh_quote(dirs.dir .. "/" .. primary) .. " 2>/dev/null || true"
    for _, b in ipairs(entry.bin or {}) do
      local rs = runs[b]
      if rs and rs.hint and rs.hint:find("{", 1, true) then
        runs[b] = { kind = rs.kind, hint = b }
      end
    end
    entry.runs = runs
  end

  return L
end

return M
