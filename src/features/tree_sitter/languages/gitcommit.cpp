#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_gitcommit() {
  TreeSitterLanguageSpec spec;
  spec.name = "gitcommit";
  spec.url = "https://github.com/gbprod/tree-sitter-gitcommit";
  spec.extensions = {".gitcommit"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
