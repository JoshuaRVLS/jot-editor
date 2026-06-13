#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_glsl() {
  TreeSitterLanguageSpec spec;
  spec.name = "glsl";
  spec.url = "https://github.com/theHamsta/tree-sitter-glsl";
  spec.extensions = {".glsl", ".vert", ".frag"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
