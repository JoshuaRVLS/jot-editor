#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(block_mapping_pair key: (flow_node) @property)
(block_mapping_pair key: (flow_node (plain_scalar (string_scalar) @property)))
(flow_node (plain_scalar (string_scalar) @string))
(flow_node (single_quote_scalar) @string)
(flow_node (double_quote_scalar) @string)
(integer_scalar) @number
(float_scalar) @number
(boolean_scalar) @keyword
(null_scalar) @keyword
(comment) @comment
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_yaml() {
  TreeSitterLanguageSpec spec;
  spec.name = "yaml";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-yaml";
  spec.extensions = {".yml", ".yaml"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
