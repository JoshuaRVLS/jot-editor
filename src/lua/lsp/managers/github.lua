-- github manager: resolves the latest release tag of a repo, downloads the
-- platform asset (asset `match` may embed {tag} for versioned filenames),
-- extracts it (zip / tar.gz / raw .gz binary) and links the executable into
-- <root>/bin. Mirrors mason.nvim's github manager.

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (manager = "github")
---@param dirs table { root, dir, bin_dir, dl_dir }
---@param platform string "linux" | "mac" | "win"
---@return string[] install script lines (POSIX sh)
function M.install_lines(entry, dirs, platform)
  local spec = entry.asset and entry.asset[platform]
  if not spec then
    return nil
  end
  local repo = entry.repo
  local bin_name = entry.bin[1]
  local lines = {
    "mkdir -p " .. sh_quote(dirs.dl_dir),
    -- Resolve the latest release tag from the GitHub API.
    "TAG=$(curl -fsSL " .. sh_quote("https://api.github.com/repos/" .. repo .. "/releases/latest")
      .. " | sed -n 's/.*\"tag_name\": *\"\\([^\"]*\\)\".*/\\1/p' | head -1)",
    "if [ -z \"$TAG\" ]; then echo 'github: could not resolve latest release for " .. repo
      .. "'; exit 1; fi",
    -- Asset names may embed the tag (e.g. lua-language-server-<tag>-linux-x64.tar.gz).
    "ASSET=$(printf '%s' " .. sh_quote(spec.match) .. " | sed \"s/{tag}/$TAG/g\")",
    "curl -fL -o " .. sh_quote(dirs.dl_dir .. "/$ASSET") .. " "
      .. sh_quote("https://github.com/" .. repo .. "/releases/download/$TAG/$ASSET")
      .. " || { echo 'github: download failed for ' \"$ASSET\"; exit 1; }",
  }
  if spec.archive == "zip" then
    lines[#lines + 1] = "(cd " .. sh_quote(dirs.dir) .. " && unzip -oq " .. sh_quote(dirs.dl_dir .. "/$ASSET")
      .. ")"
  elseif spec.archive == "tar.gz" then
    lines[#lines + 1] = "(cd " .. sh_quote(dirs.dir) .. " && tar -xzf " .. sh_quote(dirs.dl_dir .. "/$ASSET")
      .. ")"
  else -- raw gzipped binary
    lines[#lines + 1] = "gzip -dc " .. sh_quote(dirs.dl_dir .. "/$ASSET") .. " > "
      .. sh_quote(dirs.dir .. "/" .. spec.bin)
  end
  lines[#lines + 1] = "chmod +x " .. sh_quote(dirs.dir .. "/" .. spec.bin) .. " 2>/dev/null || true"
  lines[#lines + 1] = "ln -sf " .. sh_quote(dirs.dir .. "/" .. spec.bin) .. " "
    .. sh_quote(dirs.bin_dir .. "/" .. bin_name)
  return lines
end

return M