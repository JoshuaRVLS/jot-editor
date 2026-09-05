#include "tree_sitter/manager.h"

#include <algorithm>
#include <filesystem>
#include <thread>

#ifdef JOT_TREESITTER
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#ifdef JOT_TREESITTER
namespace
{
  namespace fs = std::filesystem;
  using TreeSitterLanguageFn = const TSLanguage *(*)();

  std::string symbol_for_language(const std::string &language_name)
  {
    return "tree_sitter_" + language_name;
  }

  struct LanguageLookupResult
  {
    const TSLanguage *language = nullptr;
    std::string message;
  };

  void *open_library(const fs::path &path, std::string &error)
  {
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path.string().c_str());
    if (!handle)
    {
      error = path.string() + ": LoadLibrary failed with " + std::to_string(GetLastError());
    }
    return reinterpret_cast<void *>(handle);
#else
    void *handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
      const char *dl_error = dlerror();
      error = path.string() + ": " + (dl_error ? dl_error : "dlopen failed");
    }
    return handle;
#endif
  }

  void close_library(void *handle)
  {
    if (!handle)
      return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
  }

  void *library_symbol(void *handle, const std::string &symbol)
  {
#ifdef _WIN32
    return reinterpret_cast<void *>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol.c_str()));
#else
    return reinterpret_cast<void *>(dlsym(handle, symbol.c_str()));
#endif
  }

  uint32_t language_abi_version(const TSLanguage *language)
  {
#if defined(TREE_SITTER_LANGUAGE_VERSION) && TREE_SITTER_LANGUAGE_VERSION >= 15
    return ts_language_abi_version(language);
#else
    return ts_language_version(language);
#endif
  }

  LanguageLookupResult language_from_handle(void *handle, const std::string &symbol)
  {
    LanguageLookupResult result;
    if (!handle)
    {
      result.message = "invalid library handle";
      return result;
    }
    auto fn = reinterpret_cast<TreeSitterLanguageFn>(library_symbol(handle, symbol));
    if (!fn)
    {
      result.message = "missing symbol " + symbol;
      return result;
    }
    const TSLanguage *lang = fn();
    if (!lang)
    {
      result.message = "symbol " + symbol + " returned null";
      return result;
    }
    const uint32_t abi = language_abi_version(lang);
    if (abi < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION || abi > TREE_SITTER_LANGUAGE_VERSION)
    {
      result.message = "ABI " + std::to_string(abi) + " incompatible with "
                       + std::to_string(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) + "-"
                       + std::to_string(TREE_SITTER_LANGUAGE_VERSION);
      return result;
    }
    result.language = lang;
    result.message = "parser loaded";
    return result;
  }
} // namespace

const TSLanguage *TreeSitterManager::load_language(const std::string &language_id) const
{
  const std::string lid = language_id;
  auto metadata = languages_.find(lid);
  if (metadata == languages_.end())
    return nullptr;
  const TSLanguage *lang = nullptr;
  auto cached = parser_languages_.find(lid);
  // Only trust a cached parser when it actually loaded. A null entry means an
  // earlier attempt failed (e.g. before :tsinstall finished downloading), and
  // the library may exist now - so fall through and re-search the paths.
  if (cached != parser_languages_.end() && cached->second != nullptr)
  {
    lang = cached->second;
  }
  else
  {
    const std::string symbol =
        metadata->second.symbol.empty() ? symbol_for_language(lid) : metadata->second.symbol;
    auto handle_it = library_handles_.find(lid);
    if (handle_it != library_handles_.end())
    {
      LanguageLookupResult lookup = language_from_handle(handle_it->second, symbol);
      lang = lookup.language;
      parser_diagnostics_[lid] = lookup.message;
    }
    std::vector<fs::path> candidates;
    for (const auto &name : metadata->second.library_names)
    {
      candidates.emplace_back(name);
    }
    for (const auto &root : runtime_library_paths_)
      for (const auto &name : metadata->second.library_names)
        candidates.emplace_back(fs::path(root) / name);
    std::string last_error;
    for (const auto &candidate : candidates)
    {
      if (lang)
      {
        break;
      }
      void *handle = open_library(candidate, last_error);
      if (!handle)
      {
        continue;
      }
      LanguageLookupResult lookup = language_from_handle(handle, symbol);
      if (!lookup.language)
      {
        last_error = candidate.string() + ": " + lookup.message;
        close_library(handle);
        continue;
      }
      lang = lookup.language;
      library_handles_[lid] = handle;
      break;
    }
    if (lang)
    {
      parser_languages_[lid] = lang;
      parser_diagnostics_[lid] = "parser loaded";
    }
    else
    {
      parser_languages_[lid] = nullptr;
      std::string roots;
      for (const auto &root : runtime_library_paths_)
      {
        if (!roots.empty())
          roots += ", ";
        roots += root;
      }
      parser_diagnostics_[lid] =
          last_error.empty() ? "no parser library candidates for " + lid
                                   + (roots.empty() ? "" : "; search roots: " + roots)
                             : "parser not loaded; tried " + std::to_string(candidates.size())
                                   + " candidate(s); last error: " + last_error;
    }
  }

  return lang;
}

