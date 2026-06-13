#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(tag_name) @tag
(class_name) @type
(id_name) @type
(property_name) @property
(string_value) @string
(comment) @comment
(integer_value) @number
(float_value) @number
"@" @keyword
"!important" @keyword
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_css() {
  TreeSitterLanguageSpec spec;
  spec.name = "css";
  spec.url = "https://github.com/tree-sitter/tree-sitter-css";
  spec.extensions = {".css"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
