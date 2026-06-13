#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_wgsl() {
  TreeSitterLanguageSpec spec;
  spec.name = "wgsl";
  spec.url = "https://github.com/szebniok/tree-sitter-wgsl";
  spec.extensions = {".wgsl"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
