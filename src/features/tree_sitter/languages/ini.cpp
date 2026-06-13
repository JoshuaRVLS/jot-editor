#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_ini() {
  TreeSitterLanguageSpec spec;
  spec.name = "ini";
  spec.url = "https://github.com/justinmk/tree-sitter-ini";
  spec.extensions = {".ini", ".cfg", ".conf", ".properties"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
