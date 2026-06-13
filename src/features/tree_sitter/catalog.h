#ifndef TREE_SITTER_CATALOG_H
#define TREE_SITTER_CATALOG_H

#include <string>
#include <vector>

struct TreeSitterCatalogEntry {
  std::string name;
  std::string url;
  std::string symbol;
  std::string source_subdir;
  std::vector<std::string> extensions;
  std::vector<std::string> library_names;
};

namespace TreeSitterCatalog {
const std::vector<TreeSitterCatalogEntry> &entries();
const TreeSitterCatalogEntry *find_language(const std::string &language);
TreeSitterCatalogEntry entry_for_github_url(const std::string &url);
std::string normalize_language_name(const std::string &language);
std::string language_name_from_url(const std::string &url);
bool is_github_url(const std::string &value);
std::vector<std::string> language_names();
} // namespace TreeSitterCatalog

#endif
