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

int tree_sitter_capture_color_for_name(const std::string &name);
int tree_sitter_capture_priority_for_name(const std::string &name);

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
  std::string language_id_for_extension(const std::string &extension) const;

#ifdef JOT_TREESITTER
  TSParser *create_parser(const std::string &extension) const;
  TSQuery *get_highlight_query(const std::string &extension);
#endif

private:
  void register_languages();

  std::string load_query_source(const std::string &language_name) const;

  std::unordered_map<std::string, std::string> ext_to_lang_;
  std::unordered_map<std::string, TSLanguageEntry> languages_;
#ifdef JOT_TREESITTER
  mutable std::unordered_map<std::string, const TSLanguage *> parser_languages_;
  std::unordered_map<std::string, TSQuery *> query_cache_;
#endif
};

#endif
