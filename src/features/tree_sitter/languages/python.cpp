#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"and" @keyword
"as" @keyword
"assert" @keyword
"async" @keyword
"await" @keyword
"break" @keyword
"class" @keyword
"continue" @keyword
"def" @keyword
"del" @keyword
"elif" @keyword
"else" @keyword
"except" @keyword
"finally" @keyword
"for" @keyword
"from" @keyword
"global" @keyword
"if" @keyword
"import" @keyword
"in" @keyword
"is" @keyword
"lambda" @keyword
"nonlocal" @keyword
"not" @keyword
"or" @keyword
"pass" @keyword
"raise" @keyword
"return" @keyword
"try" @keyword
"while" @keyword
"with" @keyword
"yield" @keyword
"True" @keyword
"False" @keyword
"None" @keyword
(string) @string
(comment) @comment
(integer) @number
(float) @number
(call function: (identifier) @function)
(call function: (attribute attribute: (identifier) @function))
(function_definition name: (identifier) @function)
(class_definition name: (identifier) @type)
(attribute attribute: (identifier) @type)
(identifier) @variable
(parameters (identifier) @variable.parameter)
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_python() {
  TreeSitterLanguageSpec spec;
  spec.name = "python";
  spec.url = "https://github.com/tree-sitter/tree-sitter-python";
  spec.extensions = {".py"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
