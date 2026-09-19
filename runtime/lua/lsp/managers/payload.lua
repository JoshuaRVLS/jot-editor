-- payload manager: the package ships this server under share/jot/payload/<bin>
-- (the release vendors clangd that way), so "installing" it only links the
-- shipped binary into the managed bin dir and writes the receipt. Nothing is
-- downloaded or copied: the link points into the payload tree, which stays
-- read-only and untouched by `:lspremove` (that deletes just the link and the
-- package dir). The host answers jot_lsp_bundled(bin) with the payload
-- directory, or nil when this package carries none -- then install.lua picks
-- the entry's own manager instead of this one (see build_install_script).

local M = {}

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

---@param entry table registry entry (with payload_dir injected by install.lua)
---@param dirs table { root, dir, bin_dir, dl_dir }
---@param platform string "linux" | "mac" | "win" (unused: the payload is local)
function M.install_lines(entry, dirs, platform)
  local payload = entry.payload_dir
  if not payload or payload == "" then
    return nil
  end
  -- The package dir and the bin dir already exist (shared preamble). The
  -- payload is searched like _jot_bin does for an unpacked archive, because a
  -- release archive keeps its own directory level (clangd_22.1.8/bin/clangd).
  local L = {
    "_jot_payload_link() {",
    '  _pat="$1"',
    "  _found=$(find " .. sh_quote(payload)
      .. " \\( -type f -o -type l \\) -name \"$_pat\" 2>/dev/null | head -n1)",
    '  if [ -z "$_found" ]; then',
    "    echo \"payload: $_pat not found under " .. payload .. "\" >&2",
    "    exit 1",
    "  fi",
    '  chmod +x "$_found" 2>/dev/null || true',
    "  ln -sfn \"$_found\" " .. sh_quote(dirs.bin_dir) .. "/$_pat",
    "}",
  }
  for _, b in ipairs(entry.bin or {}) do
    L[#L + 1] = "_jot_payload_link " .. sh_quote(b)
  end
  return L
end

return M
