#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_fortran() {
  TreeSitterLanguageSpec spec;
  spec.name = "fortran";
  spec.url = "https://github.com/stadelmanma/tree-sitter-fortran";
  spec.extensions = {".f", ".f90", ".f95"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
