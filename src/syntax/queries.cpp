#include "syntax/manager.h"
#include "syntax/query_predicates.h"

#include <algorithm>
#include <cctype>
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
namespace
{
  // How deep `;; inherits:` chains are followed. A query that inherits from
  // another that inherits back is a cycle the file's author cannot see, and
  // compiling it would otherwise not terminate.
  constexpr int kMaxQueryInheritance = 4;
} // namespace

std::string TreeSitterManager::extending_query_sources(const std::string &language_id) const
{
  std::string extensions;
  for (const auto &root : runtime_query_paths_)
  {
    const fs::path base(root);
    for (const auto &candidate : {
             base / language_id / "highlights.scm",
             base / (language_id + ".scm"),
         })
    {
      if (!fs::exists(candidate))
      {
        continue;
      }
      const std::string source = read_file(candidate);
      if (jot_query::parse_modeline(source).kind != jot_query::Modeline::Extends)
      {
        continue;
      }
      extensions += source;
      if (!extensions.empty() && extensions.back() != '\n')
      {
        extensions += '\n';
      }
    }
  }
  return extensions;
}

std::string TreeSitterManager::expand_query_modelines(const std::string &source,
                                                      const std::string &base_source,
                                                      int depth,
                                                      std::string *note) const
{
  const jot_query::ModelineResult modeline = jot_query::parse_modeline(source);
  if (modeline.kind == jot_query::Modeline::None || depth >= kMaxQueryInheritance)
  {
    return source;
  }
  std::string expanded;
  if (modeline.kind == jot_query::Modeline::Inherits)
  {
    for (size_t i = 0; i < modeline.languages.size(); i++)
    {
      const std::string &parent_id = modeline.languages[i];
      const bool optional = i < modeline.optional.size() && modeline.optional[i];
      const QuerySource parent = load_query_source(parent_id);
      if (parent.source.empty())
      {
        // A missing query can only be reported, never invented: the language
        // may simply not be installed. A parenthesised name asks for exactly
        // that silence.
        if (!optional && note)
        {
          *note += "; inherited language " + parent_id + " has no query";
        }
        continue;
      }
      // A parent with a modeline of its own is resolved the same way, against
      // no base: an inherited query has nothing of its own to extend.
      const std::string inherited =
          expand_query_modelines(parent.source, std::string(), depth + 1, note);
      if (!inherited.empty())
      {
        expanded += inherited;
        if (expanded.back() != '\n')
        {
          expanded += '\n';
        }
      }
    }
    if (note)
    {
      *note += "; ;; inherits " + std::to_string(modeline.languages.size()) + " language(s)";
    }
  }
  else // Modeline::Extends
  {
    // The base is the query this file would otherwise replace, which the
    // caller knows and this file does not: it is the source that wins ahead of
    // it, not any source it can look up by name.
    if (!base_source.empty())
    {
      expanded += base_source;
      if (expanded.back() != '\n')
      {
        expanded += '\n';
      }
      if (note)
      {
        *note += "; ;; extends the query it would replace";
      }
    }
  }
  expanded += source;
  return expanded;
}

