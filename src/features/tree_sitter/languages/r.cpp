#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_r() {
  TreeSitterLanguageSpec spec;
  spec.name = "r";
  spec.url = "https://github.com/r-lib/tree-sitter-r";
  spec.extensions = {".r"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
