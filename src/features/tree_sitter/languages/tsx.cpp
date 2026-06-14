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
  "abstract"
  "class"
  "const"
  "declare"
  "enum"
  "extends"
  "function"
  "implements"
  "interface"
  "let"
  "new"
  "private"
  "protected"
  "public"
  "readonly"
  "static"
  "type"
  "var"
] @keyword.storage

[
  "export"
  "import"
  "namespace"
] @keyword.directive

[
  "as"
  "delete"
  "in"
  "instanceof"
  "keyof"
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
(interface_declaration name: (type_identifier) @type)
(type_alias_declaration name: (type_identifier) @type)
(enum_declaration name: (identifier) @type)
(type_identifier) @type
(predefined_type) @type.builtin
(formal_parameters (identifier) @variable.parameter)
(required_parameter name: (identifier) @variable.parameter)
(optional_parameter name: (identifier) @variable.parameter)
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

TreeSitterLanguageSpec tree_sitter_language_spec_tsx() {
  TreeSitterLanguageSpec spec;
  spec.name = "tsx";
  spec.url = "https://github.com/tree-sitter/tree-sitter-typescript";
  spec.source_subdir = "tsx";
  spec.extensions = {".tsx"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
