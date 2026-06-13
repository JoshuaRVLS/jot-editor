#include "tree_sitter_catalog.h"

#include <algorithm>
#include <cctype>

namespace {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

std::string replace_copy(std::string value, char from, char to) {
  std::replace(value.begin(), value.end(), from, to);
  return value;
}

std::string strip_suffix(std::string value, const std::string &suffix) {
  if (value.size() >= suffix.size() &&
      value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
    value.erase(value.size() - suffix.size());
  }
  return value;
}

std::vector<std::string> default_library_names(const std::string &name) {
  const std::string hyphen = replace_copy(name, '_', '-');
  const std::string underscore = replace_copy(name, '-', '_');
  std::vector<std::string> names = {
      "libtree-sitter-" + hyphen + ".so",
      "libtree-sitter-" + hyphen + ".dylib",
      "libtree_sitter_" + underscore + ".so",
      "libtree_sitter_" + underscore + ".dylib",
      "tree-sitter-" + hyphen + ".so",
      "tree-sitter-" + hyphen + ".dylib",
  };
  if (hyphen != name) {
    names.push_back("libtree-sitter-" + name + ".so");
    names.push_back("libtree-sitter-" + name + ".dylib");
  }
  return names;
}

TreeSitterCatalogEntry make_entry(
    std::string name, std::string url, std::vector<std::string> extensions,
    std::string source_subdir = "",
    std::vector<std::string> library_names = {}) {
  name = TreeSitterCatalog::normalize_language_name(name);
  TreeSitterCatalogEntry entry;
  entry.name = name;
  entry.url = std::move(url);
  entry.symbol = "tree_sitter_" + replace_copy(name, '-', '_');
  entry.source_subdir = std::move(source_subdir);
  entry.extensions = std::move(extensions);
  entry.library_names =
      library_names.empty() ? default_library_names(name) : std::move(library_names);
  return entry;
}
} // namespace

