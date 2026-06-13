#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
"if" @keyword
"then" @keyword
"else" @keyword
"elif" @keyword
"fi" @keyword
"case" @keyword
"esac" @keyword
"for" @keyword
"while" @keyword
"until" @keyword
"do" @keyword
"done" @keyword
"in" @keyword
"function" @keyword
"declare" @keyword
"local" @keyword
"export" @keyword
"return" @keyword
(string) @string
(raw_string) @string
(comment) @comment
(variable_name) @variable
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_bash() {
  TreeSitterLanguageSpec spec;
  spec.name = "bash";
  spec.url = "https://github.com/tree-sitter/tree-sitter-bash";
  spec.extensions = {".sh", ".bash"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
