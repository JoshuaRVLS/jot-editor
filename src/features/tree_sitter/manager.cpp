#include "tree_sitter/manager.h"
#include "tree_sitter/install.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

namespace
{
  namespace fs = std::filesystem;

  std::string trim_copy(const std::string &s)
  {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
      return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
  }

  char path_list_separator()
  {
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
  }

  std::vector<std::string> split_list(const std::string &text,
                                      char delimiter = path_list_separator())
  {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delimiter))
    {
      item = trim_copy(item);
      if (!item.empty())
        out.push_back(item);
    }
    return out;
  }

  fs::path data_root()
  {
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local && *local)
      return fs::path(local) / "jot" / "treesitter";
    const char *app_data = getenv("APPDATA");
    if (app_data && *app_data)
      return fs::path(app_data) / "jot" / "treesitter";
    const char *home = getenv("USERPROFILE");
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
      return fs::path(xdg) / "jot" / "treesitter";
    const char *home = getenv("HOME");
#endif
    return home ? fs::path(home) / ".local" / "share" / "jot" / "treesitter" : fs::path();
  }

  fs::path home_path(const std::string &suffix)
  {
    fs::path root = data_root();
    return root.empty() ? fs::path() : root / suffix;
  }

  fs::path cache_root()
  {
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local && *local)
      return fs::path(local) / "jot" / "treesitter";
    const char *home = getenv("USERPROFILE");
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg)
      return fs::path(xdg) / "jot" / "treesitter";
    const char *home = getenv("HOME");
#endif
    return home ? fs::path(home) / ".cache" / "jot" / "treesitter" : fs::path();
  }

  fs::path cache_path(const std::string &suffix)
  {
    fs::path root = cache_root();
    return root.empty() ? fs::path() : root / suffix;
  }

#ifdef JOT_TREESITTER
  void close_library_handle(void *handle)
  {
    if (!handle)
      return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
  }
#endif
} // namespace

TreeSitterManager::TreeSitterManager()
{
  // Language policy is loaded by Lua before syntax is used.
  configure_runtime_paths();
}
TreeSitterManager::~TreeSitterManager()
{
#ifdef JOT_TREESITTER
  // Stop a background query compile still in flight and release anything it
  // produced but that was never installed into the caches.
  deferred_cancel_.store(true);
  if (deferred_compile_thread_.joinable())
  {
    deferred_compile_thread_.join();
  }
  {
    std::lock_guard<std::mutex> lock(deferred_mutex_);
    for (auto &result : deferred_compile_results_)
    {
      if (result.query)
        ts_query_delete(result.query);
      if (result.handle)
        close_library_handle(result.handle);
    }
    deferred_compile_results_.clear();
  }
  for (auto &entry : lua_parsers_)
    ts_parser_delete(entry.second);
  for (auto &entry : lua_trees_)
    ts_tree_delete(entry.second);
  for (auto &entry : lua_queries_)
    ts_query_delete(entry.second);
  for (auto &entry : query_cache_)
  {
    if (entry.second)
    {
      ts_query_delete(entry.second);
    }
  }
  for (auto &entry : library_handles_)
  {
    if (entry.second)
    {
      close_library_handle(entry.second);
    }
  }
#endif
}

