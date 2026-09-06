-- LSP / language-tooling installer orchestrator (mason.nvim-inspired).
--
-- The package catalog (registry.lua) is generated from the mason registry by
-- tools/mason_import.py. Each manager module (managers/*.lua) renders the
-- install shell steps for one package-manager family. This module assembles
-- the steps into one script that runs in a silent background job: isolated
-- package dir + binaries/wrappers under <root>/bin + a receipt file, so
-- uninstall and status stay trivial.
--
-- The native host wraps the returned script with [jot:lsp] markers, spawns
-- it and polls the log. Pure Lua except `jot_lsp_root`/`jot_lsp_platform`
-- which the host sets before loading.

local registry = dofile(_G.jot_lsp_lua_root .. "/registry.lua")

local managers = {}
for _, name in ipairs({ "npm", "pypi", "golang", "cargo", "gem", "nuget",
                        "github", "generic", "openvsx", "luarocks", "composer", "opam" }) do
  managers[name] = dofile(_G.jot_lsp_lua_root .. "/managers/" .. name .. ".lua")
end

local M = {}

local ROOT = _G.jot_lsp_root or ""
local PLATFORM = _G.jot_lsp_platform or "linux"

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

local function package_dir(id)
  return ROOT .. "/" .. id
end

-- Shared POSIX preamble: $PDIR (package dir), $BIN (managed bin dir) and a
-- _jot_bin helper that locates a produced file inside $PDIR and links it (or
-- writes an exec wrapper) under $BIN. `kind` selects the wrapper:
--   "" symlink, jar -> java -jar, node/python/php/ruby/dotnet -> exec runtime,
--   gem -> exec with GEM_HOME/GEM_PATH pinned to the package dir.
-- NOTE: %%s inside the wrapper printf format strings stays %s after the
-- outer string.format below.
local PREAMBLE = [[
set -eu
PDIR='%s'
BIN='%s'
mkdir -p "$PDIR" "$BIN"
_jot_bin() {
  _name="$1"; _kind="$2"; _pat="$3"
  if [ "${_pat#*/}" != "$_pat" ]; then
    _found=$(find "$PDIR" -path "$PDIR/$_pat" 2>/dev/null | head -n1)
  else
    _found=""
  fi
  if [ -z "$_found" ]; then
    _found=$(find "$PDIR" \( -type f -o -type l \) -name "$_pat" 2>/dev/null | head -n1)
  fi
  if [ -z "$_found" ]; then
    echo "install: binary $_name ($_pat) not found under $PDIR" >&2
    exit 1
  fi
  chmod +x "$_found" 2>/dev/null || true
  case "$_kind" in
    # Wrappers bake the concrete path at write time: the generated file is a
    # standalone launcher, so install-script variables are out of scope later.
    jar) printf '#!/bin/sh\nexec java -jar "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    node) printf '#!/bin/sh\nexec node "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    python) printf '#!/bin/sh\nexec python3 "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    php) printf '#!/bin/sh\nexec php "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    ruby) printf '#!/bin/sh\nexec ruby "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    dotnet) printf '#!/bin/sh\nexec dotnet "%%s" "$@"\n' "$_found" > "$BIN/$_name" ;;
    gem) printf '#!/bin/sh\nexec env GEM_HOME=%%s GEM_PATH=%%s "%%s" "$@"\n' "$PDIR" "$PDIR" "$_found" > "$BIN/$_name" ;;
    pypi) if [ -f "$PDIR/bin/$_name" ]; then
      # pip --target console script: needs the package dir importable.
      printf '#!/bin/sh\nexec env PYTHONPATH=%%s python3 "%%s" "$@"\n' "$PDIR" "$PDIR/bin/$_name" > "$BIN/$_name"
    else
      # Compiled/data-file wheel (no console script): link it directly.
      ln -sfn "$_found" "$BIN/$_name"
    fi ;;
    *) ln -sfn "$_found" "$BIN/$_name" ;;
  esac
  chmod +x "$BIN/$_name" 2>/dev/null || true
}
]]

