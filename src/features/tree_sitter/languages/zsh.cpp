#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_zsh() {
  TreeSitterLanguageSpec spec;
  spec.name = "zsh";
  spec.url = "https://github.com/georgeharker/tree-sitter-zsh";
  spec.extensions = {".zsh"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