TSParser *TreeSitterManager::create_parser(const std::string &extension) const
{
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end())
    return nullptr;

  const std::string &lid = ext_it->second;
  const TSLanguage *lang = load_language(lid);
  if (!lang)
    return nullptr;

  TSParser *parser = ts_parser_new();
  if (!parser || !ts_parser_set_language(parser, lang))
  {
    if (parser)
    {
      ts_parser_delete(parser);
    }
    parser_diagnostics_[lid] = "ts_parser_set_language failed";
    return nullptr;
  }
  return parser;
}

TSTree *TreeSitterManager::reparse_incremental(TSParser *parser,
                                               TSTree *tree,
                                               const std::string &base,
                                               const std::string &text)
{
  if (!parser)
    return tree;
  if (!tree)
    return ts_parser_parse_string(parser, nullptr, text.data(), (uint32_t)text.size());

  // Longest common prefix of base and text: bytes before this are untouched.
  const size_t min_size = std::min(base.size(), text.size());
  size_t prefix = 0;
  while (prefix < min_size && base[prefix] == text[prefix])
  {
    ++prefix;
  }
  if (prefix == base.size() && prefix == text.size())
  {
    return tree; // no change
  }

  // Longest common suffix: bytes at or beyond this (old/new) are the edit.
  size_t old_end = base.size();
  size_t new_end = text.size();
  while (old_end > prefix && new_end > prefix && base[old_end - 1] == text[new_end - 1])
  {
    --old_end;
    --new_end;
  }

  auto point_at = [](const std::string &s, size_t offset) -> TSPoint
  {
    uint32_t row = 0;
    size_t last_newline = 0;
    for (size_t i = 0; i < offset && i < s.size(); ++i)
    {
      if (s[i] == '\n')
      {
        ++row;
        last_newline = i + 1;
      }
    }
    return {row, static_cast<uint32_t>(offset - last_newline)};
  };

  TSInputEdit edit;
  edit.start_byte = static_cast<uint32_t>(prefix);
  edit.old_end_byte = static_cast<uint32_t>(old_end);
  edit.new_end_byte = static_cast<uint32_t>(new_end);
  edit.start_point = point_at(base, prefix);
  edit.old_end_point = point_at(base, old_end);
  edit.new_end_point = point_at(text, new_end);

  ts_tree_edit(tree, &edit);
  TSTree *new_tree =
      ts_parser_parse_string(parser, tree, text.data(), static_cast<uint32_t>(text.size()));
  if (new_tree)
  {
    ts_tree_delete(tree);
    return new_tree;
  }
  // Parse failure: keep the caller's tree (still matches base) so the pending
  // edit can be retried; the caller must not treat it as in sync.
  return tree;
}

TreeSitterHandle TreeSitterManager::create_parser_handle(const std::string &extension)
{
  TSParser *parser = create_parser(extension);
  if (!parser)
    return 0;
  const TreeSitterHandle handle = next_handle_++;
  lua_parsers_[handle] = parser;
  return handle;
}

bool TreeSitterManager::delete_parser_handle(TreeSitterHandle handle)
{
  auto it = lua_parsers_.find(handle);
  if (it == lua_parsers_.end())
    return false;
  ts_parser_delete(it->second);
  lua_parsers_.erase(it);
  return true;
}

TreeSitterHandle TreeSitterManager::parse_handle(TreeSitterHandle parser_handle,
                                                 const std::string &text,
                                                 TreeSitterHandle old_handle)
{
  auto parser = lua_parsers_.find(parser_handle);
  if (parser == lua_parsers_.end())
    return 0;
  TSTree *old_tree = nullptr;
  if (old_handle)
  {
    auto old = lua_trees_.find(old_handle);
    if (old == lua_trees_.end())
      return 0;
    old_tree = old->second;
  }
  TSTree *tree = ts_parser_parse_string(
      parser->second, old_tree, text.c_str(), static_cast<uint32_t>(text.size()));
  if (!tree)
    return 0;
  const TreeSitterHandle handle = next_handle_++;
  lua_trees_[handle] = tree;
  return handle;
}

