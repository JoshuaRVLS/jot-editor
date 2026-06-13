#include "tree_sitter_manager.h"
#include "tree_sitter_catalog.h"
#include "tree_sitter_language_spec.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef JOT_TREESITTER
#include <dlfcn.h>
#endif

namespace {
namespace fs = std::filesystem;

std::string trim_copy(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> split_list(const std::string &text, char delimiter = ':') {
  std::vector<std::string> out;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    item = trim_copy(item);
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

fs::path home_path(const std::string &suffix) {
  const char *home = getenv("HOME");
  if (!home) return fs::path();
  return fs::path(home) / suffix;
}
}

TreeSitterManager::TreeSitterManager() { register_languages(); }
TreeSitterManager::~TreeSitterManager() {
#ifdef JOT_TREESITTER
  for (auto &entry : query_cache_) {
    if (entry.second) {
      ts_query_delete(entry.second);
    }
  }
  for (auto &entry : library_handles_) {
    if (entry.second) {
      dlclose(entry.second);
    }
  }
#endif
}

void TreeSitterManager::register_languages() {
  ext_to_lang_.clear();
  languages_.clear();
  for (const auto &catalog_entry : TreeSitterCatalog::entries()) {
    TSLanguageEntry entry;
    entry.language_id = catalog_entry.name;
    entry.highlight_query_source =
        TreeSitterLanguageSpecs::highlight_query_for_language(catalog_entry.name);
    languages_[catalog_entry.name] = std::move(entry);
    for (const auto &ext : catalog_entry.extensions) {
      ext_to_lang_[ext] = catalog_entry.name;
    }
  }

  std::vector<std::string> default_library_paths;
  const char *env_paths = getenv("JOT_TREESITTER_PATH");
  if (env_paths && *env_paths) {
    default_library_paths = split_list(env_paths, ':');
  }
  for (const auto &path : {
           home_path(".local/lib/jot/tree-sitter"),
           home_path(".local/lib"),
           fs::path("/usr/local/lib"),
           fs::path("/usr/lib"),
           fs::path("/opt/homebrew/lib"),
       }) {
    if (!path.empty()) {
      default_library_paths.push_back(path.string());
    }
  }
  runtime_library_paths_ = default_library_paths;

  std::vector<std::string> default_query_paths;
  const char *env_query_paths = getenv("JOT_TREESITTER_QUERY_PATH");
  if (env_query_paths && *env_query_paths) {
    default_query_paths = split_list(env_query_paths, ':');
  }
  for (const auto &path : {
           home_path(".config/jot/treesitter/queries"),
           home_path(".local/share/jot/treesitter/queries"),
       }) {
    if (!path.empty()) {
      default_query_paths.push_back(path.string());
    }
  }
  runtime_query_paths_ = default_query_paths;
}

const TSLanguageEntry *TreeSitterManager::get_language(const std::string &extension) const {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;
  auto lang_it = languages_.find(ext_it->second);
  if (lang_it == languages_.end()) return nullptr;
  return &lang_it->second;
}

bool TreeSitterManager::has_language(const std::string &extension) const {
  return get_language(extension) != nullptr;
}

bool TreeSitterManager::has_language_override(
    const std::string &extension) const {
  return language_override_extensions_.find(extension) !=
         language_override_extensions_.end();
}

std::string
TreeSitterManager::language_id_for_extension(const std::string &extension) const {
  const TSLanguageEntry *entry = get_language(extension);
  return entry ? entry->language_id : "";
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_extension(
    const std::string &extension) const {
  TreeSitterRuntimeStatus status;
  status.language_id = language_id_for_extension(extension);
  status.has_language = !status.language_id.empty();
  if (!status.has_language) {
    status.parser_message = "unsupported extension";
    return status;
  }
#ifdef JOT_TREESITTER
  auto parser_it = parser_languages_.find(status.language_id);
  status.parser_loaded =
      parser_it != parser_languages_.end() && parser_it->second != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded"
                                                      : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded =
      query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query =
      runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query =
      builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded"
                                                    : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_language(
    const std::string &language_id) const {
  TreeSitterRuntimeStatus status;
  status.language_id = TreeSitterCatalog::normalize_language_name(language_id);
  status.has_language = !status.language_id.empty() &&
                        languages_.find(status.language_id) != languages_.end();
  if (!status.has_language) {
    status.parser_message = "unsupported language";
    return status;
  }
#ifdef JOT_TREESITTER
  status.parser_loaded = load_language(status.language_id) != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded"
                                                      : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded =
      query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query =
      runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query =
      builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded"
                                                    : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

void TreeSitterManager::set_runtime_options(
    const std::vector<std::string> &library_paths,
    const std::vector<std::string> &query_paths,
    const std::vector<std::string> &language_overrides) {
  if (!library_paths.empty()) {
    runtime_library_paths_.insert(runtime_library_paths_.begin(),
                                  library_paths.begin(), library_paths.end());
  }
  if (!query_paths.empty()) {
    runtime_query_paths_.insert(runtime_query_paths_.begin(), query_paths.begin(),
                                query_paths.end());
  }
  for (const auto &raw : language_overrides) {
    size_t sep = raw.find(':');
    if (sep == std::string::npos) {
      sep = raw.find('=');
    }
    if (sep == std::string::npos) {
      continue;
    }
    std::string ext = trim_copy(raw.substr(0, sep));
    std::string lang =
        TreeSitterCatalog::normalize_language_name(trim_copy(raw.substr(sep + 1)));
    if (ext.empty() || lang.empty()) {
      continue;
    }
    if (ext.front() != '.') {
      ext = "." + ext;
    }
    if (languages_.find(lang) == languages_.end()) {
      TSLanguageEntry entry;
      entry.language_id = lang;
      entry.highlight_query_source =
        TreeSitterLanguageSpecs::highlight_query_for_language(lang);
      languages_[lang] = std::move(entry);
    }
    ext_to_lang_[ext] = lang;
    language_override_extensions_.insert(ext);
  }
}

void TreeSitterManager::reload() {
#ifdef JOT_TREESITTER
  for (auto &entry : query_cache_) {
    if (entry.second) {
      ts_query_delete(entry.second);
    }
  }
  query_cache_.clear();
  parser_languages_.clear();
  for (auto &entry : library_handles_) {
    if (entry.second) {
      dlclose(entry.second);
    }
  }
  library_handles_.clear();
  parser_diagnostics_.clear();
  query_diagnostics_.clear();
  runtime_query_used_.clear();
  builtin_query_used_.clear();
#endif
}
