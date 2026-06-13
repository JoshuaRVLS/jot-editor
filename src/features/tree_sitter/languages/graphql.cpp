#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_graphql() {
  TreeSitterLanguageSpec spec;
  spec.name = "graphql";
  spec.url = "https://github.com/bkegley/tree-sitter-graphql";
  spec.extensions = {".graphql", ".gql"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
