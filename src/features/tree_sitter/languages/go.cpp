#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"chan" @keyword
"const" @keyword
"continue" @keyword
"default" @keyword
"defer" @keyword
"else" @keyword
"fallthrough" @keyword
"for" @keyword
"func" @keyword
"go" @keyword
"goto" @keyword
"if" @keyword
"import" @keyword
"interface" @keyword
"map" @keyword
"package" @keyword
"range" @keyword
"return" @keyword
"select" @keyword
"struct" @keyword
"switch" @keyword
"type" @keyword
"var" @keyword
(interpreted_string_literal) @string
(raw_string_literal) @string
(comment) @comment
(int_literal) @number
(float_literal) @number
(call_expression function: (identifier) @function)
(function_declaration name: (identifier) @function)
(type_declaration (type_spec name: (type_identifier) @type))
(identifier) @variable
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_go() {
  TreeSitterLanguageSpec spec;
  spec.name = "go";
  spec.url = "https://github.com/tree-sitter/tree-sitter-go";
  spec.extensions = {".go"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
