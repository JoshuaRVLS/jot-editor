#include "tree_sitter/catalog.h"
#include "tree_sitter/language_spec.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

std::string replace_copy(std::string value, char from, char to) {
  std::replace(value.begin(), value.end(), from, to);
  return value;
}

std::string strip_suffix(std::string value, const std::string &suffix) {
  if (value.size() >= suffix.size() &&
      value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
    value.erase(value.size() - suffix.size());
  }
  return value;
}

std::vector<std::string> default_library_names(const std::string &name) {
  const std::string hyphen = replace_copy(name, '_', '-');
  const std::string underscore = replace_copy(name, '-', '_');
  std::vector<std::string> names = {
      "libtree-sitter-" + hyphen + ".so",
      "libtree-sitter-" + hyphen + ".dylib",
      "libtree_sitter_" + underscore + ".so",
      "libtree_sitter_" + underscore + ".dylib",
      "tree-sitter-" + hyphen + ".so",
      "tree-sitter-" + hyphen + ".dylib",
  };
  if (hyphen != name) {
    names.push_back("libtree-sitter-" + name + ".so");
    names.push_back("libtree-sitter-" + name + ".dylib");
  }
  return names;
}

TreeSitterCatalogEntry make_entry(
    std::string name, std::string url, std::vector<std::string> extensions,
    std::string source_subdir = "",
    std::vector<std::string> library_names = {}) {
  name = TreeSitterCatalog::normalize_language_name(name);
  TreeSitterCatalogEntry entry;
  entry.name = name;
  entry.url = std::move(url);
  entry.symbol = "tree_sitter_" + replace_copy(name, '-', '_');
  entry.source_subdir = std::move(source_subdir);
  entry.extensions = std::move(extensions);
  entry.library_names =
      library_names.empty() ? default_library_names(name) : std::move(library_names);
  return entry;
}
} // namespace

namespace TreeSitterCatalog {
std::string normalize_language_name(const std::string &language) {
  std::string out = lower_copy(language);
  for (char &c : out) {
    if (c == '-' || c == ' ') {
      c = '_';
    }
  }
  return out;
}

bool is_github_url(const std::string &value) {
  std::string lower = lower_copy(value);
  return lower.rfind("https://github.com/", 0) == 0 ||
         lower.rfind("http://github.com/", 0) == 0 ||
         lower.rfind("github.com/", 0) == 0;
}

std::string normalize_github_url(const std::string &url) {
  std::string lower = lower_copy(url);
  if (lower.rfind("github.com/", 0) == 0) {
    return "https://" + url;
  }
  return url;
}

std::string language_name_from_url(const std::string &url) {
  std::string value = url;
  value = strip_suffix(value, "/");
  value = strip_suffix(value, ".git");
  size_t slash = value.find_last_of('/');
  std::string repo = slash == std::string::npos ? value : value.substr(slash + 1);
  repo = lower_copy(repo);
  if (repo.rfind("tree-sitter-", 0) == 0) {
    repo.erase(0, std::string("tree-sitter-").size());
  }
  return normalize_language_name(repo);
}

const std::vector<TreeSitterCatalogEntry> &entries() {
  static const std::vector<TreeSitterCatalogEntry> catalog = [] {
    std::vector<TreeSitterCatalogEntry> out;
    for (const auto &spec : TreeSitterLanguageSpecs::all()) {
      TreeSitterCatalogEntry entry;
      entry.name = spec.name;
      entry.url = spec.url;
      entry.symbol = "tree_sitter_" + replace_copy(spec.name, '-', '_');
      entry.source_subdir = spec.source_subdir;
      entry.extensions = spec.extensions;
      entry.library_names = spec.library_names.empty()
                                ? default_library_names(spec.name)
                                : spec.library_names;
      out.push_back(std::move(entry));
    }
    return out;
  }();
  return catalog;
}

const TreeSitterCatalogEntry *find_language(const std::string &language) {
  const std::string normalized = normalize_language_name(language);
  for (const auto &entry : entries()) {
    if (entry.name == normalized) {
      return &entry;
    }
  }
  return nullptr;
}

TreeSitterCatalogEntry entry_for_github_url(const std::string &url) {
  std::string name = language_name_from_url(url);
  return make_entry(name, normalize_github_url(url), {});
}

std::vector<std::string> language_names() {
  std::vector<std::string> names;
  for (const auto &entry : entries()) {
    names.push_back(entry.name);
  }
  return names;
}
} // namespace TreeSitterCatalog
