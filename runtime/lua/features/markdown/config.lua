-- Markdown preview configuration.
--
-- Every scalar option is stored under the `markdown_preview_*` keys of the
-- normal jot config store, so `:settings`, `config.lua` and `jot.md.setup{}`
-- all edit the same thing and edits apply live. Complex values (the
-- preprocessor function, the nested preview option table) live on the module.
local M = {}

M.defaults = {
  auto_start = false,          -- open the preview when a markdown buffer opens
  auto_close = true,           -- close it when the last markdown buffer closes
  refresh_interval = 100,      -- ms between preview->editor scroll syncs
  markdown_ext = "markdown,md,mkd,mkdn,mdx,markdown.mdx",
  port = 0,                    -- 0 = pick a free port
  host = "127.0.0.1",
  theme = "dark",              -- dark | light | auto
  page_title = "",             -- empty = derive from the file name
  browser = "",                -- "", firefox, chromium, google-chrome, brave, ...
  echo_preview_url = false,    -- show the preview URL in the message area
  custom_css = "",             -- extra CSS injected after the built-in sheet
  images_path = "",            -- prefix for relative image paths
  open_timeout_ms = 4000,      -- how long to wait for the browser to open
}

-- The markdown-it-plugin equivalent switches. Off by default except the ones
-- that only change how the page renders (no extra network fetches).
M.defaults_options = {
  highlightjs = true,     -- highlight.js code blocks (CDN, when a fence has a language)
  katex = false,          -- $...$ / $$...$$ math
  mermaid = false,        -- ```mermaid diagrams
  plantuml = false,       -- ```plantuml diagrams (encoded to the PlantUML server)
  flowchart = false,     -- ```flow / ```flowchart diagrams
  echarts = false,        -- ```echarts charts
  vega = false,           -- ```vega / ```vega-lite charts
  toc = false,            -- floating table-of-contents panel
  emoji = false,          -- :smile: shortcodes
  source_map = true,      -- data-line anchors that drive scroll sync
  code_copy = true,       -- per-code-block copy button
}

-- Options that only ever live in the module (functions / tables).
M.runtime = {
  preprocessor = nil, -- function(text, path) -> text
}

local function config_key(name)
  return "markdown_preview_" .. name
end

-- Reads one option: config store first (live + `:settings`-visible), then the
-- built-in default.
function M.get(name)
  if name == "preprocessor" then
    return M.runtime.preprocessor
  end
  local default = M.defaults[name]
  if type(default) == "boolean" then
    return jot.config.get_bool(config_key(name), default)
  elseif type(default) == "number" then
    return jot.config.get_number(config_key(name), default)
  end
  return jot.config.get(config_key(name), default)
end

function M.option(name)
  local default = M.defaults_options[name]
  return jot.config.get_bool("markdown_preview_option_" .. name, default == true)
end

-- Applies a `jot.md.setup{ ... }` table. Scalars go into the config store;
-- `preprocessor` and `options` stay on the module.
function M.setup(opts)
  if type(opts) ~= "table" then
    return
  end
  for key, value in pairs(opts) do
    if key == "preprocessor" then
      M.runtime.preprocessor = type(value) == "function" and value or nil
    elseif key == "options" and type(value) == "table" then
      for oname, ovalue in pairs(value) do
        if M.defaults_options[oname] ~= nil then
          jot.config.set("markdown_preview_option_" .. oname, ovalue and true or false)
        end
      end
    elseif M.defaults[key] ~= nil then
      jot.config.set(config_key(key), value)
    end
  end
end

-- True when `path` carries a configured markdown extension.
function M.is_markdown_path(path)
  if not path or path == "" then
    return false
  end
  local ext = path:match("%.([%w_%-%.]+)$")
  if not ext then
    return false
  end
  ext = ext:lower()
  for candidate in tostring(M.get("markdown_ext")):gmatch("[^,%s]+") do
    candidate = candidate:lower()
    if ext == candidate or path:lower():sub(-#candidate - 1) == "." .. candidate then
      return true
    end
  end
  return false
end

return M
