#include "tree_sitter/manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace
{
  namespace fs = std::filesystem;

  std::string read_file(const fs::path &path)
  {
    std::ifstream file(path);
    if (!file.is_open())
      return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }
} // namespace

TreeSitterManager::QuerySource
TreeSitterManager::load_query_source(const std::string &language_name) const
{
  // Bundled Lua queries are the curated source; runtime query packages (from
  // :tsinstall or user query paths) are the fallback for languages whose
  // bundled query does not match the installed parser's grammar version.
  // Runtime-first here would let an older query package shadow a newer
  // bundled query on every cache miss (the deferred boot compile installs the
  // bundled one, but the next cache miss re-loads the stale runtime file).
  auto entry_it = languages_.find(language_name);
  if (entry_it != languages_.end() && !entry_it->second.highlight_query_source.empty())
  {
    QuerySource query;
    query.source = entry_it->second.highlight_query_source;
    query.stored = true;
    return query;
  }
  for (const auto &root : runtime_query_paths_)
  {
    fs::path base(root);
    for (const auto &candidate : {
             base / language_name / "highlights.scm",
             base / (language_name + ".scm"),
         })
    {
      if (fs::exists(candidate))
      {
        std::string source = read_file(candidate);
        QuerySource query;
        query.source = source;
        query.path = candidate.string();
        query.runtime = true;
        return query;
      }
    }
  }
  QuerySource query;
  query.source.clear();
  query.runtime = false;
  return query;
}

#ifdef JOT_TREESITTER
TSQuery *TreeSitterManager::get_highlight_query(const std::string &extension)
{
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end())
    return nullptr;
  const std::string &language_id = ext_it->second;
  auto cached = query_cache_.find(language_id);
  if (cached != query_cache_.end())
  {
    return cached->second;
  }

  TSParser *parser = create_parser(extension);
  if (!parser)
  {
    query_diagnostics_[language_id] = "parser unavailable";
    return nullptr;
  }
  const TSLanguage *lang = ts_parser_language(parser);
  ts_parser_delete(parser);
  if (!lang)
  {
    query_diagnostics_[language_id] = "parser has no language";
    return nullptr;
  }

  // Candidate query sources in priority order. The bundled Lua query is the
  // curated source; runtime query packages (from :tsinstall or user query
  // paths) are the fallback for languages whose bundled query predates the
  // installed parser's grammar. Runtime-first here would let an older query
  // package shadow a newer bundled query on every cache miss.
  std::vector<QuerySource> candidates;
  {
    auto entry_it = languages_.find(language_id);
    if (entry_it != languages_.end() && !entry_it->second.highlight_query_source.empty())
    {
      QuerySource stored;
      stored.source = entry_it->second.highlight_query_source;
      stored.stored = true;
      candidates.push_back(std::move(stored));
    }
    for (const auto &root : runtime_query_paths_)
    {
      fs::path base(root);
      for (const auto &candidate : {
               base / language_id / "highlights.scm",
               base / (language_id + ".scm"),
           })
      {
        if (fs::exists(candidate))
        {
          QuerySource runtime;
          runtime.source = read_file(candidate);
          runtime.path = candidate.string();
          runtime.runtime = true;
          candidates.push_back(std::move(runtime));
        }
      }
    }
  }
  if (candidates.empty())
  {
    query_diagnostics_[language_id] = "query unavailable; Lua policy not loaded";
    return nullptr;
  }

  std::string first_failure; // diagnostics for the highest-priority source
  for (const QuerySource &source : candidates)
  {
    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(
        lang, source.source.c_str(), static_cast<uint32_t>(source.source.size()),
        &error_offset, &error_type);
    if (query)
    {
      query_cache_[language_id] = query;
      runtime_query_used_[language_id] = source.runtime;
      builtin_query_used_[language_id] = source.stored;
      query_diagnostics_[language_id] =
          source.runtime ? ("runtime query loaded: " + source.path) : "Lua query loaded";
      return query;
    }
    const std::string error =
        "query failed: error " + std::to_string((int)error_type) + " at offset "
        + std::to_string(error_offset);
    std::string message;
    if (source.runtime)
    {
      message = "runtime query failed: " + source.path + "; ";
    }
    else if (source.stored)
    {
      message = "Lua query failed; ";
    }
    message += error;
    if (first_failure.empty())
      first_failure = message;
  }
  runtime_query_used_[language_id] = false;
  builtin_query_used_[language_id] = false;
  query_diagnostics_[language_id] = first_failure;
  return nullptr;
}
#endif

#ifdef JOT_TREESITTER
TreeSitterHandle TreeSitterManager::compile_query_handle(const std::string &extension,
                                                         const std::string &source)
{
  const std::string language_id = language_for_extension(extension);
  const TSLanguage *language = load_language(language_id);
  if (!language)
    return 0;
  std::string query_source = source;
  if (query_source.empty())
    query_source = load_query_source(language_id).source;
  uint32_t offset = 0;
  TSQueryError error = TSQueryErrorNone;
  TSQuery *query = ts_query_new(
      language, query_source.c_str(), static_cast<uint32_t>(query_source.size()), &offset, &error);
  if (!query)
    return 0;
  const TreeSitterHandle handle = next_handle_++;
  lua_queries_[handle] = query;
  return handle;
}

bool TreeSitterManager::delete_query_handle(TreeSitterHandle handle)
{
  auto it = lua_queries_.find(handle);
  if (it == lua_queries_.end())
    return false;
  ts_query_delete(it->second);
  lua_queries_.erase(it);
  return true;
}

