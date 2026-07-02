#include "tree_sitter/manager.h"
#include "tree_sitter/catalog.h"

#include <algorithm>
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
namespace {
namespace fs = std::filesystem;
using TreeSitterLanguageFn = const TSLanguage *(*)();

std::vector<fs::path> library_candidates(
    const std::string &language_name,
    const std::vector<std::string> &runtime_paths) {
  std::vector<std::string> library_names;
  if (const auto *entry = TreeSitterCatalog::find_language(language_name)) {
    library_names = entry->library_names;
  } else {
    library_names =
        TreeSitterCatalog::entry_for_github_url("https://github.com/x/tree-sitter-" +
                                                language_name)
            .library_names;
  }

  std::vector<fs::path> candidates;
  for (const auto &name : library_names) {
    candidates.emplace_back(name);
  }
  for (const auto &root : runtime_paths) {
    for (const auto &name : library_names) {
      candidates.emplace_back(fs::path(root) / name);
    }
  }
  return candidates;
}

std::string symbol_for_language(const std::string &language_name) {
  if (const auto *entry = TreeSitterCatalog::find_language(language_name)) {
    return entry->symbol;
  }
  std::string symbol = "tree_sitter_" + language_name;
  std::replace(symbol.begin(), symbol.end(), '-', '_');
  return symbol;
}

struct LanguageLookupResult {
  const TSLanguage *language = nullptr;
  std::string message;
};

void *open_library(const fs::path &path, std::string &error) {
#ifdef _WIN32
  HMODULE handle = LoadLibraryA(path.string().c_str());
  if (!handle) {
    error = path.string() + ": LoadLibrary failed with " +
            std::to_string(GetLastError());
  }
  return reinterpret_cast<void *>(handle);
#else
  void *handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char *dl_error = dlerror();
    error = path.string() + ": " + (dl_error ? dl_error : "dlopen failed");
  }
  return handle;
#endif
}

void close_library(void *handle) {
  if (!handle) return;
#ifdef _WIN32
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

void *library_symbol(void *handle, const std::string &symbol) {
#ifdef _WIN32
  return reinterpret_cast<void *>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol.c_str()));
#else
  return reinterpret_cast<void *>(dlsym(handle, symbol.c_str()));
#endif
}

uint32_t language_abi_version(const TSLanguage *language) {
#if defined(TREE_SITTER_LANGUAGE_VERSION) && TREE_SITTER_LANGUAGE_VERSION >= 15
  return ts_language_abi_version(language);
#else
  return ts_language_version(language);
#endif
}

LanguageLookupResult language_from_handle(void *handle, const std::string &symbol) {
  LanguageLookupResult result;
  if (!handle) {
    result.message = "invalid library handle";
    return result;
  }
  auto fn = reinterpret_cast<TreeSitterLanguageFn>(library_symbol(handle, symbol));
  if (!fn) {
    result.message = "missing symbol " + symbol;
    return result;
  }
  const TSLanguage *lang = fn();
  if (!lang) {
    result.message = "symbol " + symbol + " returned null";
    return result;
  }
  const uint32_t abi = language_abi_version(lang);
  if (abi < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION ||
      abi > TREE_SITTER_LANGUAGE_VERSION) {
    result.message = "ABI " + std::to_string(abi) + " incompatible with " +
                     std::to_string(TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) +
                     "-" + std::to_string(TREE_SITTER_LANGUAGE_VERSION);
    return result;
  }
  result.language = lang;
  result.message = "parser loaded";
  return result;
}
} // namespace

const TSLanguage *TreeSitterManager::load_language(
    const std::string &language_id) const {
  const std::string lid = TreeSitterCatalog::normalize_language_name(language_id);
  const TSLanguage *lang = nullptr;
  auto cached = parser_languages_.find(lid);
  if (cached != parser_languages_.end()) {
    lang = cached->second;
  } else {
    const std::string symbol = symbol_for_language(lid);
    auto handle_it = library_handles_.find(lid);
    if (handle_it != library_handles_.end()) {
      LanguageLookupResult lookup =
          language_from_handle(handle_it->second, symbol);
      lang = lookup.language;
      parser_diagnostics_[lid] = lookup.message;
    }
    std::vector<fs::path> candidates =
        library_candidates(lid, runtime_library_paths_);
    std::string last_error;
    for (const auto &candidate : candidates) {
      if (lang) {
        break;
      }
      void *handle = open_library(candidate, last_error);
      if (!handle) {
        continue;
      }
      LanguageLookupResult lookup = language_from_handle(handle, symbol);
      if (!lookup.language) {
        last_error = candidate.string() + ": " + lookup.message;
        close_library(handle);
        continue;
      }
      lang = lookup.language;
      library_handles_[lid] = handle;
      break;
    }
    if (lang) {
      parser_languages_[lid] = lang;
      parser_diagnostics_[lid] = "parser loaded";
    } else {
      parser_languages_[lid] = nullptr;
      parser_diagnostics_[lid] =
          last_error.empty()
              ? "no parser library candidates for " + lid
              : "parser not loaded; tried " +
                    std::to_string(candidates.size()) +
                    " candidate(s); last error: " + last_error;
    }
  }

  return lang;
}

TSParser *TreeSitterManager::create_parser(const std::string &extension) const {
  auto ext_it = ext_to_lang_.find(extension);
  if (ext_it == ext_to_lang_.end()) return nullptr;

  const std::string &lid = ext_it->second;
  const TSLanguage *lang = load_language(lid);
  if (!lang) return nullptr;

  TSParser *parser = ts_parser_new();
  if (!parser || !ts_parser_set_language(parser, lang)) {
    if (parser) {
      ts_parser_delete(parser);
    }
    parser_diagnostics_[lid] = "ts_parser_set_language failed";
    return nullptr;
  }
  return parser;
}

#endif
