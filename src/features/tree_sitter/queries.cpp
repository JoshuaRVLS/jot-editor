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

#ifdef JOT_TREESITTER
TreeSitterHandle TreeSitterManager::compile_query_handle(
    const std::string &extension, const std::string &source) {
  const std::string language_id = language_for_extension(extension);
  const TSLanguage *language = load_language(language_id);
  if (!language) return 0;
  std::string query_source = source;
  if (query_source.empty()) query_source = load_query_source(language_id).source;
  uint32_t offset = 0;
  TSQueryError error = TSQueryErrorNone;
  TSQuery *query = ts_query_new(language, query_source.c_str(),
                                static_cast<uint32_t>(query_source.size()),
                                &offset, &error);
  if (!query) return 0;
  const TreeSitterHandle handle = next_handle_++;
  lua_queries_[handle] = query;
  return handle;
}

bool TreeSitterManager::delete_query_handle(TreeSitterHandle handle) {
  auto it = lua_queries_.find(handle);
  if (it == lua_queries_.end()) return false;
  ts_query_delete(it->second);
  lua_queries_.erase(it);
  return true;
}

std::vector<TreeSitterCapture> TreeSitterManager::captures_for_handles(
    TreeSitterHandle query_handle, TreeSitterHandle tree_handle,
    uint32_t start_byte, uint32_t end_byte) const {
  std::vector<TreeSitterCapture> result;
  auto query_it = lua_queries_.find(query_handle);
  auto tree_it = lua_trees_.find(tree_handle);
  if (query_it == lua_queries_.end() || tree_it == lua_trees_.end()) return result;
  TSQueryCursor *cursor = ts_query_cursor_new();
  if (!cursor) return result;
  ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);
  ts_query_cursor_exec(cursor, query_it->second, ts_tree_root_node(tree_it->second));
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture &capture = match.captures[i];
      uint32_t name_length = 0;
      const char *name = ts_query_capture_name_for_id(
          query_it->second, capture.index, &name_length);
      TSPoint start = ts_node_start_point(capture.node);
      TSPoint end = ts_node_end_point(capture.node);
      result.push_back({std::string(name, name_length), ts_node_start_byte(capture.node),
                        ts_node_end_byte(capture.node), start.row, start.column,
                        end.row, end.column});
    }
  }
  ts_query_cursor_delete(cursor);
  return result;
}

bool TreeSitterManager::set_query_source(const std::string &extension,
                                         const std::string &source,
                                         std::string &error) {
  TreeSitterHandle handle = compile_query_handle(extension, source);
  if (!handle) {
    error = "query compilation failed";
    return false;
  }
  const std::string language_id = language_for_extension(extension);
  auto query_it = lua_queries_.find(handle);
  auto cached = query_cache_.find(language_id);
  if (cached != query_cache_.end()) ts_query_delete(cached->second);
  query_cache_[language_id] = query_it->second;
  lua_queries_.erase(query_it);
  runtime_query_used_[language_id] = false;
  builtin_query_used_[language_id] = false;
  query_diagnostics_[language_id] = "Lua query loaded";
  return true;
}
#else
TreeSitterHandle TreeSitterManager::compile_query_handle(const std::string &, const std::string &) { return 0; }
bool TreeSitterManager::delete_query_handle(TreeSitterHandle) { return false; }
std::vector<TreeSitterCapture> TreeSitterManager::captures_for_handles(TreeSitterHandle, TreeSitterHandle, uint32_t, uint32_t) const { return {}; }
bool TreeSitterManager::set_query_source(const std::string &, const std::string &, std::string &error) { error = "Tree-sitter runtime not available"; return false; }
#endif