-- Emits a _jot_bin call for every public binary. `runs` (from the catalog)
-- carries {kind, hint} when the binary needs an interpreter; the hint path's
-- basename is what we search for.
local function link_lines(entry)
  local out = {}
  local runs = entry.runs or {}
  for _, b in ipairs(entry.bin or {}) do
    local spec = runs[b]
    local kind = spec and spec.kind or ""
    local hint = (spec and spec.hint ~= "") and spec.hint or b
    local pat = hint:match("([^/]+)$") or b
    out[#out + 1] = ("_jot_bin %s %s %s"):format(sh_quote(b), sh_quote(kind), sh_quote(pat))
  end
  return out
end

local function build_install_script(entry)
  local dir = package_dir(entry.id)
  local bin_dir = ROOT .. "/bin"
  local dirs = { root = ROOT, dir = dir, bin_dir = bin_dir,
                 dl_dir = dir .. "/dl" }
  local manager = managers[entry.manager]
  if not manager or not manager.install_lines then
    return nil
  end
  local lines = manager.install_lines(entry, dirs, PLATFORM)
  if not lines then
    return nil
  end
  local script = {
    PREAMBLE:format(dir, bin_dir),
  }
  for _, l in ipairs(lines) do
    script[#script + 1] = l
  end
  if not entry.no_bin_link then
    for _, l in ipairs(link_lines(entry)) do
      script[#script + 1] = l
    end
  end
  -- A receipt is only written after every step succeeded (set -e).
  script[#script + 1] = "printf 'name=%s\\n' " .. sh_quote(entry.id) .. " > "
    .. sh_quote(dir .. "/receipt")
  return table.concat(script, "\n") .. "\n"
end

local function build_remove_script(entry)
  local lines = { "set -u" }
  for _, b in ipairs(entry.bin or {}) do
    lines[#lines + 1] = "rm -f " .. sh_quote(ROOT .. "/bin/" .. b)
  end
  lines[#lines + 1] = "rm -rf " .. sh_quote(package_dir(entry.id))
  return table.concat(lines, "\n") .. "\n"
end

local function catalog_list()
  local out = {}
  for _, e in ipairs(registry.entries) do
    out[#out + 1] = { id = e.id, display = e.display, detail = e.detail }
  end
  return out
end

---@param name string user-supplied id / alias
function M.plan_install(name)
  local entry = registry.resolve(name)
  if not entry then
    return nil
  end
  local base = { id = entry.id }
  if PLATFORM == "win" then
    if not entry.win_cmd then
      base.script = ""
      base.message = entry.display .. " is not supported by the Windows installer yet"
      return base
    end
    base.script = entry.win_cmd
    base.message = "LSP install started: " .. entry.id
    return base
  end
  local script = build_install_script(entry)
  if not script then
    base.script = ""
    base.message = entry.display .. " is not supported on this platform yet"
    return base
  end
  base.script = script
  base.message = "LSP install started: " .. entry.id
  return base
end

function M.plan_remove(name)
  local entry = registry.resolve(name)
  if not entry then
    return nil
  end
  local base = { id = entry.id }
  if PLATFORM == "win" then
    local remove = entry.win_remove_cmd
    if not remove then
      base.script = ""
      base.message = entry.display .. " has no Windows remove command"
      return base
    end
    base.script = remove
    base.message = "LSP remove started: " .. entry.id
    return base
  end
  base.script = build_remove_script(entry)
  base.message = "LSP remove started: " .. entry.id
  return base
end

-- Globals the native host calls (see api_lsp_install.cpp).
jot_lsp_plan_install = M.plan_install
jot_lsp_plan_remove = M.plan_remove
jot_lsp_list = catalog_list

-- Plugin-facing surface: jot.lsp.installer.*
local jot = _G.jot
if jot then
  if not jot.lsp then
    jot.lsp = {}
  end
  jot.lsp.installer = M
  jot.lsp.installer.list = catalog_list
  jot.lsp.installer.resolve = registry.resolve
end

return M