#include "tree_sitter/manager.h"
#include "tree_sitter/language_spec.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
namespace fs = std::filesystem;

std::string read_file(const fs::path &path) {
  std::ifstream file(path);
  if (!file.is_open()) return "";
  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}
} // namespace

TreeSitterManager::QuerySource
TreeSitterManager::load_query_source(const std::string &language_name) const {
  for (const auto &root : runtime_query_paths_) {
    fs::path base(root);
    for (const auto &candidate : {
             base / language_name / "highlights.scm",
             base / (language_name + ".scm"),
         }) {
      std::string source = read_file(candidate);
      if (!source.empty()) {
        QuerySource query;
        query.source = source;
        query.path = candidate.string();
        query.runtime = true;
        return query;
      }
    }
  }
  auto it = languages_.find(language_name);
  QuerySource query;
  query.source = it != languages_.end() ? it->second.highlight_query_source
                                        : std::string();
  query.runtime = false;
  return query;
}

#ifdef JOT_TREESITTER
TSQuery *TreeSitterManager::get_highlight_query(const std::string &extension) {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;
  const std::string &language_id = ext_it->second;
  auto cached = query_cache_.find(language_id);
  if (cached != query_cache_.end()) return cached->second;

  TSParser *parser = create_parser(extension);
  if (!parser) {
    query_diagnostics_[language_id] = "parser unavailable";
    return nullptr;
  }
  const TSLanguage *lang = ts_parser_language(parser);
  ts_parser_delete(parser);
  if (!lang) {
    query_diagnostics_[language_id] = "parser has no language";
    return nullptr;
  }

  auto compile_query = [&](const std::string &source, uint32_t &error_offset,
                           TSQueryError &error_type) {
    return ts_query_new(lang, source.c_str(), (uint32_t)source.size(),
                        &error_offset, &error_type);
  };

  auto compile_empty_query = [&]() {
    uint32_t empty_error_offset = 0;
    TSQueryError empty_error_type = TSQueryErrorNone;
    return compile_query("", empty_error_offset, empty_error_type);
  };

  QuerySource source = load_query_source(language_id);
  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  TSQuery *query = compile_query(source.source, error_offset, error_type);
  runtime_query_used_[language_id] = source.runtime && query != nullptr;
  builtin_query_used_[language_id] = !source.runtime && query != nullptr;
  if (query) {
    query_cache_[language_id] = query;
    query_diagnostics_[language_id] =
        source.runtime ? ("runtime query loaded: " + source.path)
                       : "built-in query loaded";
    return query;
  }

  std::string runtime_error;
  if (source.runtime) {
    runtime_error = "runtime query failed: " + source.path +
                    " error " + std::to_string((int)error_type) +
                    " at offset " + std::to_string(error_offset);
    const auto entry_it = languages_.find(language_id);
    const std::string fallback_source =
        entry_it != languages_.end() ? entry_it->second.highlight_query_source
                                     : std::string();
    error_offset = 0;
    error_type = TSQueryErrorNone;
    query = compile_query(fallback_source, error_offset, error_type);
    if (query) {
      query_cache_[language_id] = query;
      runtime_query_used_[language_id] = false;
      builtin_query_used_[language_id] = true;
      query_diagnostics_[language_id] =
          "built-in query loaded; " + runtime_error;
      return query;
    }
  }

  std::string builtin_error;
  if (!query && source.runtime) {
    builtin_error = "built-in query failed: error " +
                    std::to_string((int)error_type) + " at offset " +
                    std::to_string(error_offset);
  }

  if (!query && language_id == "cpp") {
    error_offset = 0;
    error_type = TSQueryErrorNone;
    query = compile_query(
        TreeSitterLanguageSpecs::minimal_query_for_language(language_id),
        error_offset, error_type);
    if (query) {
      query_cache_[language_id] = query;
      runtime_query_used_[language_id] = false;
      builtin_query_used_[language_id] = true;
      std::string message = "minimal built-in query loaded";
      if (!runtime_error.empty()) {
        message += "; " + runtime_error;
      }
      if (!builtin_error.empty()) {
        message += "; " + builtin_error;
      }
      query_diagnostics_[language_id] = message;
      return query;
    }
  }

  query = compile_empty_query();
  if (query) {
    query_cache_[language_id] = query;
    runtime_query_used_[language_id] = false;
    builtin_query_used_[language_id] = true;
    std::string message = "empty built-in query loaded";
    if (!runtime_error.empty()) {
      message += "; " + runtime_error;
    }
    if (!builtin_error.empty()) {
      message += "; " + builtin_error;
    } else if (source.runtime) {
      message += "; built-in query failed";
    }
    query_diagnostics_[language_id] = message;
    return query;
  }

  const std::string final_query_error =
      "query failed: error " + std::to_string((int)error_type) +
      " at offset " + std::to_string(error_offset);
  runtime_query_used_[language_id] = false;
  builtin_query_used_[language_id] = false;
  std::string message;
  if (!runtime_error.empty()) {
    message = runtime_error;
    if (!builtin_error.empty()) {
      message += "; " + builtin_error;
    }
    if (language_id == "cpp") {
      message += "; minimal built-in " + final_query_error;
    }
  } else {
    message = final_query_error;
  }
  query_diagnostics_[language_id] = message;
  return nullptr;
}
#endif
