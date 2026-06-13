#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(tag_name) @tag
(attribute_name) @attribute
(attribute_value) @string
(comment) @comment
(text) @text
"<" @punctuation
">" @punctuation
"</" @punctuation
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_html() {
  TreeSitterLanguageSpec spec;
  spec.name = "html";
  spec.url = "https://github.com/tree-sitter/tree-sitter-html";
  spec.extensions = {".html", ".htm"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
