#ifndef TREE_SITTER_MANAGER_H
#define TREE_SITTER_MANAGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
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

enum TreeSitterTokenKind
{
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
void tree_sitter_set_capture_color(const std::string &name, int color);

#ifdef JOT_TREESITTER
// Precise compile-failure wording shared by the synchronous and the
// background query-compile paths, so both report identical messages.
std::string tree_sitter_query_compile_error(uint32_t offset,
                                             TSQueryError error,
                                             const std::string &source);
#endif

struct TSLanguageEntry
{
  std::string language_id;
  std::string highlight_query_source;
  std::string minimal_query_source;
  std::string url;
  std::string symbol;
  std::string source_subdir;
  std::vector<std::string> library_names;
};

struct TreeSitterRuntimeStatus
{
  bool has_language = false;
  bool parser_loaded = false;
  bool query_loaded = false;
  bool used_runtime_query = false;
  bool used_builtin_query = false;
  std::string language_id;
  std::string parser_message;
  std::string query_message;
};

using TreeSitterHandle = std::uint64_t;

struct TreeSitterCapture
{
  std::string name;
  std::uint32_t start_byte = 0;
  std::uint32_t end_byte = 0;
  std::uint32_t start_row = 0;
  std::uint32_t start_column = 0;
  std::uint32_t end_row = 0;
  std::uint32_t end_column = 0;
};

class TreeSitterManager
{
public:
  TreeSitterManager();
  ~TreeSitterManager();

  TreeSitterManager(const TreeSitterManager &) = delete;
  TreeSitterManager &operator=(const TreeSitterManager &) = delete;

  const TSLanguageEntry *get_language(const std::string &extension) const;
  std::vector<std::string> language_names() const;

  bool has_language(const std::string &extension) const;
  bool has_language_override(const std::string &extension) const;
  std::string language_id_for_extension(const std::string &extension) const;
  TreeSitterRuntimeStatus runtime_status_for_extension(const std::string &extension) const;
  TreeSitterRuntimeStatus runtime_status_for_language(const std::string &language_id) const;
  void set_runtime_options(const std::vector<std::string> &library_paths,
                           const std::vector<std::string> &query_paths,
                           const std::vector<std::string> &language_overrides);
  void reload();

  // Deferred (background) query compilation. While deferred mode is on,
  // set_query_source records the source and queues the language instead of
  // compiling on the caller's thread; start_deferred_compile spawns a worker
  // that dlopens installed parsers and compiles the queued queries off the
  // main thread, and finish_deferred_compile (main thread only) installs the
  // finished queries into the caches, returning per-language failure messages
  // for the Lua policy to report. Skipped languages keep working through the
  // normal synchronous path.
  enum class DeferredCompileState
  {
    Idle,
    Compiling,
    Done
  };
  void set_deferred_compile_mode(bool on);
  bool deferred_compile_mode() const;
  DeferredCompileState deferred_compile_state() const;
  void start_deferred_compile();
  std::vector<std::string> finish_deferred_compile();

  // Lua-facing operations. Handles are opaque and valid only while this
  // manager is alive; all ownership remains native.
  bool register_language(const std::string &language_id,
                         const std::vector<std::string> &extensions,
                         const std::string &query_source = "",
                         const std::string &url = "",
                         const std::string &source_subdir = "",
                         const std::string &symbol = "",
                         const std::vector<std::string> &library_names = {},
                         const std::string &minimal_query = "");
  void disable_language(const std::string &language_id);
  std::string language_for_extension(const std::string &extension) const;
  TreeSitterRuntimeStatus status(const std::string &language_or_extension) const;
  TreeSitterHandle create_parser_handle(const std::string &extension);
  bool delete_parser_handle(TreeSitterHandle handle);
  TreeSitterHandle
  parse_handle(TreeSitterHandle parser, const std::string &text, TreeSitterHandle old_tree = 0);
  bool delete_tree_handle(TreeSitterHandle handle);
  TreeSitterHandle compile_query_handle(const std::string &extension,
                                        const std::string &source = "");
  bool delete_query_handle(TreeSitterHandle handle);
  std::vector<TreeSitterCapture> captures_for_handles(TreeSitterHandle query,
                                                      TreeSitterHandle tree,
                                                      std::uint32_t start_byte = 0,
                                                      std::uint32_t end_byte = UINT32_MAX) const;
  bool
  set_query_source(const std::string &extension, const std::string &source, std::string &error);
  void configure_runtime_paths();

#ifdef JOT_TREESITTER
  const TSLanguage *load_language(const std::string &language_id) const;
  TSParser *create_parser(const std::string &extension) const;
  TSQuery *get_highlight_query(const std::string &extension);

