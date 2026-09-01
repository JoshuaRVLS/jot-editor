local M = {}

-- Metadata stays declarative so startup order and extension ownership are clear.
M.languages = {
  { name = "c", extensions = { ".c", ".h" } },
  { name = "cpp", extensions = { ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx" } },
  { name = "javascript", extensions = { ".js", ".mjs", ".cjs" } },
  { name = "python", extensions = { ".py", ".pyw" } },
  { name = "lua", extensions = { ".lua" } },
  { name = "rust", extensions = { ".rs" } },
  { name = "go", extensions = { ".go" } },
  { name = "json", extensions = { ".json" } },
  { name = "markdown", extensions = { ".md", ".markdown" } },
  { name = "yaml", extensions = { ".yml", ".yaml" } },
}

function M.register(native)
  for _, language in ipairs(M.languages) do
    native.register_language(language.name, language.extensions)
  end
end

return M
