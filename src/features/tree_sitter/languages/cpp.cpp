#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"break" @keyword.control
"case" @keyword.control
"class" @keyword
"const" @keyword.storage
"constexpr" @keyword.storage
"continue" @keyword.control
"default" @keyword.control
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword.control
"goto" @keyword.control
"if" @keyword.control
"namespace" @keyword
"new" @keyword
"return" @keyword.control
"sizeof" @keyword
"static" @keyword.storage
"struct" @keyword
"switch" @keyword.control
"template" @keyword
"typedef" @keyword
"typename" @keyword
"union" @keyword
"using" @keyword
"virtual" @keyword.storage
"volatile" @keyword.storage
"while" @keyword.control
(escape_sequence) @string.escape
(raw_string_literal) @string
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type.builtin
(type_identifier) @type
(template_type) @type
(namespace_identifier) @namespace
(qualified_identifier scope: (namespace_identifier) @namespace)
(qualified_identifier name: (type_identifier) @type)
(field_identifier) @property
(identifier) @variable
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function.method))
(call_expression function: (qualified_identifier name: (identifier) @function))
(call_expression function: (qualified_identifier name: (field_identifier) @function.method))
(call_expression function: (qualified_identifier name: (operator_name) @function))
(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (qualified_identifier name: (identifier) @function))
(function_declarator declarator: (field_identifier) @function.method)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
(function_definition declarator: (function_declarator declarator: (qualified_identifier name: (identifier) @function)))
"#include" @keyword.directive
"#define" @keyword.directive
(preproc_include) @keyword.directive
(preproc_def) @keyword.directive
(preproc_if) @keyword.directive
(preproc_elif) @keyword.directive
(preproc_else) @keyword.directive
(preproc_endif) @keyword.directive
(preproc_arg) @constant.macro
(operator_name) @operator
"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket
"<" @punctuation.bracket
">" @punctuation.bracket
"," @punctuation.delimiter
";" @punctuation.delimiter
"." @punctuation.delimiter
"::" @punctuation.delimiter
"->" @operator
"=" @operator
"+" @operator
"-" @operator
"*" @operator
"/" @operator
)TREEQUERY"; }

const char *minimal_query_source() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"for" @keyword
"if" @keyword
"namespace" @keyword
"return" @keyword
"struct" @keyword
"switch" @keyword
"template" @keyword
"typename" @keyword
"using" @keyword
"while" @keyword
(raw_string_literal) @string
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(field_identifier) @property
(call_expression function: (identifier) @function)
(function_declarator declarator: (identifier) @function)
(preproc_include) @keyword
)TREEQUERY"; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_cpp() {
  TreeSitterLanguageSpec spec;
  spec.name = "cpp";
  spec.url = "https://github.com/tree-sitter/tree-sitter-cpp";
  spec.extensions = {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