  // Reparse `text` reusing `tree`, which must describe `base`. One bounded
  // ts_tree_edit covering the diff between `base` and `text` is applied first,
  // so cost tracks the edited region instead of a whole-file parse. Consumes
  // `tree` on success and returns the new tree (caller owns it). Returns
  // `tree` unchanged when base == text (nothing to do) or when the incremental
  // parse fails (the caller should keep its edit-pending state and retry).
  // Passing a null `tree` performs a full parse from scratch.
  static TSTree *reparse_incremental(TSParser *parser,
                                     TSTree *tree,
                                     const std::string &base,
                                     const std::string &text);
#endif

private:
  struct QuerySource
  {
    std::string source;
    std::string path;
    bool runtime = false;
    bool stored = false;
  };

  QuerySource load_query_source(const std::string &language_name) const;

#ifdef JOT_TREESITTER
  struct DeferredQueryJob
  {
    std::string language_id;
    std::string symbol;
    std::vector<std::string> library_names;
    std::string source;
  };

  struct DeferredCompileResult
  {
    std::string language_id;
    std::string source;
    std::string error; // empty on success
    bool parser_absent = false; // no installed parser: stored, not a failure
    void *handle = nullptr;
    const TSLanguage *language = nullptr;
    TSQuery *query = nullptr;
  };

  void deferred_query_compile_worker();
#endif


  std::unordered_map<std::string, std::string> ext_to_lang_;
  std::unordered_set<std::string> language_override_extensions_;
  std::unordered_map<std::string, TSLanguageEntry> languages_;
  std::vector<std::string> runtime_library_paths_;
  std::vector<std::string> runtime_query_paths_;

  // Deferred-compile state (see the public API above). The queue is filled on
  // the main thread during the boot Lua load, snapshotted once by the worker,
  // and results are installed back on the main thread, so the mutex only
  // guards hand-off points. The mode and state flags exist in every build;
  // the queue and worker types are tree-sitter-only.
  bool deferred_compile_mode_ = false;
  DeferredCompileState deferred_compile_state_ = DeferredCompileState::Idle;
#ifdef JOT_TREESITTER
  std::mutex deferred_mutex_;
  std::vector<DeferredQueryJob> deferred_query_jobs_;
  std::vector<std::string> deferred_compile_roots_;
  std::vector<DeferredCompileResult> deferred_compile_results_;
  std::thread deferred_compile_thread_;
  std::atomic<bool> deferred_cancel_{false};
#endif
#ifdef JOT_TREESITTER
  mutable std::unordered_map<std::string, const TSLanguage *> parser_languages_;
  mutable std::unordered_map<std::string, void *> library_handles_;
  mutable std::unordered_map<std::string, std::string> parser_diagnostics_;
  mutable std::unordered_map<std::string, std::string> query_diagnostics_;
  mutable std::unordered_map<std::string, bool> runtime_query_used_;
  mutable std::unordered_map<std::string, bool> builtin_query_used_;
  std::unordered_map<std::string, TSQuery *> query_cache_;
  std::unordered_map<TreeSitterHandle, TSParser *> lua_parsers_;
  std::unordered_map<TreeSitterHandle, TSTree *> lua_trees_;
  std::unordered_map<TreeSitterHandle, TSQuery *> lua_queries_;
#endif
  TreeSitterHandle next_handle_ = 1;
};

#endif
