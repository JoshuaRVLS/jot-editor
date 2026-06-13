#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_cmake() {
  TreeSitterLanguageSpec spec;
  spec.name = "cmake";
  spec.url = "https://github.com/uyha/tree-sitter-cmake";
  spec.extensions = {".cmake"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
