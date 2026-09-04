local root = jot.treesitter.runtime_path
local native = jot.treesitter.native
local execute = jot.treesitter.execute
local registry = dofile(root .. "/registry.lua")
local queries = dofile(root .. "/queries.lua")
local highlight = dofile(root .. "/highlight.lua")

registry.register(native)

-- Logs bundled-query failures to stderr (boot logs / headless) and, when
-- running inside the editor, also shows a summary on the message line so the
-- warning does not flash by unnoticed before the UI paints.
local function report_query_failures(query_errors)
  for _, reason in ipairs(query_errors) do
    io.stderr:write("Tree-sitter bundled query skipped (" .. reason
      .. "); runtime queries or regex will be used\n")
  end
  local notify = show_message
  if notify and query_errors[1] then
    local names = {}
    for _, reason in ipairs(query_errors) do
      local lang = reason:match("^([^:]+):")
      names[#names + 1] = lang or reason
    end
    local word = #query_errors == 1 and "query" or "queries"
    pcall(notify, "Tree-sitter: " .. #query_errors .. " bundled highlight " .. word
      .. " skipped (" .. table.concat(names, ", ") .. "); run :tsstatus for details")
  end
end

local query_ok, query_errors = queries.load_all(native, root, registry)
if not query_ok then
  report_query_failures(query_errors)
end
highlight.configure(native)

local M = {}
M.execute = execute
M.register_language = native.register_language
M.language_for_extension = native.language_for_extension
M.status = native.status
M.parser = native.parser
M.parse = native.parse
M.query = native.query
M.captures = native.captures
M.set_query = native.set_query
M.set_capture_color = native.set_capture_color
M.install_command = native.install_command
M.reload = function()
  native.reload()
  registry = dofile(root .. "/registry.lua")
  queries = dofile(root .. "/queries.lua")
  highlight = dofile(root .. "/highlight.lua")
  registry.register(native)
  local query_ok, query_errors = queries.load_all(native, root, registry)
  if not query_ok then
    report_query_failures(query_errors)
  end
  highlight.configure(native)
  M.registry = registry
  M.query_loader = queries
  M.highlight_policy = highlight
end

-- Handle cleanup is explicit and named by resource type.
M.delete_parser = native.delete_parser
M.delete_tree = native.delete_tree
M.delete_query = native.delete_query
M.registry = registry
M.query_loader = queries
M.highlight_policy = highlight
jot.treesitter = M
return M
