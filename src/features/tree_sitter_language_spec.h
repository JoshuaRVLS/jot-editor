#ifndef TREE_SITTER_LANGUAGE_SPEC_H
#define TREE_SITTER_LANGUAGE_SPEC_H

#include <string>
#include <vector>

struct TreeSitterLanguageSpec {
  std::string name;
  std::string url;
  std::string source_subdir;
  std::vector<std::string> extensions;
  std::vector<std::string> library_names;
  std::string highlight_query;
  std::string minimal_query;
};

namespace TreeSitterLanguageSpecs {
const std::vector<TreeSitterLanguageSpec> &all();
const TreeSitterLanguageSpec *find(const std::string &language);
std::string highlight_query_for_language(const std::string &language);
std::string minimal_query_for_language(const std::string &language);
} // namespace TreeSitterLanguageSpecs

#endif
