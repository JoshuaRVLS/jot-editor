#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
[
  "break"
  "case"
  "catch"
  "continue"
  "debugger"
  "default"
  "do"
  "else"
  "finally"
  "for"
  "if"
  "return"
  "switch"
  "throw"
  "try"
  "while"
  "with"
] @keyword.control

[
  "class"
  "const"
  "extends"
  "function"
  "let"
  "new"
  "var"
] @keyword.storage

[
  "export"
  "import"
] @keyword.directive

[
  "delete"
  "in"
  "instanceof"
  "of"
  "this"
  "typeof"
  "void"
  "yield"
] @keyword

(string) @string
(template_string) @string
(comment) @comment
(number) @number
(regex) @string
(escape_sequence) @string.escape

(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (property_identifier) @function.method))
(function_declaration name: (identifier) @function)
(method_definition name: (property_identifier) @function.method)
(class_declaration name: (identifier) @type)
(formal_parameters (identifier) @variable.parameter)
(property_identifier) @property
(pair key: (property_identifier) @property)

(jsx_opening_element name: (_) @tag)
(jsx_closing_element name: (_) @tag)
(jsx_self_closing_element name: (_) @tag)
(jsx_attribute (identifier) @tag.attribute)
(jsx_attribute (property_identifier) @tag.attribute)
(jsx_attribute (jsx_namespace_name) @tag.attribute)

(identifier) @variable
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_javascript() {
  TreeSitterLanguageSpec spec;
  spec.name = "javascript";
  spec.url = "https://github.com/tree-sitter/tree-sitter-javascript";
  spec.extensions = {".js", ".jsx", ".mjs", ".cjs"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
