#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_regex() {
  TreeSitterLanguageSpec spec;
  spec.name = "regex";
  spec.url = "https://github.com/tree-sitter/tree-sitter-regex";
  spec.extensions = {".regex"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
