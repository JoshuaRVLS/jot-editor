#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_gdscript() {
  TreeSitterLanguageSpec spec;
  spec.name = "gdscript";
  spec.url = "https://github.com/PrestonKnopp/tree-sitter-gdscript";
  spec.extensions = {".gd"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
