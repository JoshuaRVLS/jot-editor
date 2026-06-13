#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"break" @keyword
"case" @keyword
"catch" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"debugger" @keyword
"default" @keyword
"delete" @keyword
"do" @keyword
"else" @keyword
"export" @keyword
"extends" @keyword
"finally" @keyword
"for" @keyword
"function" @keyword
"if" @keyword
"import" @keyword
"in" @keyword
"instanceof" @keyword
"let" @keyword
"new" @keyword
"of" @keyword
"return" @keyword
"switch" @keyword
"this" @keyword
"throw" @keyword
"try" @keyword
"typeof" @keyword
"var" @keyword
"void" @keyword
"while" @keyword
"with" @keyword
"yield" @keyword
(string) @string
(template_string) @string
(comment) @comment
(number) @number
(call_expression function: (identifier) @function)
(function_declaration name: (identifier) @function)
(class_declaration name: (identifier) @type)
(identifier) @variable
(formal_parameters (identifier) @variable.parameter)
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