bool TreeSitterManager::delete_tree_handle(TreeSitterHandle handle)
{
  auto it = lua_trees_.find(handle);
  if (it == lua_trees_.end())
    return false;
  ts_tree_delete(it->second);
  lua_trees_.erase(it);
  return true;
}

#endif

#ifndef JOT_TREESITTER
TreeSitterHandle TreeSitterManager::create_parser_handle(const std::string &)
{
  return 0;
}
bool TreeSitterManager::delete_parser_handle(TreeSitterHandle)
{
  return false;
}
TreeSitterHandle
TreeSitterManager::parse_handle(TreeSitterHandle, const std::string &, TreeSitterHandle)
{
  return 0;
}
bool TreeSitterManager::delete_tree_handle(TreeSitterHandle)
{
  return false;
}
#endif

void TreeSitterManager::set_deferred_compile_mode(bool on)
{
  deferred_compile_mode_ = on;
}

bool TreeSitterManager::deferred_compile_mode() const
{
  return deferred_compile_mode_;
}

const std::vector<std::string> &TreeSitterManager::runtime_library_paths() const
{
  return runtime_library_paths_;
}

TreeSitterManager::DeferredCompileState TreeSitterManager::deferred_compile_state() const
{
  return deferred_compile_state_;
}

#ifndef JOT_TREESITTER
void TreeSitterManager::start_deferred_compile()
{
  deferred_compile_state_ = DeferredCompileState::Done;
}

std::vector<std::string> TreeSitterManager::finish_deferred_compile()
{
  deferred_compile_state_ = DeferredCompileState::Idle;
  return {};
}

void TreeSitterManager::queue_async_parse(AsyncParseJob)
{
}

std::vector<TreeSitterManager::AsyncParseResult> TreeSitterManager::take_finished_parses()
{
  return {};
}
#else
void TreeSitterManager::start_deferred_compile()
{
  if (!deferred_compile_mode_)
    return;
  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    if (deferred_compile_state_ != DeferredCompileState::Idle)
      return;
    deferred_compile_roots_ = runtime_library_paths_;
    deferred_cancel_.store(false);
    if (deferred_query_jobs_.empty())
    {
      deferred_compile_state_ = DeferredCompileState::Done;
      return;
    }
    deferred_compile_state_ = DeferredCompileState::Compiling;
  }
  deferred_compile_thread_ = std::thread(&TreeSitterManager::deferred_query_compile_worker, this);
}

void TreeSitterManager::deferred_query_compile_worker()
{
  std::vector<DeferredQueryJob> jobs;
  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    jobs = std::move(deferred_query_jobs_);
  }
  const std::vector<std::string> roots = deferred_compile_roots_;

  std::vector<DeferredCompileResult> results;
  results.reserve(jobs.size());
  for (auto &job : jobs)
  {
    if (deferred_cancel_.load())
    {
      break;
    }
    DeferredCompileResult result;
    result.language_id = job.language_id;
    result.source = job.source;

    // Mirror load_language's candidate search, but purely from the job
    // snapshot: the worker never touches the shared manager maps.
    std::vector<fs::path> candidates;
    for (const auto &name : job.library_names)
    {
      candidates.emplace_back(name);
    }
    for (const auto &root : roots)
      for (const auto &name : job.library_names)
        candidates.emplace_back(fs::path(root) / name);

    std::string last_error;
    for (const auto &candidate : candidates)
    {
      void *handle = open_library(candidate, last_error);
      if (!handle)
      {
        continue;
      }
      LanguageLookupResult lookup = language_from_handle(handle, job.symbol);
      if (!lookup.language)
      {
        last_error = candidate.string() + ": " + lookup.message;
        close_library(handle);
        continue;
      }
      result.handle = handle;
      result.language = lookup.language;
      break;
    }
    if (!result.language)
    {
      // No installed parser: the source is stored and stays available for a
      // later :tsinstall - exactly like the synchronous path, this is not a
      // failure to report.
      result.parser_absent = true;
      results.push_back(std::move(result));
      continue;
    }

    uint32_t offset = 0;
    TSQueryError query_error = TSQueryErrorNone;
    TSQuery *query = ts_query_new(
        result.language, result.source.c_str(),
        static_cast<uint32_t>(result.source.size()), &offset, &query_error);
    if (!query)
    {
      result.error = tree_sitter_query_compile_error(offset, query_error, result.source);
      close_library(result.handle);
      result.handle = nullptr;
      results.push_back(std::move(result));
      continue;
    }
    result.query = query;
    results.push_back(std::move(result));
  }

  std::lock_guard<std::mutex> lock(deferred_mutex_);
  deferred_compile_results_ = std::move(results);
  deferred_compile_state_ = DeferredCompileState::Done;
}