namespace TreeSitterCatalog {
std::string normalize_language_name(const std::string &language) {
  std::string out = lower_copy(language);
  for (char &c : out) {
    if (c == '-' || c == ' ') {
      c = '_';
    }
  }
  return out;
}

bool is_github_url(const std::string &value) {
  std::string lower = lower_copy(value);
  return lower.rfind("https://github.com/", 0) == 0 ||
         lower.rfind("http://github.com/", 0) == 0 ||
         lower.rfind("github.com/", 0) == 0;
}

std::string normalize_github_url(const std::string &url) {
  std::string lower = lower_copy(url);
  if (lower.rfind("github.com/", 0) == 0) {
    return "https://" + url;
  }
  return url;
}

std::string language_name_from_url(const std::string &url) {
  std::string value = url;
  value = strip_suffix(value, "/");
  value = strip_suffix(value, ".git");
  size_t slash = value.find_last_of('/');
  std::string repo = slash == std::string::npos ? value : value.substr(slash + 1);
  repo = lower_copy(repo);
  if (repo.rfind("tree-sitter-", 0) == 0) {
    repo.erase(0, std::string("tree-sitter-").size());
  }
  return normalize_language_name(repo);
}

const std::vector<TreeSitterCatalogEntry> &entries() {
  static const std::vector<TreeSitterCatalogEntry> catalog = {
      make_entry("c", "https://github.com/tree-sitter/tree-sitter-c",
                 {".c", ".h"}),
      make_entry("cpp", "https://github.com/tree-sitter/tree-sitter-cpp",
                 {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"}),
      make_entry("python", "https://github.com/tree-sitter/tree-sitter-python",
                 {".py"}),
      make_entry("javascript", "https://github.com/tree-sitter/tree-sitter-javascript",
                 {".js", ".jsx", ".mjs", ".cjs"}),
      make_entry("typescript", "https://github.com/tree-sitter/tree-sitter-typescript",
                 {".ts"}, "typescript"),
      make_entry("tsx", "https://github.com/tree-sitter/tree-sitter-typescript",
                 {".tsx"}, "tsx"),
      make_entry("rust", "https://github.com/tree-sitter/tree-sitter-rust",
                 {".rs"}),
      make_entry("go", "https://github.com/tree-sitter/tree-sitter-go",
                 {".go"}),
      make_entry("json", "https://github.com/tree-sitter/tree-sitter-json",
                 {".json", ".jsonc"}),
      make_entry("html", "https://github.com/tree-sitter/tree-sitter-html",
                 {".html", ".htm"}),
      make_entry("css", "https://github.com/tree-sitter/tree-sitter-css",
                 {".css"}),
      make_entry("bash", "https://github.com/tree-sitter/tree-sitter-bash",
                 {".sh", ".bash"}),
      make_entry("lua", "https://github.com/tree-sitter-grammars/tree-sitter-lua",
                 {".lua"}),
      make_entry("markdown", "https://github.com/tree-sitter-grammars/tree-sitter-markdown",
                 {".md", ".markdown"}),
      make_entry("toml", "https://github.com/tree-sitter-grammars/tree-sitter-toml",
                 {".toml"}),
      make_entry("yaml", "https://github.com/tree-sitter-grammars/tree-sitter-yaml",
                 {".yml", ".yaml"}),
      make_entry("ruby", "https://github.com/tree-sitter/tree-sitter-ruby",
                 {".rb"}),
      make_entry("php", "https://github.com/tree-sitter/tree-sitter-php",
                 {".php"}),
      make_entry("java", "https://github.com/tree-sitter/tree-sitter-java",
                 {".java"}),
      make_entry("kotlin", "https://github.com/fwcd/tree-sitter-kotlin",
                 {".kt", ".kts"}),
      make_entry("swift", "https://github.com/alex-pinkus/tree-sitter-swift",
                 {".swift"}),
      make_entry("c_sharp", "https://github.com/tree-sitter/tree-sitter-c-sharp",
                 {".cs"}),
      make_entry("sql", "https://github.com/DerekStride/tree-sitter-sql",
                 {".sql"}),
      make_entry("cmake", "https://github.com/uyha/tree-sitter-cmake",
                 {".cmake"}),
      make_entry("dockerfile", "https://github.com/camdencheek/tree-sitter-dockerfile",
                 {".dockerfile"}),
      make_entry("make", "https://github.com/alemuller/tree-sitter-make",
                 {".make", ".mk"}),
      make_entry("ini", "https://github.com/justinmk/tree-sitter-ini",
                 {".ini", ".cfg", ".conf", ".properties"}),
      make_entry("xml", "https://github.com/tree-sitter-grammars/tree-sitter-xml",
                 {".xml"}),
      make_entry("vue", "https://github.com/tree-sitter-grammars/tree-sitter-vue",
                 {".vue"}),
      make_entry("svelte", "https://github.com/tree-sitter-grammars/tree-sitter-svelte",
                 {".svelte"}),
      make_entry("zig", "https://github.com/tree-sitter-grammars/tree-sitter-zig",
                 {".zig"}),
      make_entry("scala", "https://github.com/tree-sitter/tree-sitter-scala",
                 {".scala", ".sc"}),
      make_entry("elixir", "https://github.com/elixir-lang/tree-sitter-elixir",
                 {".ex", ".exs"}),
      make_entry("erlang", "https://github.com/WhatsApp/tree-sitter-erlang",
                 {".erl", ".hrl"}),
      make_entry("haskell", "https://github.com/tree-sitter/tree-sitter-haskell",
                 {".hs"}),
      make_entry("ocaml", "https://github.com/tree-sitter/tree-sitter-ocaml",
                 {".ml", ".mli"}),
      make_entry("r", "https://github.com/r-lib/tree-sitter-r",
                 {".r"}),
      make_entry("perl", "https://github.com/tree-sitter-perl/tree-sitter-perl",
                 {".pl", ".pm"}),
      make_entry("dart", "https://github.com/UserNobody14/tree-sitter-dart",
                 {".dart"}),
      make_entry("hcl", "https://github.com/tree-sitter-grammars/tree-sitter-hcl",
                 {".hcl", ".tf"}),
      make_entry("nix", "https://github.com/nix-community/tree-sitter-nix",
                 {".nix"}),
      make_entry("vim", "https://github.com/tree-sitter-grammars/tree-sitter-vim",
                 {".vim"}),
      make_entry("vimdoc", "https://github.com/neovim/tree-sitter-vimdoc",
                 {".txt"}),
      make_entry("query", "https://github.com/tree-sitter/tree-sitter-query",
                 {".scm"}),
      make_entry("regex", "https://github.com/tree-sitter/tree-sitter-regex",
                 {".regex"}),
      make_entry("diff", "https://github.com/the-mikedavis/tree-sitter-diff",
                 {".diff", ".patch"}),
      make_entry("git_config", "https://github.com/the-mikedavis/tree-sitter-git-config",
                 {".gitconfig"}),
      make_entry("gitcommit", "https://github.com/gbprod/tree-sitter-gitcommit",
                 {".gitcommit"}),
      make_entry("csv", "https://github.com/tree-sitter-grammars/tree-sitter-csv",
                 {".csv", ".tsv"}),
      make_entry("proto", "https://github.com/treywood/tree-sitter-proto",
                 {".proto"}),
      make_entry("graphql", "https://github.com/bkegley/tree-sitter-graphql",
                 {".graphql", ".gql"}),
      make_entry("latex", "https://github.com/latex-lsp/tree-sitter-latex",
                 {".tex"}),
      make_entry("bibtex", "https://github.com/latex-lsp/tree-sitter-bibtex",
                 {".bib"}),
      make_entry("asm", "https://github.com/rush-rs/tree-sitter-asm",
                 {".asm", ".s", ".S"}),
      make_entry("verilog", "https://github.com/tree-sitter/tree-sitter-verilog",
                 {".v", ".vh"}),
      make_entry("systemverilog", "https://github.com/gmlarumbe/tree-sitter-systemverilog",
                 {".sv", ".svh"}),
      make_entry("fortran", "https://github.com/stadelmanma/tree-sitter-fortran",
                 {".f", ".f90", ".f95"}),
      make_entry("gdscript", "https://github.com/PrestonKnopp/tree-sitter-gdscript",
                 {".gd"}),
      make_entry("glsl", "https://github.com/theHamsta/tree-sitter-glsl",
                 {".glsl", ".vert", ".frag"}),
      make_entry("wgsl", "https://github.com/szebniok/tree-sitter-wgsl",
                 {".wgsl"}),
      make_entry("zsh", "https://github.com/georgeharker/tree-sitter-zsh",
                 {".zsh"}),
      make_entry("fish", "https://github.com/ram02z/tree-sitter-fish",
                 {".fish"}),
  };
  return catalog;
}

const TreeSitterCatalogEntry *find_language(const std::string &language) {
  const std::string normalized = normalize_language_name(language);
  for (const auto &entry : entries()) {
    if (entry.name == normalized) {
      return &entry;
    }
  }
  return nullptr;
}

TreeSitterCatalogEntry entry_for_github_url(const std::string &url) {
  std::string name = language_name_from_url(url);
  return make_entry(name, normalize_github_url(url), {});
}

std::vector<std::string> language_names() {
  std::vector<std::string> names;
  for (const auto &entry : entries()) {
    names.push_back(entry.name);
  }
  return names;
}
} // namespace TreeSitterCatalog
