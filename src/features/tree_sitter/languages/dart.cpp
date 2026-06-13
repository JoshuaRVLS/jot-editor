#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_dart() {
  TreeSitterLanguageSpec spec;
  spec.name = "dart";
  spec.url = "https://github.com/UserNobody14/tree-sitter-dart";
  spec.extensions = {".dart"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
