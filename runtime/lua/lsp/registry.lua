-- LSP / language-tooling package catalog, generated from the mason registry.
-- Source: https://github.com/mason-org/mason-registry
-- Regenerate: python3 tools/mason_import.py <mason-registry-checkout>
--
-- The catalog is split into alphabetical shards under servers/ so each
-- file stays small; this module loads them and exposes the combined list.

local M = {}

M.entries = {}
for _, shard in ipairs({"actionlint_bslint", "buf_csskit", "cssmodules-language-server_erb-lint", "erg_gotests", "gotestsum_kmp-lsp", "kos-language-server_ms-terraform-lsp", "mutt-language-server_pint", "pkl-lsp_rescript-language-server", "revive_sonarlint-language-server", "sorbet_textlsp", "tflint_vue", "vulture_zuban"}) do
  local part = dofile(_G.jot_lsp_lua_root .. "/servers/" .. shard .. ".lua")
  for _, e in ipairs(part) do
    M.entries[#M.entries + 1] = e
  end
end

-- Resolves a user-supplied id or alias to an entry.
function M.resolve(name)
  if not name then
    return nil
  end
  local n = tostring(name):lower():gsub('%s', '')
  for _, e in ipairs(M.entries) do
    if e.id == n then
      return e
    end
    for _, a in ipairs(e.aliases or {}) do
      if a == n then
        return e
      end
    end
  end
  return nil
end

return M
