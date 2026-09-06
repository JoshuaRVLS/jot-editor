-- Web / full-stack LSP attach policy (Lua-first; see api_lsp_install.cpp).
--
-- The native LSP engine decides the *primary* server for a file from its
-- generated extension table. This module answers the follow-up question:
-- "which additional servers should attach to the same buffer?" so one file
-- can be served by typescript + tailwind + eslint together, exactly like a
-- VS Code workspace.
--
-- Everything here is policy: edit this file (or register your own with
-- jot.lsp.policy.set_override) to change web behavior without touching C++.

local jot = _G.jot

local M = {}

-- Config escape hatch: assign a function taking (primary, filepath) and
-- returning a list of { server, bin, args } tables to take over attach
-- completely. Used by tests and power users.
local override = nil
M.set_override = function(fn)
  override = fn
end

-- Files a secondary server can meaningfully serve, keyed by extension.
local TAILWIND_FILES = {
  js = true, jsx = true, ts = true, tsx = true, mts = true, cts = true,
  css = true, scss = true, less = true, html = true,
}
local ESLINT_FILES = {
  js = true, jsx = true, ts = true, tsx = true, mjs = true, cjs = true,
}

-- Extra servers only attach when their project actually uses them, so a
-- lone .js file never drags tailwind/eslint in. Launch args default to
-- stdio for the vscode-family servers.
local EXTRA_ARGS = {
  ["tailwindcss-language-server"] = { "--stdio" },
}

local function extension(filepath)
  return (filepath:match("%.([%w]+)$") or ""):lower()
end

local function file_exists(path)
  local f = io.open(path, "r")
  if not f then
    return false
  end
  f:close()
  return true
end

local function dirname(path)
  local dir = path:match("^(.*)[/\\][^/\\]*$")
  return dir or "."
end

local function is_absolute(path)
  return path:match("^/") ~= nil or path:match("^[A-Za-z]:[/\\]") ~= nil
end

-- Walk up from the file looking for package.json / .git, mirroring the
-- native workspace-root search. Returns the root dir or nil.
local function project_root(filepath)
  if not filepath or filepath == "" then
    return nil
  end
  local dir = dirname(filepath)
  if not is_absolute(filepath) then
    dir = dirname("./" .. filepath)
  end
  for _ = 1, 32 do
    if file_exists(dir .. "/package.json") or file_exists(dir .. "/.git") then
      return dir
    end
    local parent = dirname(dir)
    if parent == dir then
      return nil
    end
    dir = parent
  end
  return nil
end

local function file_mentions(path, token)
  local f = io.open(path, "r")
  if not f then
    return false
  end
  local text = f:read("*a")
  f:close()
  if not text then
    return false
  end
  -- Cheap, forgiving check: the config JSON key (plus -config files, which
  -- are matched by filename below instead of content).
  return text:find('"' .. token .. '"', 1, true) ~= nil
end

local function has_tailwind(root)
  if file_exists(root .. "/tailwind.config.js") or file_exists(root .. "/tailwind.config.cjs")
     or file_exists(root .. "/tailwind.config.mjs") or file_exists(root .. "/tailwind.config.ts")
     or file_exists(root .. "/tailwind.config.cts") then
    return true
  end
  return file_mentions(root .. "/package.json", "tailwindcss")
end

local function has_eslint(root)
  for _, name in ipairs({
    "eslint.config.js", "eslint.config.mjs", "eslint.config.cjs",
    ".eslintrc", ".eslintrc.json", ".eslintrc.js", ".eslintrc.cjs", ".eslintrc.yml",
  }) do
    if file_exists(root .. "/" .. name) then
      return true
    end
  end
  return file_mentions(root .. "/package.json", "eslint")
end

local function extras_for(primary, filepath)
  if primary ~= "typescript" and primary ~= "css" and primary ~= "html" then
    -- json files: tailwind config JSON gets its own server; nothing extra yet.
    if primary ~= "json" then
      return {}
    end
  end
  local ext = extension(filepath)
  local root = project_root(filepath)
  if not root then
    return {}
  end

  local out = {}
  if TAILWIND_FILES[ext] and has_tailwind(root)
     and jot and jot.lsp and jot.lsp.installed and jot.lsp.installed("tailwindcss-language-server") then
    out[#out + 1] = {
      server = "tailwindcss-language-server",
      bin = "tailwindcss-language-server",
      args = EXTRA_ARGS["tailwindcss-language-server"] or {},
    }
  end
  if ESLINT_FILES[ext] and has_eslint(root)
     and jot and jot.lsp and jot.lsp.installed and jot.lsp.installed("eslint-lsp") then
    out[#out + 1] = {
      server = "eslint-lsp",
      bin = "eslint-lsp",
      args = {},
    }
  end
  return out
end

-- One-shot toolkit presets consumed by `:lspinstall web` / `:tsinstall web`.
local PRESETS = {
  web = {
    lsp = { "typescript", "html", "css", "json" },
    parsers = { "javascript", "typescript", "tsx", "html", "css", "json" },
  },
}

local function preset(name)
  local cfg = PRESETS[name]
  if not cfg then
    return {}
  end
  local out = {}
  for _, id in ipairs(cfg.lsp) do
    out[#out + 1] = { kind = "lsp", name = id }
  end
  for _, id in ipairs(cfg.parsers) do
    out[#out + 1] = { kind = "parser", name = id }
  end
  return out
end

-- Register where the native host and plugins find us.
if jot then
  if not jot.lsp then
    jot.lsp = {}
  end
  jot.lsp.policy = {
    extra_servers = function(primary, filepath)
      if override then
        local custom = override(primary, filepath)
        return custom or {}
      end
      return extras_for(primary or "", filepath or "")
    end,
    preset = preset,
    set_override = M.set_override,
  }
end

return M
