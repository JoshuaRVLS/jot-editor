local M = {}

-- This table is the complete Tree-sitter policy. Native code only owns handles,
-- loading, parsing, caching, and ABI validation.
local data = {
  {"asm", "https://github.com/rush-rs/tree-sitter-asm", {".asm", ".s", ".S"}},
  {"bash", "https://github.com/tree-sitter/tree-sitter-bash", {".sh", ".bash"}},
  {"bibtex", "https://github.com/latex-lsp/tree-sitter-bibtex", {".bib"}},
  {"c", "https://github.com/tree-sitter/tree-sitter-c", {".c", ".h"}},
  {"c_sharp", "https://github.com/tree-sitter/tree-sitter-c-sharp", {".cs"}},
  {"cmake", "https://github.com/uyha/tree-sitter-cmake", {".cmake"}},
  {"cpp", "https://github.com/tree-sitter/tree-sitter-cpp", {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"}},
  {"css", "https://github.com/tree-sitter/tree-sitter-css", {".css"}},
  {"csv", "https://github.com/tree-sitter-grammars/tree-sitter-csv", {".csv", ".tsv"}},
  {"dart", "https://github.com/UserNobody14/tree-sitter-dart", {".dart"}},
  {"diff", "https://github.com/the-mikedavis/tree-sitter-diff", {".diff", ".patch"}},
  {"dockerfile", "https://github.com/camdencheek/tree-sitter-dockerfile", {".dockerfile"}},
  {"elixir", "https://github.com/elixir-lang/tree-sitter-elixir", {".ex", ".exs"}},
  {"erlang", "https://github.com/WhatsApp/tree-sitter-erlang", {".erl", ".hrl"}},
  {"fish", "https://github.com/ram02z/tree-sitter-fish", {".fish"}},
  {"fortran", "https://github.com/stadelmanma/tree-sitter-fortran", {".f", ".f90", ".f95"}},
  {"gdscript", "https://github.com/PrestonKnopp/tree-sitter-gdscript", {".gd"}},
  {"git_config", "https://github.com/the-mikedavis/tree-sitter-git-config", {".gitconfig"}},
  {"gitcommit", "https://github.com/gbprod/tree-sitter-gitcommit", {".gitcommit"}},
  {"glsl", "https://github.com/theHamsta/tree-sitter-glsl", {".glsl", ".vert", ".frag"}},
  {"go", "https://github.com/tree-sitter/tree-sitter-go", {".go"}},
  {"graphql", "https://github.com/bkegley/tree-sitter-graphql", {".graphql", ".gql"}},
  {"haskell", "https://github.com/tree-sitter/tree-sitter-haskell", {".hs"}},
  {"hcl", "https://github.com/tree-sitter-grammars/tree-sitter-hcl", {".hcl", ".tf"}},
  {"html", "https://github.com/tree-sitter/tree-sitter-html", {".html", ".htm"}},
  {"ini", "https://github.com/justinmk/tree-sitter-ini", {".ini", ".cfg", ".conf", ".properties"}},
  {"java", "https://github.com/tree-sitter/tree-sitter-java", {".java"}},
  {"javascript", "https://github.com/tree-sitter/tree-sitter-javascript", {".js", ".jsx", ".mjs", ".cjs"}, nil, {"jsx"}},
  {"json", "https://github.com/tree-sitter/tree-sitter-json", {".json", ".jsonc"}},
  {"kotlin", "https://github.com/fwcd/tree-sitter-kotlin", {".kt", ".kts"}},
  {"latex", "https://github.com/latex-lsp/tree-sitter-latex", {".tex"}},
  {"lua", "https://github.com/tree-sitter-grammars/tree-sitter-lua", {".lua"}},
  {"make", "https://github.com/alemuller/tree-sitter-make", {".make", ".mk"}},
  {"markdown", "https://github.com/tree-sitter-grammars/tree-sitter-markdown", {".md", ".markdown"}},
  {"nix", "https://github.com/nix-community/tree-sitter-nix", {".nix"}},
  {"ocaml", "https://github.com/tree-sitter/tree-sitter-ocaml", {".ml", ".mli"}},
  {"perl", "https://github.com/tree-sitter-perl/tree-sitter-perl", {".pl", ".pm"}},
  {"php", "https://github.com/tree-sitter/tree-sitter-php", {".php"}},
  {"proto", "https://github.com/treywood/tree-sitter-proto", {".proto"}},
  {"python", "https://github.com/tree-sitter/tree-sitter-python", {".py", ".pyw"}},
  {"query", "https://github.com/tree-sitter/tree-sitter-query", {".scm"}},
  {"r", "https://github.com/r-lib/tree-sitter-r", {".r"}},
  {"regex", "https://github.com/tree-sitter/tree-sitter-regex", {".regex"}},
  {"ruby", "https://github.com/tree-sitter/tree-sitter-ruby", {".rb"}},
  {"rust", "https://github.com/tree-sitter/tree-sitter-rust", {".rs"}},
  {"scala", "https://github.com/tree-sitter/tree-sitter-scala", {".scala", ".sc"}},
  {"sql", "https://github.com/DerekStride/tree-sitter-sql", {".sql"}},
  {"svelte", "https://github.com/tree-sitter-grammars/tree-sitter-svelte", {".svelte"}},
  {"swift", "https://github.com/alex-pinkus/tree-sitter-swift", {".swift"}},
  {"systemverilog", "https://github.com/gmlarumbe/tree-sitter-systemverilog", {".sv", ".svh"}},
  {"toml", "https://github.com/tree-sitter-grammars/tree-sitter-toml", {".toml"}},
  {"tsx", "https://github.com/tree-sitter/tree-sitter-typescript", {".tsx"}, "tsx"},
  {"typescript", "https://github.com/tree-sitter/tree-sitter-typescript", {".ts", ".mts", ".cts"}, "typescript"},
  {"verilog", "https://github.com/tree-sitter/tree-sitter-verilog", {".v", ".vh"}},
  {"vim", "https://github.com/tree-sitter-grammars/tree-sitter-vim", {".vim"}},
  {"vimdoc", "https://github.com/neovim/tree-sitter-vimdoc", {".txt"}},
  {"vue", "https://github.com/tree-sitter-grammars/tree-sitter-vue", {".vue"}},
  {"wgsl", "https://github.com/szebniok/tree-sitter-wgsl", {".wgsl"}},
  {"xml", "https://github.com/tree-sitter-grammars/tree-sitter-xml", {".xml"}},
  {"yaml", "https://github.com/tree-sitter-grammars/tree-sitter-yaml", {".yml", ".yaml"}},
  {"zig", "https://github.com/tree-sitter-grammars/tree-sitter-zig", {".zig"}},
  {"zsh", "https://github.com/georgeharker/tree-sitter-zsh", {".zsh"}},
}

local function libraries(name)
  local hyphen = name:gsub("_", "-")
  local underscore = name:gsub("-", "_")
  local result = {"libtree-sitter-" .. hyphen .. ".so", "libtree-sitter-" .. hyphen .. ".dylib",
    "tree-sitter-" .. hyphen .. ".dll", "libtree-sitter-" .. hyphen .. ".dll",
    "libtree_sitter_" .. underscore .. ".so", "libtree_sitter_" .. underscore .. ".dylib",
    "tree_sitter_" .. underscore .. ".dll", "libtree_sitter_" .. underscore .. ".dll",
    "tree-sitter-" .. hyphen .. ".so", "tree-sitter-" .. hyphen .. ".dylib"}
  if hyphen ~= name then
    for _, suffix in ipairs({".so", ".dylib", ".dll"}) do
      result[#result + 1] = "libtree-sitter-" .. name .. suffix
    end
    result[#result + 1] = "tree-sitter-" .. name .. ".dll"
  end
  return result
end

M.languages = {}
for _, item in ipairs(data) do
  local language = {name = item[1], url = item[2], extensions = item[3],
    source_subdir = item[4] or "", aliases = item[5] or {},
    symbol = "tree_sitter_" .. item[1], library_names = libraries(item[1]),
    -- Relative to lua/treesitter/queries/ (queries.lua prepends the dir).
    query_file = item[1] .. "/highlights.scm", minimal_query = ""}
  if language.name == "cpp" then
    language.minimal_query = [[
"break" @keyword
"case" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"for" @keyword
"if" @keyword
"namespace" @keyword
"return" @keyword
"struct" @keyword
"switch" @keyword
"template" @keyword
"typename" @keyword
"using" @keyword
"while" @keyword
(raw_string_literal) @string
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(field_identifier) @property
(call_expression function: (identifier) @function)
(function_declarator declarator: (identifier) @function)
(preproc_include) @keyword
]]
  end
  table.insert(M.languages, language)
end

function M.register(native)
  for _, language in ipairs(M.languages) do
    native.register_language(language.name, language.extensions, "", language.url,
      language.source_subdir, language.symbol, language.library_names,
      language.minimal_query)
    for _, alias in ipairs(language.aliases) do
      native.register_language(language.name, {"." .. alias}, "", language.url,
        language.source_subdir, language.symbol, language.library_names,
        language.minimal_query)
    end
  end
end

function M.find(name)
  for _, language in ipairs(M.languages) do
    if language.name == name then return language end
    for _, alias in ipairs(language.aliases) do if alias == name then return language end end
  end
end

return M
