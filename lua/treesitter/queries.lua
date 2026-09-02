local M = {}

function M.load(native, root, language)
  local path = root .. "/queries/" .. language.query_file
  local file = io.open(path, "r")
  if not file then
    native.disable_language(language.name)
    return false, "query file not found: " .. path
  end
  local source = file:read("*a")
  file:close()
  local ok, err = pcall(native.set_query, language.name, source)
  if not ok then native.disable_language(language.name); return false, err end
  return true
end

function M.load_all(native, root, registry)
  local failures = {}
  for _, language in ipairs(registry.languages) do
    local ok, err = M.load(native, root, language)
    if not ok then failures[#failures + 1] = language.name .. ": " .. err end
  end
  return #failures == 0, failures
end

return M
