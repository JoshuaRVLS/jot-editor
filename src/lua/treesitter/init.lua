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

-- The bundled highlight queries are NOT loaded at boot. Loading one means
-- dlopening its parser (when installed) and compiling the query, which for
-- large grammars costs tens of milliseconds per language, all before the
-- first frame. Everything above -- language registration, extension mapping,
-- module surface -- is cheap and stays on the boot path so plugins can rely
-- on it. The native host calls load_queries() once shortly after the first
-- frame paints; the compiles themselves run on a background thread, so
-- startup and rendering never block on grammar-sized query compiles. A
-- language highlighted before a query lands falls back to runtime / regex
-- highlighting and upgrades itself on the next request. Explicit reloads
-- (:tsreload, post-:tsinstall verification) still load synchronously so their
-- results and errors are immediate. The call is idempotent.
function M.load_queries()
  if M._queries_loaded then
    return true
  end
  M._queries_loaded = true
  local ok, query_errors = queries.load_all(native, root, registry)
  if not ok then
    report_query_failures(query_errors)
  end
  return ok
end

-- Called by the native host when an asynchronously compiled query failed, so
-- the boot warning (and :tsstatus diagnostics) still surface exactly as they
-- did when compilation ran synchronously. `query_errors` is a list of
-- "<language>: <reason>" strings, the same shape load_all produces.
M.report_query_failures = function(query_errors)
  report_query_failures(query_errors)
end

M.reload = function()
  native.reload()
  registry = dofile(root .. "/registry.lua")
  queries = dofile(root .. "/queries.lua")
  highlight = dofile(root .. "/highlight.lua")
  registry.register(native)
  M._queries_loaded = false
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
