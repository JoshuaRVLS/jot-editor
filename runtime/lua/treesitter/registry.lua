local M = {}

-- This table is the complete Tree-sitter policy. Native code only owns handles,
-- loading, parsing, caching, and ABI validation.
local data = {
  {"ada", "https://github.com/briot/tree-sitter-ada", {".adb", ".ads"}},
  {"agda", "https://github.com/tree-sitter/tree-sitter-agda", {".agda"}},
  {"apex", "https://github.com/aheber/tree-sitter-sfapex", {".apex", ".cls", ".trigger"}, "apex"},
  {"asm", "https://github.com/rush-rs/tree-sitter-asm", {".asm", ".s", ".S"}},
  {"bash", "https://github.com/tree-sitter/tree-sitter-bash", {".sh", ".bash"}},
  {"bibtex", "https://github.com/latex-lsp/tree-sitter-bibtex", {".bib"}},
  {"bicep", "https://github.com/tree-sitter-grammars/tree-sitter-bicep", {".bicep"}},
  {"bp", "https://github.com/ambroisie/tree-sitter-bp", {".bp"}},
  {"bpftrace", "https://github.com/sgruszka/tree-sitter-bpftrace", {".bt"}},
  {"c", "https://github.com/tree-sitter/tree-sitter-c", {".c", ".h"}},
  {"c3", "https://github.com/c3lang/tree-sitter-c3", {".c3", ".c3i", ".c3t"}},
  {"c_sharp", "https://github.com/tree-sitter/tree-sitter-c-sharp", {".cs"}},
  {"caddy", "https://github.com/opa-oz/tree-sitter-caddy", {".caddyfile"}},
  {"chatito", "https://github.com/tree-sitter-grammars/tree-sitter-chatito", {".chatito"}},
  {"clojure", "https://github.com/sogaiu/tree-sitter-clojure", {".clj", ".cljs", ".cljc", ".edn"}},
  {"cmake", "https://github.com/uyha/tree-sitter-cmake", {".cmake"}},
  {"comment", "https://github.com/stsewd/tree-sitter-comment", {".comment"}},
  {"commonlisp", "https://github.com/tree-sitter-grammars/tree-sitter-commonlisp", {".cl", ".lisp"}},
  {"cpp", "https://github.com/tree-sitter/tree-sitter-cpp", {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"}},
  {"crystal", "https://github.com/crystal-lang-tools/tree-sitter-crystal", {".cr"}},
  {"css", "https://github.com/tree-sitter/tree-sitter-css", {".css"}},
  {"csv", "https://github.com/tree-sitter-grammars/tree-sitter-csv", {".csv", ".tsv"}},
  {"cue", "https://github.com/eonpatapon/tree-sitter-cue", {".cue"}},
  {"dart", "https://github.com/UserNobody14/tree-sitter-dart", {".dart"}},
  {"desktop", "https://github.com/ValdezFOmar/tree-sitter-desktop", {".desktop", ".directory"}},
  {"devicetree", "https://github.com/joelspadin/tree-sitter-devicetree", {".dts", ".dtsi", ".dtso", ".its", ".overlay"}},
  {"dhall", "https://github.com/jbellerb/tree-sitter-dhall", {".dhall"}},
  {"diff", "https://github.com/the-mikedavis/tree-sitter-diff", {".diff", ".patch"}},
  {"djot", "https://github.com/treeman/tree-sitter-djot", {".dj"}},
  {"dockerfile", "https://github.com/camdencheek/tree-sitter-dockerfile", {".dockerfile"}},
  {"doxygen", "https://github.com/tree-sitter-grammars/tree-sitter-doxygen", {".doxygen"}},
  {"earthfile", "https://github.com/glehmann/tree-sitter-earthfile", {".earthfile"}},
  {"editorconfig", "https://github.com/ValdezFOmar/tree-sitter-editorconfig", {".editorconfig"}},
  {"elixir", "https://github.com/elixir-lang/tree-sitter-elixir", {".ex", ".exs"}},
  {"elm", "https://github.com/elm-tooling/tree-sitter-elm", {".elm"}},
  {"erlang", "https://github.com/WhatsApp/tree-sitter-erlang", {".erl", ".hrl"}},
  {"facility", "https://github.com/FacilityApi/tree-sitter-facility", {".fsd"}},
  {"faust", "https://github.com/khiner/tree-sitter-faust", {".dsp", ".lib"}},
  {"fennel", "https://github.com/alexmozaidze/tree-sitter-fennel", {".fnl"}},
  {"fish", "https://github.com/ram02z/tree-sitter-fish", {".fish"}},
  {"foam", "https://github.com/FoamScience/tree-sitter-foam", {".foam", ".openfoam"}},
  {"fortran", "https://github.com/stadelmanma/tree-sitter-fortran", {".f", ".f90", ".f95"}},
  {"fsharp", "https://github.com/ionide/tree-sitter-fsharp", {".fs", ".fsx", ".fsi"}, "fsharp"},
  {"gdscript", "https://github.com/PrestonKnopp/tree-sitter-gdscript", {".gd"}},
  {"gdshader", "https://github.com/airblast-dev/tree-sitter-gdshader", {".gdshader", ".gdshaderinc"}},
  {"git_config", "https://github.com/the-mikedavis/tree-sitter-git-config", {".gitconfig"}},
  {"gitattributes", "https://github.com/tree-sitter-grammars/tree-sitter-gitattributes", {".gitattributes"}},
  {"gitcommit", "https://github.com/gbprod/tree-sitter-gitcommit", {".gitcommit"}},
  {"gleam", "https://github.com/gleam-lang/tree-sitter-gleam", {".gleam"}},
  {"glimmer", "https://github.com/ember-tooling/tree-sitter-glimmer", {".glimmer", ".handlebars", ".hbs", ".html.handlebars"}},
  {"glsl", "https://github.com/theHamsta/tree-sitter-glsl", {".glsl", ".vert", ".frag"}},
  {"gnuplot", "https://github.com/dpezto/tree-sitter-gnuplot", {".gnuplot"}},
  {"go", "https://github.com/tree-sitter/tree-sitter-go", {".go"}},
  {"godot_resource", "https://github.com/PrestonKnopp/tree-sitter-godot-resource", {".godot", ".tres", ".tscn"}},
  {"gomod", "https://github.com/camdencheek/tree-sitter-go-mod", {".go.mod"}},
  {"gosum", "https://github.com/tree-sitter-grammars/tree-sitter-go-sum", {".go.sum"}},
  {"gpg", "https://github.com/tree-sitter-grammars/tree-sitter-gpg-config", {".gpg.conf"}},
  {"graphql", "https://github.com/bkegley/tree-sitter-graphql", {".graphql", ".gql"}},
  {"gren", "https://github.com/MaeBrooks/tree-sitter-gren", {".gren"}},
  {"groovy", "https://github.com/murtaza64/tree-sitter-groovy", {".groovy"}},
  {"groq", "https://github.com/ajrussellaudio/tree-sitter-groq", {".groq"}},
  {"haskell", "https://github.com/tree-sitter/tree-sitter-haskell", {".hs"}},
  {"hcl", "https://github.com/tree-sitter-grammars/tree-sitter-hcl", {".hcl", ".tf"}},
  {"heex", "https://github.com/connorlay/tree-sitter-heex", {".heex", ".neex"}},
  {"html", "https://github.com/tree-sitter/tree-sitter-html", {".html", ".htm"}},
  {"idl", "https://github.com/cathaysia/tree-sitter-idl", {".idl"}},
  {"idris", "https://github.com/kayhide/tree-sitter-idris", {".idr"}},
  {"ini", "https://github.com/justinmk/tree-sitter-ini", {".ini", ".cfg", ".conf", ".properties"}},
  {"inko", "https://github.com/inko-lang/tree-sitter-inko", {".inko"}},
  {"janet_simple", "https://github.com/sogaiu/tree-sitter-janet-simple", {".cgen", ".janet", ".jdn"}},
  {"java", "https://github.com/tree-sitter/tree-sitter-java", {".java"}},
  {"javadoc", "https://github.com/rmuir/tree-sitter-javadoc", {".javadoc"}},
  {"javascript", "https://github.com/tree-sitter/tree-sitter-javascript", {".js", ".jsx", ".mjs", ".cjs"}, nil, {"jsx"}},
  {"jjdescription", "https://github.com/ribru17/tree-sitter-jjdescription", {".jjdescription"}},
  {"json", "https://github.com/tree-sitter/tree-sitter-json", {".json", ".jsonc"}},
  {"julia", "https://github.com/tree-sitter-grammars/tree-sitter-julia", {".jl"}},
  {"jq", "https://github.com/itchyny/tree-sitter-jq", {".jq"}},
  {"just", "https://github.com/IndianBoy42/tree-sitter-just", {".just", ".justfile"}},
  {"kcl", "https://github.com/kcl-lang/tree-sitter-kcl", {".k"}},
  {"kconfig", "https://github.com/tree-sitter-grammars/tree-sitter-kconfig", {".kconfig"}},
  {"kdl", "https://github.com/tree-sitter-grammars/tree-sitter-kdl", {".kdl"}},
  {"kitty", "https://github.com/OXY2DEV/tree-sitter-kitty", {".kitty.conf"}},
  {"kos", "https://github.com/kos-lang/tree-sitter-kos", {".kos"}},
  {"kotlin", "https://github.com/fwcd/tree-sitter-kotlin", {".kt", ".kts"}},
  {"koto", "https://github.com/koto-lang/tree-sitter-koto", {".koto"}},
  {"lalrpop", "https://github.com/traxys/tree-sitter-lalrpop", {".lalrpop"}},
  {"latex", "https://github.com/latex-lsp/tree-sitter-latex", {".tex"}},
  {"ledger", "https://github.com/cbarrete/tree-sitter-ledger", {".journal", ".ledger"}},
  {"liquidsoap", "https://github.com/savonet/tree-sitter-liquidsoap", {".liquidsoap"}},
  {"llvm", "https://github.com/benwilliamgraham/tree-sitter-llvm", {".ll", ".llvm"}},
  {"lua", "https://github.com/tree-sitter-grammars/tree-sitter-lua", {".lua"}},
  {"luadoc", "https://github.com/tree-sitter-grammars/tree-sitter-luadoc", {".luadoc"}},
  {"make", "https://github.com/alemuller/tree-sitter-make", {".make", ".mk"}},
  {"markdown", "https://github.com/tree-sitter-grammars/tree-sitter-markdown", {".md", ".markdown"}},
  {"matlab", "https://github.com/acristoffers/tree-sitter-matlab", {".m"}},
  {"meson", "https://github.com/tree-sitter-grammars/tree-sitter-meson", {".meson"}},
  {"mlir", "https://github.com/artagnon/tree-sitter-mlir", {".mlir"}},
  {"nginx", "https://github.com/opa-oz/tree-sitter-nginx", {".nginx"}},
  {"nickel", "https://github.com/nickel-lang/tree-sitter-nickel", {".ncl", ".nickel"}},
  {"nix", "https://github.com/nix-community/tree-sitter-nix", {".nix"}},
  {"nu", "https://github.com/nushell/tree-sitter-nu", {".nu"}},
  {"ocaml", "https://github.com/tree-sitter/tree-sitter-ocaml", {".ml", ".mli"}},
  {"ocamllex", "https://github.com/atom-ocaml/tree-sitter-ocamllex", {".mll"}},
  {"odin", "https://github.com/tree-sitter-grammars/tree-sitter-odin", {".odin"}},
  {"pascal", "https://github.com/Isopod/tree-sitter-pascal", {".dpr", ".lpr", ".pas", ".pp"}},
  {"perl", "https://github.com/tree-sitter-perl/tree-sitter-perl", {".pl", ".pm"}},
  {"php", "https://github.com/tree-sitter/tree-sitter-php", {".php"}},
  {"pkl", "https://github.com/apple/tree-sitter-pkl", {".pcf", ".pkl"}},
  {"poe_filter", "https://github.com/tree-sitter-grammars/tree-sitter-poe-filter", {".filter"}},
  {"powershell", "https://github.com/airbus-cert/tree-sitter-powershell", {".ps1", ".psm1", ".psd1"}},
  {"prisma", "https://github.com/victorhqc/tree-sitter-prisma", {".prisma"}},
  {"proto", "https://github.com/treywood/tree-sitter-proto", {".proto"}},
  {"puppet", "https://github.com/tree-sitter-grammars/tree-sitter-puppet", {".pp"}},
  {"pymanifest", "https://github.com/tree-sitter-grammars/tree-sitter-pymanifest", {".manifest.in"}},
  {"python", "https://github.com/tree-sitter/tree-sitter-python", {".py", ".pyw"}},
  {"ql", "https://github.com/tree-sitter/tree-sitter-ql", {".ql", ".qll"}},
  {"qmldir", "https://github.com/tree-sitter-grammars/tree-sitter-qmldir", {".qmldir"}},
  {"query", "https://github.com/tree-sitter/tree-sitter-query", {".scm"}},
  {"r", "https://github.com/r-lib/tree-sitter-r", {".r"}},
  {"racket", "https://github.com/6cdh/tree-sitter-racket", {".rkt"}},
  {"rasi", "https://github.com/Fymyte/tree-sitter-rasi", {".rasi"}},
  {"razor", "https://github.com/tris203/tree-sitter-razor", {".razor"}},
  {"rbs", "https://github.com/joker1007/tree-sitter-rbs", {".rbs"}},
  {"readline", "https://github.com/tree-sitter-grammars/tree-sitter-readline", {".inputrc"}},
  {"regex", "https://github.com/tree-sitter/tree-sitter-regex", {".regex"}},
  {"rego", "https://github.com/FallenAngel97/tree-sitter-rego", {".rego"}},
  {"requirements", "https://github.com/tree-sitter-grammars/tree-sitter-requirements", {".pip", ".requirements.txt"}},
  {"rescript", "https://github.com/rescript-lang/tree-sitter-rescript", {".res", ".resi"}},
  {"robot", "https://github.com/Hubro/tree-sitter-robot", {".robot"}},
  {"rst", "https://github.com/stsewd/tree-sitter-rst", {".rst"}},
  {"ruby", "https://github.com/tree-sitter/tree-sitter-ruby", {".rb"}},
  {"runescript", "https://github.com/2004Scape/tree-sitter-runescript", {".cs2", ".rs2"}},
  {"rust", "https://github.com/tree-sitter/tree-sitter-rust", {".rs"}},
  {"scala", "https://github.com/tree-sitter/tree-sitter-scala", {".scala", ".sc"}},
  {"slang", "https://github.com/tree-sitter-grammars/tree-sitter-slang", {".slang"}},
  {"snakemake", "https://github.com/osthomas/tree-sitter-snakemake", {".smk", ".snakefile"}},
  {"snl", "https://github.com/minijackson/tree-sitter-snl", {".st", ".stt"}},
  {"solidity", "https://github.com/JoranHonig/tree-sitter-solidity", {".sol"}},
  {"sourcepawn", "https://github.com/nilshelmig/tree-sitter-sourcepawn", {".inc", ".sp"}},
  {"sparql", "https://github.com/GordianDziwis/tree-sitter-sparql", {".rq", ".sparql"}},
  {"sproto", "https://github.com/hanxi/tree-sitter-sproto", {".sproto"}},
  {"sql", "https://github.com/DerekStride/tree-sitter-sql", {".sql"}},
  {"ssh_config", "https://github.com/tree-sitter-grammars/tree-sitter-ssh-config", {".ssh_config"}},
  {"starlark", "https://github.com/tree-sitter-grammars/tree-sitter-starlark", {".bzl"}},
  {"strace", "https://github.com/sigmaSd/tree-sitter-strace", {".strace"}},
  {"svelte", "https://github.com/tree-sitter-grammars/tree-sitter-svelte", {".svelte"}},
  {"swift", "https://github.com/alex-pinkus/tree-sitter-swift", {".swift"}},
  {"systemverilog", "https://github.com/gmlarumbe/tree-sitter-systemverilog", {".sv", ".svh"}},
  {"t32", "https://github.com/xasc/tree-sitter-t32", {".cmm", ".cmmt", ".t32"}},
  {"tact", "https://github.com/tact-lang/tree-sitter-tact", {".tact"}},
  {"tcl", "https://github.com/tree-sitter-grammars/tree-sitter-tcl", {".tcl", ".tk", ".tm"}},
  {"teal", "https://github.com/euclidianAce/tree-sitter-teal", {".tl"}},
  {"templ", "https://github.com/vrischmann/tree-sitter-templ", {".templ"}},
  {"tera", "https://github.com/uncenter/tree-sitter-tera", {".tera"}},
  {"tlaplus", "https://github.com/tlaplus-community/tree-sitter-tlaplus", {".tla"}},
  {"toml", "https://github.com/tree-sitter-grammars/tree-sitter-toml", {".toml"}},
  {"tsx", "https://github.com/tree-sitter/tree-sitter-typescript", {".tsx"}, "tsx"},
  {"twig", "https://github.com/gbprod/tree-sitter-twig", {".html.twig", ".html.twig.js.css", ".twig"}},
  {"typescript", "https://github.com/tree-sitter/tree-sitter-typescript", {".ts", ".mts", ".cts"}, "typescript"},
  {"typespec", "https://github.com/happenslol/tree-sitter-typespec", {".tsp", ".typespec"}},
  {"typoscript", "https://github.com/Teddytrombone/tree-sitter-typoscript", {".tsconfig", ".typoscript"}},
  {"udev", "https://github.com/tree-sitter-grammars/tree-sitter-udev", {".rules"}},
  {"unison", "https://github.com/kylegoetz/tree-sitter-unison", {".u"}},
  {"usd", "https://github.com/ColinKennedy/tree-sitter-usd", {".usd", ".usda", ".usdc"}},
  {"vento", "https://github.com/ventojs/tree-sitter-vento", {".vto"}},
  {"verilog", "https://github.com/tree-sitter/tree-sitter-verilog", {".v", ".vh"}},
  {"vhdl", "https://github.com/jpt13653903/tree-sitter-vhdl", {".vhd", ".vhdl"}},
  {"vim", "https://github.com/tree-sitter-grammars/tree-sitter-vim", {".vim"}},
  {"vimdoc", "https://github.com/neovim/tree-sitter-vimdoc", {".txt"}},
  {"vue", "https://github.com/tree-sitter-grammars/tree-sitter-vue", {".vue"}},
  {"wgsl", "https://github.com/szebniok/tree-sitter-wgsl", {".wgsl"}},
  {"wit", "https://github.com/bytecodealliance/tree-sitter-wit", {".wit"}},
  {"wxml", "https://github.com/BlockLune/tree-sitter-wxml", {".wxml"}},
  {"xcompose", "https://github.com/tree-sitter-grammars/tree-sitter-xcompose", {".xcompose"}},
  {"xml", "https://github.com/tree-sitter-grammars/tree-sitter-xml", {".xml"}},
  {"xresources", "https://github.com/ValdezFOmar/tree-sitter-xresources", {".xdefaults", ".xresources"}},
  {"yaml", "https://github.com/tree-sitter-grammars/tree-sitter-yaml", {".yml", ".yaml"}},
  {"yuck", "https://github.com/tree-sitter-grammars/tree-sitter-yuck", {".yuck"}},
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
