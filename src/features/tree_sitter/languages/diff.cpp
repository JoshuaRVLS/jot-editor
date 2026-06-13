#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_diff() {
  TreeSitterLanguageSpec spec;
  spec.name = "diff";
  spec.url = "https://github.com/the-mikedavis/tree-sitter-diff";
  spec.extensions = {".diff", ".patch"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
