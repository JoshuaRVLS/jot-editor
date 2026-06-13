#include "tree_sitter/language_spec.h"

namespace {
const char *highlight_query_source() { return ""; }
const char *minimal_query_source() { return ""; }
} // namespace

TreeSitterLanguageSpec tree_sitter_language_spec_elixir() {
  TreeSitterLanguageSpec spec;
  spec.name = "elixir";
  spec.url = "https://github.com/elixir-lang/tree-sitter-elixir";
  spec.extensions = {".ex", ".exs"};
  spec.highlight_query = highlight_query_source();
  spec.minimal_query = minimal_query_source();
  return spec;
}
