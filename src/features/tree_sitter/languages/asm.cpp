#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_asm() {
  TreeSitterLanguageSpec spec;
  spec.name = "asm";
  spec.url = "https://github.com/rush-rs/tree-sitter-asm";
  spec.extensions = {".asm", ".s", ".S"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
