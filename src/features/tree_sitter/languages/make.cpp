#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_make() {
  TreeSitterLanguageSpec spec;
  spec.name = "make";
  spec.url = "https://github.com/alemuller/tree-sitter-make";
  spec.extensions = {".make", ".mk"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
