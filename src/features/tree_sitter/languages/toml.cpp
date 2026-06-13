#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(bare_key) @property
(quoted_key) @property
(string) @string
(integer) @number
(float) @number
(boolean) @keyword
(comment) @comment
"[" @punctuation
"]" @punctuation
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_toml() {
  TreeSitterLanguageSpec spec;
  spec.name = "toml";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-toml";
  spec.extensions = {".toml"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
