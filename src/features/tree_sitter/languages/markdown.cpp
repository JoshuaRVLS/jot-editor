#include "tree_sitter_language_spec.h"

namespace {
const char *highlight_query_source() { return R"TREEQUERY(
(atx_heading (heading_content) @keyword)
(setext_heading (heading_content) @keyword)
(code_fence_content) @string
(indented_code_block) @string
(fenced_code_block) @string
(emphasis) @property
(strong_emphasis) @property
(link_text) @property
(link_destination) @string
(fenced_code_block) @string
)TREEQUERY"; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_markdown() {
  TreeSitterLanguageSpec spec;
  spec.name = "markdown";
  spec.url = "https://github.com/tree-sitter-grammars/tree-sitter-markdown";
  spec.extensions = {".md", ".markdown"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
