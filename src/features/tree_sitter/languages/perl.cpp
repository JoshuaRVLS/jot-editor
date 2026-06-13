#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_perl() {
  TreeSitterLanguageSpec spec;
  spec.name = "perl";
  spec.url = "https://github.com/tree-sitter-perl/tree-sitter-perl";
  spec.extensions = {".pl", ".pm"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
