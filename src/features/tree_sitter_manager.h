#ifndef TREE_SITTER_MANAGER_H
#define TREE_SITTER_MANAGER_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#else
typedef struct TSLanguage TSLanguage;
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSQuery TSQuery;
#endif

enum TreeSitterTokenKind {
  TS_TOKEN_NONE = 0,
  TS_TOKEN_KEYWORD = 1,
  TS_TOKEN_STRING = 2,
  TS_TOKEN_COMMENT = 3,
  TS_TOKEN_NUMBER = 4,
  TS_TOKEN_TYPE = 5,
  TS_TOKEN_FUNCTION = 6,
  TS_TOKEN_VARIABLE = 7,
  TS_TOKEN_CONSTANT = 8,
  TS_TOKEN_BUILTIN = 9,
  TS_TOKEN_OPERATOR = 10,
  TS_TOKEN_PUNCTUATION = 11,
  TS_TOKEN_TAG = 12,
  TS_TOKEN_ATTRIBUTE = 13,
  TS_TOKEN_NAMESPACE = 14,
  TS_TOKEN_MODULE = 15,
  TS_TOKEN_PARAMETER = 16,
  TS_TOKEN_FIELD = 17,
  TS_TOKEN_KEYWORD_CONTROL = 18,
  TS_TOKEN_KEYWORD_STORAGE = 19,
  TS_TOKEN_KEYWORD_PREPROC = 20,
  TS_TOKEN_FUNCTION_METHOD = 21,
  TS_TOKEN_FUNCTION_CONSTRUCTOR = 22,
  TS_TOKEN_TYPE_BUILTIN = 23,
  TS_TOKEN_CONSTANT_MACRO = 24,
  TS_TOKEN_STRING_ESCAPE = 25,
  TS_TOKEN_PUNCTUATION_BRACKET = 26,
  TS_TOKEN_PUNCTUATION_DELIMITER = 27,
};

int tree_sitter_capture_color_for_name(const std::string &name);
int tree_sitter_capture_token_for_name(const std::string &name);
int tree_sitter_capture_priority_for_name(const std::string &name);

struct TSLanguageEntry {
  std::string language_id;
  std::string highlight_query_source;
};

struct TreeSitterRuntimeStatus {
  bool has_language = false;
  bool parser_loaded = false;
  bool query_loaded = false;
  bool used_runtime_query = false;
  bool used_builtin_query = false;
  std::string language_id;
  std::string parser_message;
  std::string query_message;
};

class TreeSitterManager {
public:
  TreeSitterManager();
  ~TreeSitterManager();

  TreeSitterManager(const TreeSitterManager &) = delete;
  TreeSitterManager &operator=(const TreeSitterManager &) = delete;

  const TSLanguageEntry *get_language(const std::string &extension) const;

  bool has_language(const std::string &extension) const;
  bool has_language_override(const std::string &extension) const;
  std::string language_id_for_extension(const std::string &extension) const;
  TreeSitterRuntimeStatus runtime_status_for_extension(
      const std::string &extension) const;
  TreeSitterRuntimeStatus runtime_status_for_language(
      const std::string &language_id) const;
  void set_runtime_options(const std::vector<std::string> &library_paths,
                           const std::vector<std::string> &query_paths,
                           const std::vector<std::string> &language_overrides);
  void reload();

#ifdef JOT_TREESITTER
  const TSLanguage *load_language(const std::string &language_id) const;
  TSParser *create_parser(const std::string &extension) const;
  TSQuery *get_highlight_query(const std::string &extension);
#endif

private:
  struct QuerySource {
    std::string source;
    std::string path;
    bool runtime = false;
  };

  void register_languages();

  QuerySource load_query_source(const std::string &language_name) const;

  std::unordered_map<std::string, std::string> ext_to_lang_;
  std::unordered_set<std::string> language_override_extensions_;
  std::unordered_map<std::string, TSLanguageEntry> languages_;
  std::vector<std::string> runtime_library_paths_;
  std::vector<std::string> runtime_query_paths_;
#ifdef JOT_TREESITTER
  mutable std::unordered_map<std::string, const TSLanguage *> parser_languages_;
  mutable std::unordered_map<std::string, void *> library_handles_;
  mutable std::unordered_map<std::string, std::string> parser_diagnostics_;
  mutable std::unordered_map<std::string, std::string> query_diagnostics_;
  mutable std::unordered_map<std::string, bool> runtime_query_used_;
  mutable std::unordered_map<std::string, bool> builtin_query_used_;
  std::unordered_map<std::string, TSQuery *> query_cache_;
#endif
};

#endif
