#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_dockerfile() {
  TreeSitterLanguageSpec spec;
  spec.name = "dockerfile";
  spec.url = "https://github.com/camdencheek/tree-sitter-dockerfile";
  spec.extensions = {".dockerfile"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
