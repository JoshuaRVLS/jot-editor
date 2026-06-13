#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"break" @keyword.control
"case" @keyword.control
"const" @keyword.storage
"continue" @keyword.control
"default" @keyword.control
"do" @keyword
"else" @keyword
"enum" @keyword
"extern" @keyword
"for" @keyword.control
"goto" @keyword.control
"if" @keyword.control
"return" @keyword.control
"sizeof" @keyword
"static" @keyword
"struct" @keyword
"switch" @keyword.control
"typedef" @keyword
"union" @keyword
"volatile" @keyword
"while" @keyword.control
(escape_sequence) @string.escape
(string_literal) @string
(system_lib_string) @string
(comment) @comment
(number_literal) @number
(primitive_type) @type.builtin
(type_identifier) @type
(type_descriptor) @type
(field_identifier) @property
(identifier) @variable
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (field_identifier) @function.method))
(function_declarator declarator: (identifier) @function)
(function_definition declarator: (function_declarator declarator: (identifier) @function))
"#include" @keyword.directive
"#define" @keyword.directive
(preproc_include) @keyword.directive
(preproc_def) @keyword.directive
(preproc_if) @keyword.directive
(preproc_elif) @keyword.directive
(preproc_else) @keyword.directive
(preproc_endif) @keyword.directive
(preproc_arg) @constant.macro
"(" @punctuation.bracket
")" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"{" @punctuation.bracket
"}" @punctuation.bracket
"," @punctuation.delimiter
";" @punctuation.delimiter
"." @punctuation.delimiter
"->" @operator
"=" @operator
"+" @operator
"-" @operator
"*" @operator
"/" @operator
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_c() {
  TreeSitterLanguageSpec spec;
  spec.name = "c";
  spec.url = "https://github.com/tree-sitter/tree-sitter-c";
  spec.extensions = {".c", ".h"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
