#include "tree_sitter/manager.h"

#include <filesystem>

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
