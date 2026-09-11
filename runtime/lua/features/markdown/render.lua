-- Render orchestration: resolve the option set, run the preprocessor hook,
-- parse the document and wrap it in the page template. The Lua feature only
-- ever calls into this module, so the parser and the page shell stay
-- replaceable independently.
local block = require("jot_md.block")
local template = require("jot_md.template")
local config = require("jot_md.config")

local M = {}

-- Snapshot of the resolved option table (booleans for every switch).
function M.options()
  local options = {}
  for name in pairs(config.defaults_options) do
    options[name] = config.option(name)
  end
  return options
end

-- Renders `text` for the file described by `meta` ({ path, name }).
-- Returns { html, body, toc, title, options }.
function M.render(text, meta)
  meta = meta or {}
  local options = M.options()

  local preprocessor = config.get("preprocessor")
  if type(preprocessor) == "function" then
    local ok, processed = pcall(preprocessor, text, meta.path)
    if ok and type(processed) == "string" then
      text = processed
    end
  end

  local prefix = config.get("images_path")
  if prefix == "" then
    prefix = nil
  end

  local parsed = block.render(text or "", {
    options = options,
    images_prefix = prefix,
  })

  local title = config.get("page_title")
  if not title or title == "" then
    title = parsed.title or meta.name or "Markdown Preview"
  end

  local page = template.document({
    title = title,
    theme = config.get("theme"),
    body_html = parsed.html,
    toc_html = parsed.toc,
    options = options,
    custom_css = config.get("custom_css"),
  })

  return {
    html = page,
    body = parsed.html,
    toc = parsed.toc,
    title = title,
    options = options,
  }
end

return M
