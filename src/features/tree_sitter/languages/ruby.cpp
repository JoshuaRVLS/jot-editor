#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_ruby() {
  TreeSitterLanguageSpec spec;
  spec.name = "ruby";
  spec.url = "https://github.com/tree-sitter/tree-sitter-ruby";
  spec.extensions = {".rb"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
