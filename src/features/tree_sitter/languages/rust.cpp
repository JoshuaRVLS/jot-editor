#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"as" @keyword
"async" @keyword
"await" @keyword
"break" @keyword
"const" @keyword
"continue" @keyword
"crate" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"fn" @keyword
"for" @keyword
"if" @keyword
"impl" @keyword
"in" @keyword
"let" @keyword
"loop" @keyword
"match" @keyword
"mod" @keyword
"move" @keyword
"mut" @keyword
"pub" @keyword
"ref" @keyword
"return" @keyword
"self" @keyword
"static" @keyword
"struct" @keyword
"super" @keyword
"trait" @keyword
"type" @keyword
"union" @keyword
"unsafe" @keyword
"use" @keyword
"where" @keyword
"while" @keyword
(string_literal) @string
(line_comment) @comment
(block_comment) @comment
(float_literal) @number
(integer_literal) @number
(macro_invocation macro: (identifier) @function)
(call_expression function: (identifier) @function)
(function_item name: (identifier) @function)
(struct_item name: (type_identifier) @type)
(enum_item name: (type_identifier) @type)
(identifier) @variable
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_rust() {
  TreeSitterLanguageSpec spec;
  spec.name = "rust";
  spec.url = "https://github.com/tree-sitter/tree-sitter-rust";
  spec.extensions = {".rs"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
