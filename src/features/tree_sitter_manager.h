#ifndef TREE_SITTER_MANAGER_H
#define TREE_SITTER_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#else
typedef struct TSLanguage TSLanguage;
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSQuery TSQuery;
#endif

struct TSLanguageEntry {
  std::string language_id;
  std::string highlight_query_source;
};

class TreeSitterManager {
public:
  TreeSitterManager();
  ~TreeSitterManager();

  TreeSitterManager(const TreeSitterManager &) = delete;
  TreeSitterManager &operator=(const TreeSitterManager &) = delete;

  const TSLanguageEntry *get_language(const std::string &extension) const;

  bool has_language(const std::string &extension) const;

#ifdef JOT_TREESITTER
  TSParser *create_parser(const std::string &extension) const;
  TSQuery *get_highlight_query(const std::string &extension) const;
#endif

private:
  void register_languages();

  std::string load_query_source(const std::string &language_name) const;

  std::unordered_map<std::string, std::string> ext_to_lang_;
  std::unordered_map<std::string, TSLanguageEntry> languages_;
};

#endif
