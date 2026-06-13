#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_git_config() {
  TreeSitterLanguageSpec spec;
  spec.name = "git_config";
  spec.url = "https://github.com/the-mikedavis/tree-sitter-git-config";
  spec.extensions = {".gitconfig"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
