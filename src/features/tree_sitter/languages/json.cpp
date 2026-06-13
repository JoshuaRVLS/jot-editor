#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(string) @string
(number) @number
"true" @keyword
"false" @keyword
"null" @keyword
(pair key: (string) @property)
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_json() {
  TreeSitterLanguageSpec spec;
  spec.name = "json";
  spec.url = "https://github.com/tree-sitter/tree-sitter-json";
  spec.extensions = {".json", ".jsonc"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