std::vector<TreeSitterCapture>
TreeSitterManager::captures_for_handles(TreeSitterHandle query_handle,
                                        TreeSitterHandle tree_handle,
                                        uint32_t start_byte,
                                        uint32_t end_byte) const
{
  std::vector<TreeSitterCapture> result;
  auto query_it = lua_queries_.find(query_handle);
  auto tree_it = lua_trees_.find(tree_handle);
  if (query_it == lua_queries_.end() || tree_it == lua_trees_.end())
    return result;
  TSQueryCursor *cursor = ts_query_cursor_new();
  if (!cursor)
    return result;
  ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);
  ts_query_cursor_exec(cursor, query_it->second, ts_tree_root_node(tree_it->second));
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match))
  {
    for (uint16_t i = 0; i < match.capture_count; ++i)
    {
      const TSQueryCapture &capture = match.captures[i];
      uint32_t name_length = 0;
      const char *name =
          ts_query_capture_name_for_id(query_it->second, capture.index, &name_length);
      TSPoint start = ts_node_start_point(capture.node);
      TSPoint end = ts_node_end_point(capture.node);
      result.push_back({std::string(name, name_length),
                        ts_node_start_byte(capture.node),
                        ts_node_end_byte(capture.node),
                        start.row,
                        start.column,
                        end.row,
                        end.column});
    }
  }
  ts_query_cursor_delete(cursor);
  return result;
}

std::string tree_sitter_query_compile_error(uint32_t offset,
                                             TSQueryError query_error,
                                             const std::string &source)
{
  const char *kind = "invalid query";
  switch (query_error)
  {
    case TSQueryErrorSyntax:
      kind = "syntax error";
      break;
    case TSQueryErrorNodeType:
      kind = "unknown node type";
      break;
    case TSQueryErrorField:
      kind = "unknown field";
      break;
    case TSQueryErrorCapture:
      kind = "unknown capture";
      break;
    case TSQueryErrorStructure:
      kind = "invalid structure";
      break;
    case TSQueryErrorLanguage:
      kind = "language mismatch";
      break;
    default:
      break;
  }
  std::string message = std::string("query compilation failed (") + kind + " at byte "
                        + std::to_string(offset);
  if (offset < source.size())
  {
    std::string near = source.substr(
        offset, std::min<std::string::size_type>(48, source.size() - offset));
    const auto newline = near.find('\n');
    if (newline != std::string::npos)
      near.erase(newline);
    message += " near \"" + near + "\"";
  }
  message += ")";
  return message;
}

bool TreeSitterManager::set_query_source(const std::string &extension,
                                         const std::string &source,
                                         std::string &error)
{
  const std::string language_id = language_for_extension(extension);
  if (language_id.empty())
  {
    error = "unsupported language";
    return false;
  }
  auto entry_it = languages_.find(language_id);
  if (entry_it == languages_.end())
  {
    error = "language not registered";
    return false;
  }
  // Store the Lua-provided source first. A parser may not be installed yet;
  // the query is compiled lazily (or validated now when possible).
  entry_it->second.highlight_query_source = source;

#ifdef JOT_TREESITTER
  if (deferred_compile_mode_ && deferred_compile_state_ == DeferredCompileState::Idle)
  {
    // Boot-time deferred mode: record the job for the background compile and
    // return success immediately. The worker compiles it (or records the
    // precise failure) off the main thread.
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    deferred_query_jobs_.push_back({language_id,
                                    entry_it->second.symbol,
                                    entry_it->second.library_names,
                                    source});
    return true;
  }

  const TSLanguage *language = load_language(language_id);
  if (!language)
  {
    query_diagnostics_[language_id] = "Lua query stored (parser not installed yet)";
    runtime_query_used_[language_id] = false;
    builtin_query_used_[language_id] = false;
    return true;
  }

  uint32_t offset = 0;
  TSQueryError query_error = TSQueryErrorNone;
  TSQuery *query = ts_query_new(
      language, source.c_str(), static_cast<uint32_t>(source.size()), &offset, &query_error);
  if (!query)
  {
    error = tree_sitter_query_compile_error(offset, query_error, source);
    query_diagnostics_[language_id] = error;
    runtime_query_used_[language_id] = false;
    builtin_query_used_[language_id] = false;
    return false;
  }

  auto cached = query_cache_.find(language_id);
  if (cached != query_cache_.end())
    ts_query_delete(cached->second);
  query_cache_[language_id] = query;
  runtime_query_used_[language_id] = false;
  builtin_query_used_[language_id] = true;
  query_diagnostics_[language_id] = "Lua query loaded";
#endif
  return true;
}
#else
TreeSitterHandle TreeSitterManager::compile_query_handle(const std::string &, const std::string &)
{
  return 0;
}
bool TreeSitterManager::delete_query_handle(TreeSitterHandle)
{
  return false;
}
std::vector<TreeSitterCapture> TreeSitterManager::captures_for_handles(TreeSitterHandle,
                                                                       TreeSitterHandle,
                                                                       uint32_t,
                                                                       uint32_t) const
{
  return {};
}
bool TreeSitterManager::set_query_source(const std::string &extension,
                                         const std::string &source,
                                         std::string &error)
{
  const std::string language_id = language_for_extension(extension);
  if (language_id.empty())
  {
    error = "unsupported language";
    return false;
  }
  auto entry_it = languages_.find(language_id);
  if (entry_it == languages_.end())
  {
    error = "language not registered";
    return false;
  }
  entry_it->second.highlight_query_source = source;
  return true;
}
#endif
