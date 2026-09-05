-- LSP installer orchestrator (mason.nvim-inspired).
--
-- The package registry (registry.lua) is pure data; each manager module
-- (managers/*.lua) renders the install shell steps for one package-manager
-- family. This module assembles the steps into a full install script that
-- runs in a silent background job: isolated package dir + symlinked
-- binaries under <root>/bin + a receipt file, so uninstall and status are
-- trivial and the global environment is never touched.
--
-- The native host owns the process lifecycle: it wraps the returned script
-- with its [jot:lsp] start/success/failed markers, spawns it, and polls the
-- log. This module is pure Lua except for `root`/`platform`, which the host
-- sets as globals before loading (jot_lsp_root, jot_lsp_platform).

local registry = dofile(_G.jot_lsp_lua_root .. "/registry.lua")

local managers = {
  npm = dofile(_G.jot_lsp_lua_root .. "/managers/npm.lua"),
  pypi = dofile(_G.jot_lsp_lua_root .. "/managers/pypi.lua"),
  golang = dofile(_G.jot_lsp_lua_root .. "/managers/golang.lua"),
  github = dofile(_G.jot_lsp_lua_root .. "/managers/github.lua"),
}

local M = {}

local ROOT = _G.jot_lsp_root or ""
local PLATFORM = _G.jot_lsp_platform or "linux"

local function sh_quote(v)
  return "'" .. tostring(v):gsub("'", "'\\''") .. "'"
end

-- The entry id doubles as the package dir name, so the native host can check
-- receipt existence without consulting Lua.
local function package_dir(id)
  return ROOT .. "/" .. id
end

---@param name string user-supplied server name / alias
---@return table|nil entry
function M.resolve(name)
  return registry.resolve(name)
end

---@return table[] all entries (id, display, detail)
function M.list()
  local out = {}
  for _, e in ipairs(registry.entries) do
    out[#out + 1] = { id = e.id, display = e.display, detail = e.detail }
  end
  return out
end

---@param name string
---@return boolean
function M.installed(name)
  local entry = registry.resolve(name)
  if not entry then
    return false
  end
  local f = io.open(package_dir(entry.id) .. "/receipt", "r")
  if f then
    f:close()
    return true
  end
  return false
end

-- Full POSIX install script for the entry. Returns nil when the manager is
-- not supported on this platform.
local function build_install_script(entry)
  local dirs = {
    root = ROOT,
    dir = package_dir(entry.id),
    bin_dir = ROOT .. "/bin",
    dl_dir = package_dir(entry.id) .. "/dl",
  }
  local manager = managers[entry.manager]
  if not manager then
    return nil
  end
  local lines = manager.install_lines(entry, dirs, PLATFORM)
  if not lines then
    return nil
  end
  -- set -e makes any failed step abort before the receipt is written, so a
  -- receipt always means a complete install.
  local script = { "set -eu", "mkdir -p " .. sh_quote(dirs.bin_dir), "mkdir -p " .. sh_quote(dirs.dir) }
  for _, l in ipairs(lines) do
    script[#script + 1] = l
  end
  script[#script + 1] = "printf 'name=%s\\n' " .. sh_quote(entry.id) .. " > "
    .. sh_quote(package_dir(entry.id) .. "/receipt")
  return table.concat(script, "\n") .. "\n"
end

local function build_remove_script(entry)
  local lines = {
    "set -u",
  }
  for _, b in ipairs(entry.bin) do
    lines[#lines + 1] = "rm -f " .. sh_quote(ROOT .. "/bin/" .. b)
  end
  lines[#lines + 1] = "rm -rf " .. sh_quote(package_dir(entry.id))
  return table.concat(lines, "\n") .. "\n"
end

---@param name string
---@return table|nil { script = string, message = string }
function M.plan_install(name)
  local entry = registry.resolve(name)
  if not entry then
    return nil
  end
  local base = { id = entry.id }
  if PLATFORM == "win" then
    -- Windows terminals cannot run the POSIX scripts yet: fall back to the
    -- simple global install command when the entry provides one.
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

---@param name string
---@return table|nil { script = string, message = string }
function M.plan_remove(name)
  local entry = registry.resolve(name)
  if not entry then
    return nil
  end
  local base = { id = entry.id }
  if PLATFORM == "win" then
    -- Global installs are removed globally on Windows (mirror of win_cmd).
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

-- Globals the native host calls (see api_lsp_install.cpp). Return shapes are
-- tables so errors stay Lua-side.
jot_lsp_plan_install = M.plan_install
jot_lsp_plan_remove = M.plan_remove
jot_lsp_list = M.list

-- Plugin-facing surface: jot.lsp.installer.*
local jot = _G.jot
if jot then
  if not jot.lsp then
    jot.lsp = {}
  end
  jot.lsp.installer = M
end

return M