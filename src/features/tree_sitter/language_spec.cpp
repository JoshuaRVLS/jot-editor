#include "tree_sitter_language_spec.h"
#include "tree_sitter_catalog.h"

#include <algorithm>

TreeSitterLanguageSpec tree_sitter_language_spec_c();
TreeSitterLanguageSpec tree_sitter_language_spec_cpp();
TreeSitterLanguageSpec tree_sitter_language_spec_python();
TreeSitterLanguageSpec tree_sitter_language_spec_javascript();
TreeSitterLanguageSpec tree_sitter_language_spec_typescript();
TreeSitterLanguageSpec tree_sitter_language_spec_tsx();
TreeSitterLanguageSpec tree_sitter_language_spec_rust();
TreeSitterLanguageSpec tree_sitter_language_spec_go();
TreeSitterLanguageSpec tree_sitter_language_spec_json();
TreeSitterLanguageSpec tree_sitter_language_spec_html();
TreeSitterLanguageSpec tree_sitter_language_spec_css();
TreeSitterLanguageSpec tree_sitter_language_spec_bash();
TreeSitterLanguageSpec tree_sitter_language_spec_lua();
TreeSitterLanguageSpec tree_sitter_language_spec_markdown();
TreeSitterLanguageSpec tree_sitter_language_spec_toml();
TreeSitterLanguageSpec tree_sitter_language_spec_yaml();
TreeSitterLanguageSpec tree_sitter_language_spec_ruby();
TreeSitterLanguageSpec tree_sitter_language_spec_php();
TreeSitterLanguageSpec tree_sitter_language_spec_java();
TreeSitterLanguageSpec tree_sitter_language_spec_kotlin();
TreeSitterLanguageSpec tree_sitter_language_spec_swift();
TreeSitterLanguageSpec tree_sitter_language_spec_c_sharp();
TreeSitterLanguageSpec tree_sitter_language_spec_sql();
TreeSitterLanguageSpec tree_sitter_language_spec_cmake();
TreeSitterLanguageSpec tree_sitter_language_spec_dockerfile();
TreeSitterLanguageSpec tree_sitter_language_spec_make();
TreeSitterLanguageSpec tree_sitter_language_spec_ini();
TreeSitterLanguageSpec tree_sitter_language_spec_xml();
TreeSitterLanguageSpec tree_sitter_language_spec_vue();
TreeSitterLanguageSpec tree_sitter_language_spec_svelte();
TreeSitterLanguageSpec tree_sitter_language_spec_zig();
TreeSitterLanguageSpec tree_sitter_language_spec_scala();
TreeSitterLanguageSpec tree_sitter_language_spec_elixir();
TreeSitterLanguageSpec tree_sitter_language_spec_erlang();
TreeSitterLanguageSpec tree_sitter_language_spec_haskell();
TreeSitterLanguageSpec tree_sitter_language_spec_ocaml();
TreeSitterLanguageSpec tree_sitter_language_spec_r();
TreeSitterLanguageSpec tree_sitter_language_spec_perl();
TreeSitterLanguageSpec tree_sitter_language_spec_dart();
TreeSitterLanguageSpec tree_sitter_language_spec_hcl();
TreeSitterLanguageSpec tree_sitter_language_spec_nix();
TreeSitterLanguageSpec tree_sitter_language_spec_vim();
TreeSitterLanguageSpec tree_sitter_language_spec_vimdoc();
TreeSitterLanguageSpec tree_sitter_language_spec_query();
TreeSitterLanguageSpec tree_sitter_language_spec_regex();
TreeSitterLanguageSpec tree_sitter_language_spec_diff();
TreeSitterLanguageSpec tree_sitter_language_spec_git_config();
TreeSitterLanguageSpec tree_sitter_language_spec_gitcommit();
TreeSitterLanguageSpec tree_sitter_language_spec_csv();
TreeSitterLanguageSpec tree_sitter_language_spec_proto();
TreeSitterLanguageSpec tree_sitter_language_spec_graphql();
TreeSitterLanguageSpec tree_sitter_language_spec_latex();
TreeSitterLanguageSpec tree_sitter_language_spec_bibtex();
TreeSitterLanguageSpec tree_sitter_language_spec_asm();
TreeSitterLanguageSpec tree_sitter_language_spec_verilog();
TreeSitterLanguageSpec tree_sitter_language_spec_systemverilog();
TreeSitterLanguageSpec tree_sitter_language_spec_fortran();
TreeSitterLanguageSpec tree_sitter_language_spec_gdscript();
TreeSitterLanguageSpec tree_sitter_language_spec_glsl();
TreeSitterLanguageSpec tree_sitter_language_spec_wgsl();
TreeSitterLanguageSpec tree_sitter_language_spec_zsh();
TreeSitterLanguageSpec tree_sitter_language_spec_fish();

