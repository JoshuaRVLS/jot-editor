#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_csv() {
  TreeSitterLanguageSpec spec;
  spec.name = "csv";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-csv";
  spec.extensions = {".csv", ".tsv"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
