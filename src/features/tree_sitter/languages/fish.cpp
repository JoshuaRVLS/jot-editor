#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_fish() {
  TreeSitterLanguageSpec spec;
  spec.name = "fish";
  spec.url = "https://github.com/ram02z/tree-sitter-fish";
  spec.extensions = {".fish"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