namespace TreeSitterLanguageSpecs {
const std::vector<TreeSitterLanguageSpec> &all() {
  static const std::vector<TreeSitterLanguageSpec> specs = {
      tree_sitter_language_spec_c(),
      tree_sitter_language_spec_cpp(),
      tree_sitter_language_spec_python(),
      tree_sitter_language_spec_javascript(),
      tree_sitter_language_spec_typescript(),
      tree_sitter_language_spec_tsx(),
      tree_sitter_language_spec_rust(),
      tree_sitter_language_spec_go(),
      tree_sitter_language_spec_json(),
      tree_sitter_language_spec_html(),
      tree_sitter_language_spec_css(),
      tree_sitter_language_spec_bash(),
      tree_sitter_language_spec_lua(),
      tree_sitter_language_spec_markdown(),
      tree_sitter_language_spec_toml(),
      tree_sitter_language_spec_yaml(),
      tree_sitter_language_spec_ruby(),
      tree_sitter_language_spec_php(),
      tree_sitter_language_spec_java(),
      tree_sitter_language_spec_kotlin(),
      tree_sitter_language_spec_swift(),
      tree_sitter_language_spec_c_sharp(),
      tree_sitter_language_spec_sql(),
      tree_sitter_language_spec_cmake(),
      tree_sitter_language_spec_dockerfile(),
      tree_sitter_language_spec_make(),
      tree_sitter_language_spec_ini(),
      tree_sitter_language_spec_xml(),
      tree_sitter_language_spec_vue(),
      tree_sitter_language_spec_svelte(),
      tree_sitter_language_spec_zig(),
      tree_sitter_language_spec_scala(),
      tree_sitter_language_spec_elixir(),
      tree_sitter_language_spec_erlang(),
      tree_sitter_language_spec_haskell(),
      tree_sitter_language_spec_ocaml(),
      tree_sitter_language_spec_r(),
      tree_sitter_language_spec_perl(),
      tree_sitter_language_spec_dart(),
      tree_sitter_language_spec_hcl(),
      tree_sitter_language_spec_nix(),
      tree_sitter_language_spec_vim(),
      tree_sitter_language_spec_vimdoc(),
      tree_sitter_language_spec_query(),
      tree_sitter_language_spec_regex(),
      tree_sitter_language_spec_diff(),
      tree_sitter_language_spec_git_config(),
      tree_sitter_language_spec_gitcommit(),
      tree_sitter_language_spec_csv(),
      tree_sitter_language_spec_proto(),
      tree_sitter_language_spec_graphql(),
      tree_sitter_language_spec_latex(),
      tree_sitter_language_spec_bibtex(),
      tree_sitter_language_spec_asm(),
      tree_sitter_language_spec_verilog(),
      tree_sitter_language_spec_systemverilog(),
      tree_sitter_language_spec_fortran(),
      tree_sitter_language_spec_gdscript(),
      tree_sitter_language_spec_glsl(),
      tree_sitter_language_spec_wgsl(),
      tree_sitter_language_spec_zsh(),
      tree_sitter_language_spec_fish(),
  };
  return specs;
}

const TreeSitterLanguageSpec *find(const std::string &language) {
  const std::string normalized = TreeSitterCatalog::normalize_language_name(language);
  for (const auto &spec : all()) {
    if (spec.name == normalized) {
      return &spec;
    }
  }
  return nullptr;
}

std::string highlight_query_for_language(const std::string &language) {
  const auto *spec = find(language);
  return spec ? spec->highlight_query : std::string();
}

std::string minimal_query_for_language(const std::string &language) {
  const auto *spec = find(language);
  return spec ? spec->minimal_query : std::string();
}
} // namespace TreeSitterLanguageSpecs
