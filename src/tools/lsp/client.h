#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include "text_features.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

struct LSPCompletionItem {
  std::string label;
  std::string insert_text;
  std::string detail;
  std::string documentation;
  std::string filter_text;
  std::string sort_text;
  std::vector<std::string> commit_characters;
  int kind = 0;
  int insert_text_format = 1; // 1=plain text, 2=snippet
  bool deprecated = false;
  bool preselect = false;
  bool has_text_edit_range = false;
  int edit_start_line = 0;
  int edit_start_char = 0;
  int edit_end_line = 0;
  int edit_end_char = 0;
};

struct LSPLocation {
  std::string filepath;
  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
};

struct LSPHoverResult {
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::string contents;
};

struct LSPDefinitionResult {
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::vector<LSPLocation> locations;
};

struct LSPSymbol {
  std::string name;
  std::string kind;
  std::string detail;
  std::string filepath;
  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
};

struct LSPDocumentSymbolResult {
  std::string filepath;
  std::vector<LSPSymbol> symbols;
};

class LSPClient {
private:
  struct PendingPositionRequest {
    std::string filepath;
    int line = 0;
    int character = 0;
  };

  std::string language;
  std::string root_path;
  std::vector<std::string> command;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
  int child_pid;
  bool running;
  bool initialized;
  int next_request_id;
  std::map<std::string, int> file_versions;
  std::string stdout_buffer;
  std::string stderr_buffer;
  std::string outbound_buffer;
  std::string last_error;
  std::vector<std::pair<std::string, std::vector<Diagnostic>>>
      pending_diagnostics;
  std::map<int, std::string> pending_completion_requests;
  std::map<int, PendingPositionRequest> pending_hover_requests;
  std::map<int, PendingPositionRequest> pending_definition_requests;
  std::map<int, std::string> pending_document_symbol_requests;
  std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>>
      pending_completions;
  std::vector<LSPHoverResult> pending_hovers;
  std::vector<LSPDefinitionResult> pending_definitions;
  std::vector<LSPDocumentSymbolResult> pending_document_symbols;

  bool send_message(const std::string &json);
  bool flush_pending_writes();
  std::string json_escape(const std::string &value) const;
  void append_log_line(const std::string &prefix, const std::string &line);
  void handle_stdout_data(const std::string &data);
  void handle_stderr_data(const std::string &data);

public:
  LSPClient(const std::string &language_name, const std::string &workspace_root,
            const std::vector<std::string> &argv);
  ~LSPClient();

  bool start();
  void stop();
  bool restart();
  bool poll();

  bool did_open(const std::string &filepath, const std::string &language_id,
                const std::string &text);
  bool did_change(const std::string &filepath, const std::string &text);
  bool did_save(const std::string &filepath, const std::string &text);
  bool request_completion(const std::string &filepath, int line, int character,
                          char trigger_character = '\0');
  bool request_hover(const std::string &filepath, int line, int character);
  bool request_definition(const std::string &filepath, int line,
                          int character);
  bool request_document_symbols(const std::string &filepath);
  std::vector<std::pair<std::string, std::vector<Diagnostic>>>
  consume_published_diagnostics();
  std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>>
  consume_completion_items();
  std::vector<LSPHoverResult> consume_hover_results();
  std::vector<LSPDefinitionResult> consume_definition_results();
  std::vector<LSPDocumentSymbolResult> consume_document_symbol_results();

  bool is_running() const { return running; }
  bool is_initialized() const { return initialized; }
  const std::string &get_language() const { return language; }
  const std::string &get_root_path() const { return root_path; }
  const std::string &get_last_error() const { return last_error; }
  std::string describe() const;
};

#endif
