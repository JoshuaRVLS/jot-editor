local M = {}

-- Returns true when the bundled query was applied, or false plus a reason.
-- A failure never disables the language: missing or stale bundled queries
-- fall back to the runtime queries shipped with an installed parser (or regex
-- highlighting). Disabling would break parser loading, :tsinstall verification,
-- and :tsstatus for an otherwise valid language.
function M.load(native, root, language)
  local path = root .. "/queries/" .. language.query_file
  local file = io.open(path, "r")
  if not file then
    return false, language.name .. ": no bundled query file (" .. path .. ")"
  end
  local source = file:read("*a")
  file:close()
  -- The bundled query may not match the installed parser's grammar version
  -- (e.g. renamed node types), so guard compilation: native.set_query raises
  -- on failure.
  local ok, err = pcall(native.set_query, language.name, source)
  if not ok then
    return false, language.name .. ": " .. (err or "query compilation failed")
  end
  return true
end

function M.load_all(native, root, registry)
  local failures = {}
  for _, language in ipairs(registry.languages) do
    local ok, reason = M.load(native, root, language)
    if not ok then failures[#failures + 1] = reason end
  end
  return #failures == 0, failures
end

return M