// The one lookup both query kinds go through (highlights.scm, folds.scm):
// candidates in priority order, their modelines resolved, the first one that
// compiles cached and returned. `stored_source` is the copy Lua handed over
// (the bundled one), which only the highlight query has -- the fold queries
// come from the query paths, where :tsinstall writes them.
TreeSitterManager::QueryLookup
TreeSitterManager::lookup_query(const std::string &language_id,
                               const TSLanguage *lang,
                               const char *file_name,
                               const std::string &stored_source,
                               std::unordered_map<std::string, TSQuery *> &cache)
{
  QueryLookup result;
  if (lang == nullptr)
  {
    result.diagnostic = "parser unavailable";
    return result;
  }
  auto cached = cache.find(language_id);
  if (cached != cache.end())
  {
    result.query = cached->second;
    return result;
  }

  // Candidate query sources in priority order. The bundled Lua query is the
  // curated source; runtime query packages (from :tsinstall or user query
  // paths) are the fallback for languages whose bundled query predates the
  // installed parser's grammar. Runtime-first here would let an older query
  // package shadow a newer bundled query on every cache miss.
  std::vector<QuerySource> candidates;
  if (!stored_source.empty())
  {
    QuerySource stored;
    stored.source = stored_source;
    stored.stored = true;
    candidates.push_back(std::move(stored));
  }
  for (const auto &root : runtime_query_paths_)
  {
    fs::path base(root);
    for (const auto &candidate : {
             base / language_id / file_name,
             base / (language_id + ".scm"),
         })
    {
      // The `<language>.scm` spelling is the highlight query's; a fold query
      // lives in the language's own directory or nowhere.
      if (std::strcmp(file_name, "highlights.scm") != 0
          && candidate.filename().string() != file_name)
      {
        continue;
      }
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
  if (candidates.empty())
  {
    result.diagnostic = "query unavailable; Lua policy not loaded";
    return result;
  }
  // A query file may open with a modeline that changes what it contributes:
  // `;; extends` adds its patterns to the query it would otherwise replace, and
  // `;; inherits: a,b` pulls those languages' queries in ahead of its own. The
  // expansion runs here, after the whole candidate list exists, because
  // `extends` needs the source ahead of it in the list.
  for (size_t i = 0; i < candidates.size(); i++)
  {
    // The base for `;; extends` is the source that would win ahead of this
    // one: the first candidate, unless this is it (then the next one down).
    const std::string base =
        i == 0 ? (candidates.size() > 1 ? candidates[1].source : std::string())
               : candidates[0].source;
    candidates[i].source =
        expand_query_modelines(candidates[i].source, base, 0, &candidates[i].note);
  }

  std::string first_failure; // diagnostics for the highest-priority source
  for (const QuerySource &source : candidates)
  {
    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(lang,
                                  source.source.c_str(),
                                  static_cast<uint32_t>(source.source.size()),
                                  &error_offset,
                                  &error_type);
    if (query)
    {
      cache[language_id] = query;
      result.query = query;
      result.used_runtime = source.runtime;
      result.used_builtin = source.stored;
      result.diagnostic =
          source.runtime ? ("runtime query loaded: " + source.path) : "Lua query loaded";
      if (!source.note.empty())
      {
        result.diagnostic += source.note;
      }
      return result;
    }
    const std::string error = "query failed: error " + std::to_string((int)error_type)
                              + " at offset " + std::to_string(error_offset);
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
    {
      first_failure = message;
    }
  }
  result.diagnostic = first_failure;
  return result;
}

TSQuery *TreeSitterManager::get_highlight_query(const std::string &extension)
{
  const std::string language_id = language_for_extension(extension);
  const TSLanguage *lang = load_language(language_id);
  if (!lang)
  {
    query_diagnostics_[language_id] = "parser unavailable";
    return nullptr;
  }
  std::string stored_source;
  auto entry_it = languages_.find(language_id);
  if (entry_it != languages_.end())
  {
    stored_source = entry_it->second.highlight_query_source;
  }
  QueryLookup lookup = lookup_query(
      language_id, lang, "highlights.scm", stored_source, query_cache_);
  runtime_query_used_[language_id] = lookup.used_runtime;
  builtin_query_used_[language_id] = lookup.used_builtin;
  query_diagnostics_[language_id] = lookup.diagnostic;
  return lookup.query;
}

TSQuery *TreeSitterManager::get_highlight_query_for_language(const std::string &language_id)
{
  const TSLanguage *lang = load_language(language_id);
  if (!lang)
  {
    return nullptr;
  }
  std::string stored_source;
  auto entry_it = languages_.find(language_id);
  if (entry_it != languages_.end())
  {
    stored_source = entry_it->second.highlight_query_source;
  }
  return lookup_query(language_id, lang, "highlights.scm", stored_source, query_cache_).query;
}

TSQuery *TreeSitterManager::get_fold_query(const std::string &extension)
{
  const std::string language_id = language_for_extension(extension);
  QueryLookup lookup = lookup_query(
      language_id, load_language(language_id), "folds.scm", std::string(), fold_query_cache_);
  fold_query_diagnostics_[language_id] = lookup.diagnostic;
  return lookup.query;
}

TSQuery *TreeSitterManager::get_injection_query(const std::string &extension)
{
  const std::string language_id = language_for_extension(extension);
  QueryLookup lookup = lookup_query(language_id,
                                    load_language(language_id),
                                    "injections.scm",
                                    std::string(),
                                    injection_query_cache_);
  injection_query_diagnostics_[language_id] = lookup.diagnostic;
  return lookup.query;
}

TSQuery *TreeSitterManager::get_indent_query(const std::string &extension)
{
  const std::string language_id = language_for_extension(extension);
  QueryLookup lookup = lookup_query(
      language_id, load_language(language_id), "indents.scm", std::string(), indent_query_cache_);
  indent_query_diagnostics_[language_id] = lookup.diagnostic;
  return lookup.query;
}

TSQuery *TreeSitterManager::get_textobject_query(const std::string &extension)
{
  const std::string language_id = language_for_extension(extension);
  QueryLookup lookup = lookup_query(language_id,
                                    load_language(language_id),
                                    "textobjects.scm",
                                    std::string(),
                                    textobject_query_cache_);
  textobject_query_diagnostics_[language_id] = lookup.diagnostic;
  return lookup.query;
}

TSParser *TreeSitterManager::create_parser_for_language(const std::string &language_id) const
{
  const TSLanguage *language = load_language(language_id);
  if (!language)
  {
    return nullptr;
  }
  TSParser *parser = ts_parser_new();
  if (!parser || !ts_parser_set_language(parser, language))
  {
    if (parser)
    {
      ts_parser_delete(parser);
    }
    return nullptr;
  }
  return parser;
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
  {
    // The runtime/bundled file a Lua caller asked for by extension, with its
    // modeline resolved: `native.query(ext)` is the same query the highlighter
    // uses, so it has to see the same patterns. There is no base here -- this
    // resolves one file, not the candidate list -- and a bundled source already
    // carries the `;; extends` files folded into it (set_query_source).
    query_source = load_query_source(language_id).source;
    query_source = expand_query_modelines(query_source, std::string(), 0, nullptr);
  }
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
    // The C library hands a pattern's predicates back instead of applying them.
    // This path parses and queries in one step and keeps no source text, so the
    // predicates that compare node text report themselves satisfied and only
    // the tree-shape ones (#kind-eq?, #has-parent?, #has-ancestor?) decide.
    if (!jot_query::match_satisfies(query_it->second, match, jot_query::TextReader()))
      continue;
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
  std::string message =
      std::string("query compilation failed (") + kind + " at byte " + std::to_string(offset);
  if (offset < source.size())
  {
    std::string near =
        source.substr(offset, std::min<std::string::size_type>(48, source.size() - offset));
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
  // An empty bundled query is the registry's "no bundled query" placeholder,
  // not a query. It compiles, so caching it would report "Lua query loaded" and
  // capture nothing -- shadowing the query an installed parser ships with and
  // leaving the language active on a highlightless buffer.
  const bool no_bundled_query = std::all_of(
      source.begin(), source.end(), [](unsigned char c) { return std::isspace(c) != 0; });
  if (no_bundled_query)
  {
    entry_it->second.highlight_query_source.clear();
#ifdef JOT_TREESITTER
    auto cached = query_cache_.find(language_id);
    if (cached != query_cache_.end())
    {
      ts_query_delete(cached->second);
      query_cache_.erase(cached);
    }
    runtime_query_used_[language_id] = false;
    builtin_query_used_[language_id] = false;
    query_diagnostics_[language_id] = "no bundled query; runtime query or regex is used";
#endif
    return true;
  }

  // Store the Lua-provided source first. A parser may not be installed yet;
  // the query is compiled lazily (or validated now when possible).
  //
  // Runtime files that open with `;; extends` are folded in here rather than
  // joined with the source later: this stored string is what all three compile
  // paths read (the synchronous one below, the deferred boot worker, and the
  // highlighter's candidate list through load_query_source), and a file that
  // says it extends the language's query has to be part of all three or it is
  // silently dropped by whichever one runs.
  const std::string effective_source = source + extending_query_sources(language_id);
  entry_it->second.highlight_query_source = effective_source;

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
                                    effective_source});
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
  TSQuery *query = ts_query_new(language,
                                effective_source.c_str(),
                                static_cast<uint32_t>(effective_source.size()),
                                &offset,
                                &query_error);
  if (!query)
  {
    error = tree_sitter_query_compile_error(offset, query_error, effective_source);
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
  query_diagnostics_[language_id] =
      effective_source == source ? "Lua query loaded" : "Lua query loaded; ;; extends applied";
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
