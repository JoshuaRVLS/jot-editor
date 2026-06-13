#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"and" @keyword
"break" @keyword
"do" @keyword
"else" @keyword
"elseif" @keyword
"end" @keyword
"false" @keyword
"for" @keyword
"function" @keyword
"goto" @keyword
"if" @keyword
"in" @keyword
"local" @keyword
"nil" @keyword
"not" @keyword
"or" @keyword
"repeat" @keyword
"return" @keyword
"then" @keyword
"true" @keyword
"until" @keyword
"while" @keyword
(string) @string
(comment) @comment
(number) @number
(function_call name: (identifier) @function)
(function_declaration name: (identifier) @function)
(identifier) @variable
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_lua() {
  TreeSitterLanguageSpec spec;
  spec.name = "lua";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-lua";
  spec.extensions = {".lua"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
