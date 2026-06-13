#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_svelte() {
  TreeSitterLanguageSpec spec;
  spec.name = "svelte";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-svelte";
  spec.extensions = {".svelte"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