std::vector<std::string> TreeSitterManager::finish_deferred_compile()
{
  if (deferred_compile_thread_.joinable())
  {
    deferred_compile_thread_.join();
  }
  std::vector<DeferredCompileResult> results;
  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    results = std::move(deferred_compile_results_);
    deferred_compile_state_ = DeferredCompileState::Idle;
  }

  std::vector<std::string> failures;
  for (auto &result : results)
  {
    // The source may have been replaced since the job was queued (e.g. an
    // explicit :tsreload during the compile); a newer source was then
    // compiled synchronously, so discard the stale worker result.
    auto entry_it = languages_.find(result.language_id);
    if (entry_it == languages_.end() || entry_it->second.highlight_query_source != result.source)
    {
      if (result.query)
        ts_query_delete(result.query);
      if (result.handle)
        close_library(result.handle);
      continue;
    }
    if (result.parser_absent)
    {
      query_diagnostics_[result.language_id] = "Lua query stored (parser not installed yet)";
      runtime_query_used_[result.language_id] = false;
      builtin_query_used_[result.language_id] = false;
      continue;
    }
    if (!result.error.empty())
    {
      failures.push_back(result.language_id + ": " + result.error);
      query_diagnostics_[result.language_id] = result.error;
      runtime_query_used_[result.language_id] = false;
      builtin_query_used_[result.language_id] = false;
      continue;
    }
    auto cached = query_cache_.find(result.language_id);
    if (cached != query_cache_.end() && cached->second)
      ts_query_delete(cached->second);
    library_handles_[result.language_id] = result.handle;
    parser_languages_[result.language_id] = result.language;
    parser_diagnostics_[result.language_id] = "parser loaded";
    query_cache_[result.language_id] = result.query;
    runtime_query_used_[result.language_id] = false;
    builtin_query_used_[result.language_id] = true;
    query_diagnostics_[result.language_id] = "Lua query loaded";
  }
  return failures;
}

void TreeSitterManager::queue_async_parse(AsyncParseJob job)
{
  // One parse job per buffer is enough; a second queue for the same buffer
  // would race the first for install rights (the buffer flags the first as
  // pending, so the second would be discarded anyway).
  std::thread(&TreeSitterManager::async_parse_worker, this, std::move(job)).detach();
}

void TreeSitterManager::async_parse_worker(AsyncParseJob job)
{
  AsyncParseResult result;
  result.buffer_index = job.buffer_index;
  result.language_id = job.language_id;
  result.parsed_text = job.text;

  // Mirror load_language's candidate search from the job snapshot, exactly
  // like deferred_query_compile_worker: the worker never touches the shared
  // manager maps, so no lock is needed around the dlopen.
  std::vector<fs::path> candidates;
  for (const auto &name : job.library_names)
  {
    candidates.emplace_back(name);
  }
  for (const auto &root : job.library_paths)
  {
    for (const auto &name : job.library_names)
    {
      candidates.emplace_back(fs::path(root) / name);
    }
  }

  std::string last_error;
  void *handle = nullptr;
  const TSLanguage *lang = nullptr;
  for (const auto &candidate : candidates)
  {
    void *h = open_library(candidate, last_error);
    if (!h)
    {
      continue;
    }
    LanguageLookupResult lookup = language_from_handle(h, job.symbol);
    if (!lookup.language)
    {
      last_error = candidate.string() + ": " + lookup.message;
      close_library(h);
      continue;
    }
    handle = h;
    lang = lookup.language;
    break;
  }

  if (lang)
  {
    TSParser *parser = ts_parser_new();
    if (parser && ts_parser_set_language(parser, lang))
    {
      result.parser = parser;
      result.tree = ts_parser_parse_string(
          parser, nullptr, job.text.data(), static_cast<uint32_t>(job.text.size()));
      if (!result.tree)
      {
        ts_parser_delete(parser);
        result.parser = nullptr;
      }
    }
    else if (parser)
    {
      ts_parser_delete(parser);
    }
  }
  if (handle)
  {
    close_library(handle);
  }

  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    async_parse_results_.push_back(std::move(result));
  }
}

std::vector<TreeSitterManager::AsyncParseResult> TreeSitterManager::take_finished_parses()
{
  std::lock_guard<std::mutex> lock(deferred_mutex_);
  std::vector<AsyncParseResult> out;
  out.swap(async_parse_results_);
  return out;
}
#endif
