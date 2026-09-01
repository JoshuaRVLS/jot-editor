local M = {}

function M.load(native, root, language)
  local path = root .. "/queries/" .. language .. ".scm"
  local file = io.open(path, "r")
  if not file then return false end
  local source = file:read("*a")
  file:close()
  local ok = pcall(native.set_query, language, source)
  return ok
end

function M.load_all(native, root, registry)
  for _, language in ipairs(registry.languages) do
    M.load(native, root, language.name)
  end
end

return M
