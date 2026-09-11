-- LSP integration.
--
-- Servers answer completions with `insertTextFormat = 2` for snippets;
-- `jot.lsp.register_snippet_handler` hands those to us with the exact replace
-- range, so the body expands through the real engine (tabstops, choices,
-- mirrors, nested snippets, transforms) instead of a plain-text expansion.
-- Returning true consumes the item; false falls back to the native path.
local parser = require("jot_snip.parser")
local session = require("jot_snip.session")
local expand = require("jot_snip.expand")

local M = {}

function M.install()
  jot.lsp.register_snippet_handler(function(item)
    if type(item) ~= "table" or type(item.text) ~= "string" or item.text == "" then
      return false
    end
    local body = parser.parse(item.text)
    if #body == 0 then
      return false
    end
    local filetype = expand.filetype()
    local ctx = expand.context(filetype)
    ctx.snippet_name = "LSP snippet"
    return session.start(body, {
      snippet = { trig = "", name = "LSP snippet" },
      filetype = filetype,
      ctx = ctx,
      start_line = item.start_line,
      start_col = item.start_col,
      end_line = item.end_line,
      end_col = item.end_col,
    })
  end)
end

function M.uninstall()
  pcall(jot.lsp.register_snippet_handler, nil)
end

return M