bool TreeSitterManager::register_language(const std::string &language_id,
                                          const std::vector<std::string> &extensions,
                                          const std::string &query_source,
                                          const std::string &url,
                                          const std::string &source_subdir,
                                          const std::string &symbol,
                                          const std::vector<std::string> &library_names,
                                          const std::string &minimal_query)
{
  std::string language = language_id;
  std::transform(language.begin(),
                 language.end(),
                 language.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  for (char &c : language)
    if (c == '-' || c == ' ')
      c = '_';
  if (language.empty() || extensions.empty())
    return false;
  TSLanguageEntry entry;
  entry.language_id = language;
  entry.highlight_query_source = query_source;
  entry.minimal_query_source = minimal_query;
  entry.url = url;
  entry.source_subdir = source_subdir;
  entry.symbol = symbol;
  entry.library_names = library_names;
  TreeSitterInstall::register_language({language, url, source_subdir, library_names});
  languages_[language] = std::move(entry);
  for (auto extension : extensions)
  {
    if (extension.empty())
      continue;
    if (extension.front() != '.')
      extension.insert(extension.begin(), '.');
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    ext_to_lang_[extension] = language;
  }
  return true;
}

std::vector<std::string> TreeSitterManager::language_names() const
{
  std::vector<std::string> names;
  for (const auto &entry : languages_)
    names.push_back(entry.first);
  std::sort(names.begin(), names.end());
  return names;
}

void TreeSitterManager::disable_language(const std::string &language_id)
{
  languages_.erase(language_id);
  for (auto it = ext_to_lang_.begin(); it != ext_to_lang_.end();)
  {
    if (it->second == language_id)
      it = ext_to_lang_.erase(it);
    else
      ++it;
  }
}

std::string TreeSitterManager::language_for_extension(const std::string &extension) const
{
  std::string normalized = extension;
  std::transform(normalized.begin(),
                 normalized.end(),
                 normalized.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  for (char &c : normalized)
    if (c == '-' || c == ' ')
      c = '_';
  if (languages_.find(normalized) != languages_.end())
  {
    return normalized;
  }
  if (extension.empty())
    return "";
  return language_id_for_extension(extension.front() == '.' ? extension : "." + extension);
}

TreeSitterRuntimeStatus TreeSitterManager::status(const std::string &language_or_extension) const
{
  if (language_or_extension.find('.') != std::string::npos)
  {
    return runtime_status_for_extension(
        language_or_extension.front() == '.' ? language_or_extension : "." + language_or_extension);
  }
  return runtime_status_for_language(language_or_extension);
}

void TreeSitterManager::configure_runtime_paths()
{
  std::vector<std::string> default_library_paths;
  const char *env_paths = getenv("JOT_TREESITTER_PATH");
  if (env_paths && *env_paths)
  {
    default_library_paths = split_list(env_paths);
  }
  // JOT_TREESITTER_PREFIX is the documented explicit install root that
  // :tsinstall installs into; it must also be searched so installed parsers
  // are found. JOT_TREESITTER_PATH remains an additional search override.
  const char *prefix = getenv("JOT_TREESITTER_PREFIX");
  if (prefix && *prefix)
  {
    default_library_paths.insert(default_library_paths.begin(),
                                 (fs::path(prefix) / "parsers").string());
  }
  if (!home_path("parsers").empty())
  {
    default_library_paths.push_back(home_path("parsers").string());
  }
  if (!cache_path("parsers").empty())
  {
    default_library_paths.push_back(cache_path("parsers").string());
  }
  runtime_library_paths_ = default_library_paths;

  std::vector<std::string> default_query_paths;
  const char *env_query_paths = getenv("JOT_TREESITTER_QUERY_PATH");
  if (env_query_paths && *env_query_paths)
  {
    default_query_paths = split_list(env_query_paths);
  }
  if (prefix && *prefix)
  {
    default_query_paths.insert(default_query_paths.begin(),
                               (fs::path(prefix) / "queries").string());
  }
  if (!home_path("queries").empty())
  {
    default_query_paths.push_back(home_path("queries").string());
  }
  if (!cache_path("queries").empty())
  {
    default_query_paths.push_back(cache_path("queries").string());
  }
  runtime_query_paths_ = default_query_paths;
}

const TSLanguageEntry *TreeSitterManager::get_language(const std::string &extension) const
{
  std::string normalized = extension;
  if (!normalized.empty() && normalized.front() != '.')
    normalized.insert(normalized.begin(), '.');
  std::transform(normalized.begin(),
                 normalized.end(),
                 normalized.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  auto ext_it = ext_to_lang_.find(normalized);
  if (ext_it == ext_to_lang_.end())
    return nullptr;
  auto lang_it = languages_.find(ext_it->second);
  if (lang_it == languages_.end())
    return nullptr;
  return &lang_it->second;
}

bool TreeSitterManager::has_language(const std::string &extension) const
{
  return get_language(extension) != nullptr;
}

bool TreeSitterManager::has_language_override(const std::string &extension) const
{
  return language_override_extensions_.find(extension) != language_override_extensions_.end();
}

std::string TreeSitterManager::language_id_for_extension(const std::string &extension) const
{
  const TSLanguageEntry *entry = get_language(extension);
  return entry ? entry->language_id : "";
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_extension(const std::string &extension) const
{
  TreeSitterRuntimeStatus status;
  status.language_id = language_id_for_extension(extension);
  status.has_language = !status.language_id.empty();
  if (!status.has_language)
  {
    status.parser_message = "unsupported extension";
    return status;
  }
#ifdef JOT_TREESITTER
  auto parser_it = parser_languages_.find(status.language_id);
  status.parser_loaded = parser_it != parser_languages_.end() && parser_it->second != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded" : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded = query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query = runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query = builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded" : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

TreeSitterRuntimeStatus
TreeSitterManager::runtime_status_for_language(const std::string &language_id) const
{
  TreeSitterRuntimeStatus status;
  status.language_id = language_id;
  std::transform(status.language_id.begin(),
                 status.language_id.end(),
                 status.language_id.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  for (char &c : status.language_id)
    if (c == '-' || c == ' ')
      c = '_';
  status.has_language =
      !status.language_id.empty() && languages_.find(status.language_id) != languages_.end();
  if (!status.has_language)
  {
    status.parser_message = "unsupported language";
    return status;
  }
#ifdef JOT_TREESITTER
  status.parser_loaded = load_language(status.language_id) != nullptr;
  auto parser_diag_it = parser_diagnostics_.find(status.language_id);
  status.parser_message = parser_diag_it == parser_diagnostics_.end()
                              ? (status.parser_loaded ? "parser loaded" : "parser not attempted")
                              : parser_diag_it->second;

  auto query_it = query_cache_.find(status.language_id);
  status.query_loaded = query_it != query_cache_.end() && query_it->second != nullptr;
  auto runtime_it = runtime_query_used_.find(status.language_id);
  status.used_runtime_query = runtime_it != runtime_query_used_.end() && runtime_it->second;
  auto builtin_it = builtin_query_used_.find(status.language_id);
  status.used_builtin_query = builtin_it != builtin_query_used_.end() && builtin_it->second;
  auto query_diag_it = query_diagnostics_.find(status.language_id);
  status.query_message = query_diag_it == query_diagnostics_.end()
                             ? (status.query_loaded ? "query loaded" : "query not attempted")
                             : query_diag_it->second;
#else
  status.parser_message = "Tree-sitter runtime not available";
  status.query_message = "Tree-sitter runtime not available";
#endif
  return status;
}

void TreeSitterManager::set_runtime_options(const std::vector<std::string> &library_paths,
                                            const std::vector<std::string> &query_paths,
                                            const std::vector<std::string> &language_overrides)
{
  if (!library_paths.empty())
  {
    runtime_library_paths_.insert(
        runtime_library_paths_.begin(), library_paths.begin(), library_paths.end());
  }
  if (!query_paths.empty())
  {
    runtime_query_paths_.insert(
        runtime_query_paths_.begin(), query_paths.begin(), query_paths.end());
  }
  for (const auto &raw : language_overrides)
  {
    size_t sep = raw.find(':');
    if (sep == std::string::npos)
    {
      sep = raw.find('=');
    }
    if (sep == std::string::npos)
    {
      continue;
    }
    std::string ext = trim_copy(raw.substr(0, sep));
    std::string lang = trim_copy(raw.substr(sep + 1));
    std::transform(lang.begin(),
                   lang.end(),
                   lang.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    for (char &c : lang)
      if (c == '-' || c == ' ')
        c = '_';
    if (ext.empty() || lang.empty())
    {
      continue;
    }
    if (ext.front() != '.')
    {
      ext = "." + ext;
    }
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (languages_.find(lang) == languages_.end())
    {
      TSLanguageEntry entry;
      entry.language_id = lang;
      languages_[lang] = std::move(entry);
    }
    ext_to_lang_[ext] = lang;
    language_override_extensions_.insert(ext);
  }
}

void TreeSitterManager::reload()
{
  // Keep language registry across reloads to avoid transient empty state
  // if Lua re-registration fails. Stale entries are overwritten by
  // registry.register(native) which is authoritative.
#ifdef JOT_TREESITTER
  for (auto &entry : lua_parsers_)
    ts_parser_delete(entry.second);
  for (auto &entry : lua_trees_)
    ts_tree_delete(entry.second);
  for (auto &entry : lua_queries_)
    ts_query_delete(entry.second);
  lua_parsers_.clear();
  lua_trees_.clear();
  lua_queries_.clear();
  for (auto &entry : query_cache_)
  {
    if (entry.second)
    {
      ts_query_delete(entry.second);
    }
  }
  query_cache_.clear();
  parser_languages_.clear();
  for (auto &entry : library_handles_)
  {
    if (entry.second)
    {
      close_library_handle(entry.second);
    }
  }
  library_handles_.clear();
  parser_diagnostics_.clear();
  query_diagnostics_.clear();
  runtime_query_used_.clear();
  builtin_query_used_.clear();
#endif
}
