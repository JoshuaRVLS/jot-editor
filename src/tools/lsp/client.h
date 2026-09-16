#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include "text_features.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

struct LSPCompletionItem
{
  std::string label;
  std::string insert_text;
  std::string detail;
  std::string documentation;
  // The spec's optional labelDetails, which a server only sends when the client
  // advertises labelDetailsSupport. Servers split the useful information between
  // these fields and `detail` differently -- clangd puts the parameter list in
  // labelDetails.detail and the include/namespace in .description, tsserver the
  // signature and the extra info -- so the completion popup reads both.
  std::string label_detail;
  std::string label_description;
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

struct LSPLocation
{
  std::string filepath;
  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
};

struct LSPHoverResult
{
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::string contents;
};

struct LSPSignatureParameter
{
  std::string label;
  // When the server addressed the parameter as [start, end) byte offsets into
  // the signature label these are set (>= 0); otherwise -1 and callers can
  // fall back to locating `label` inside the signature text.
  int label_start = -1;
  int label_end = -1;
  std::string documentation;
};

struct LSPSignature
{
  std::string label;
  std::string documentation;
  std::vector<LSPSignatureParameter> parameters;
  int active_parameter = -1;
};

struct LSPSignatureHelpResult
{
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::vector<LSPSignature> signatures;
  int active_signature = 0;
};

// One textDocument/inlayHint item. character is a byte offset into the line
// (converted from the server's UTF-16 position); the label is the virtual
// text rendered before that position.
struct LSPInlayHint
{
  int line = 0;
  int character = 0;
  std::string label;
  int kind = 0; // 1 = Type, 2 = Parameter
  bool padding_left = false;
  bool padding_right = false;
};

struct LSPInlayHintResult
{
  std::string origin_filepath;
  // The range the request covered (used to know when a scroll leaves it).
  int start_line = 0;
  int start_character = 0;
  int end_line = 0;
  int end_character = 0;
  int version = 0;
  std::vector<LSPInlayHint> hints;
};

struct LSPDefinitionResult
{
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::vector<LSPLocation> locations;
};

struct LSPSymbol
{
  std::string name;
  std::string kind;
  std::string detail;
  std::string filepath;
  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
};

struct LSPDocumentSymbolResult
{
  std::string filepath;
  std::vector<LSPSymbol> symbols;
};

// One thing a server is working on (`$/progress`). `title` is set by the begin
// message, `message`/`percentage` keep moving while it runs.
struct LSPProgress
{
  std::string title;
  std::string message;
  int percentage = -1;
};

// Applies one `$/progress` body to a token table: "begin" starts a token,
// "report" moves its message/percentage, anything else ("end") clears it. Free
// function so the state machine is testable without a running server.
void apply_lsp_progress(std::map<std::string, LSPProgress> &tokens,
                        const std::string &token,
                        const std::string &kind,
                        const std::string &title,
                        const std::string &message,
                        int percentage);

struct LSPTextEdit
{
  int start_line = 0;
  int start_char = 0;
  int end_line = 0;
  int end_char = 0;
  std::string new_text;
};

// One textDocument/codeAction item. `edits` holds the WorkspaceEdit expanded
// into per-file edit lists (UTF-16 characters converted to editor columns);
// empty when the action has no edit (command-only actions are dropped).
struct LSPCodeAction
{
  std::string title;
  std::string kind;
  std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> edits;
};

struct LSPCodeActionResult
{
  std::string origin_filepath;
  int origin_line = 0;
  int origin_character = 0;
  std::vector<LSPCodeAction> actions;
};

class LSPClient
{
private:
  struct PendingPositionRequest
  {
    std::string filepath;
    int line = 0;
    int character = 0;
    int version = 0;
  };

  struct PendingDocumentRequest
  {
    std::string filepath;
    int version = 0;
  };

  struct PendingInlayRequest
  {
    std::string filepath;
    int start_line = 0;
    int start_character = 0;
    int end_line = 0;
    int end_character = 0;
    int version = 0;
  };

  std::string language;
  std::string root_path;
  std::vector<std::string> command;
  // Extra Lua workspace.library dirs (e.g. the bundled jot API stub) sent to
  // the server via workspace/didChangeConfiguration after initialize.
  std::vector<std::string> library_dirs;
  // Server-specific settings injected as the initialize request's
  // initializationOptions (raw JSON object body, no braces). clangd keeps
  // deduced-type inlay hints off by default, so the editor sends them here.
  std::string initialization_options;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
  int child_pid;
#ifdef _WIN32
  void *child_process_handle = nullptr;
#endif
  bool running;
  bool initialized;
  bool uses_utf8_positions;
  bool shutdown_complete;
  int next_request_id;
  int initialize_request_id;
  int shutdown_request_id;
  std::map<std::string, int> file_versions;
  std::map<std::string, std::string> document_texts;
  std::string stdout_buffer;
  std::string stderr_buffer;
  std::string outbound_buffer;
  std::vector<std::string> deferred_messages;
  std::string last_error;
  std::vector<std::pair<std::string, std::vector<Diagnostic>>> pending_diagnostics;
  // Active progress by token, plus the messages the server asked us to show.
  std::map<std::string, LSPProgress> progress_;
  std::vector<std::string> pending_show_messages;
  // Mirrors of the last results each consume_* call handed to the editor, so
  // other consumers (the Lua API) can read per-server answers without racing
  // or stealing results from the native UI flow.
  std::vector<std::pair<std::string, std::vector<Diagnostic>>> last_diagnostics_;
  LSPHoverResult last_hover_;
  std::vector<LSPDefinitionResult> last_definitions_;
  std::vector<LSPDocumentSymbolResult> last_symbols_;
  std::map<int, PendingDocumentRequest> pending_completion_requests;
  std::map<int, PendingPositionRequest> pending_hover_requests;
  std::map<int, PendingPositionRequest> pending_signature_requests;
  std::map<int, PendingPositionRequest> pending_definition_requests;
  std::map<int, PendingPositionRequest> pending_reference_requests;
  std::map<int, PendingPositionRequest> pending_code_action_requests;
  std::map<int, PendingDocumentRequest> pending_document_symbol_requests;
  std::map<int, PendingDocumentRequest> pending_format_requests;
  std::map<int, PendingPositionRequest> pending_rename_requests;
  std::map<int, PendingInlayRequest> pending_inlay_hint_requests;
  std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>> pending_completions;
  std::vector<LSPHoverResult> pending_hovers;
  std::vector<LSPSignatureHelpResult> pending_signatures;
  std::vector<LSPInlayHintResult> pending_inlay_hints;
  std::vector<LSPDefinitionResult> pending_definitions;
  std::vector<LSPDefinitionResult> pending_references;
  std::vector<LSPCodeActionResult> pending_code_actions;
  std::vector<LSPDocumentSymbolResult> pending_document_symbols;
  std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> pending_formats;
  std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> pending_renames;

  bool send_message(const std::string &json, bool allow_during_initialization = false);
  bool flush_pending_writes();
  std::string json_escape(const std::string &value) const;
  // Lua server settings handed to the client (workspace library, globals,
  // completion), as the inner object under settings.Lua (no wrapper).
  std::string lua_settings_json() const;
  void append_log_line(const std::string &prefix, const std::string &line);
  void handle_stdout_data(const std::string &data);
  void handle_stderr_data(const std::string &data);
  void close_transport();
  std::string document_line(const std::string &filepath, int line) const;
  int lsp_character(const std::string &filepath, int line, int byte_character) const;
  int editor_character(const std::string &filepath, int line, int character) const;

public:
  LSPClient(const std::string &language_name,
            const std::string &workspace_root,
            const std::vector<std::string> &argv,
            const std::vector<std::string> &library_dirs = {},
            const std::string &initialization_options = {});
  ~LSPClient();

  // The completion-related client capabilities, shared by the initialize request
  // and the test that asserts them (labelDetails is opt-in per the spec).
  static std::string completion_client_capabilities();
  // The window block of the initialize capabilities; split out so the test
  // can assert it the way the completion block is asserted.
  static std::string window_client_capabilities();
  // The initialize params as a JSON object; split out so a test can parse the
  // whole thing rather than its pieces.
  std::string initialize_params_json() const;
  // The same builder, reachable from tests without starting a server (the
  // constructor takes a command, but nothing is spawned until start()).
  static std::string initialize_params_json_for_test()
  {
    LSPClient probe("cpp", "/tmp/jot-lsp-params", {}, {}, "");
    return probe.initialize_params_json();
  }

  bool start();
  void stop();
  bool restart();
  bool poll();

  bool
  did_open(const std::string &filepath, const std::string &language_id, const std::string &text);
  bool did_change(const std::string &filepath, const std::string &text);
  bool did_save(const std::string &filepath, const std::string &text);
  bool did_close(const std::string &filepath);
  bool request_completion(const std::string &filepath,
                          int line,
                          int character,
                          char trigger_character = '\0');
  bool request_hover(const std::string &filepath, int line, int character);
  bool request_signature_help(const std::string &filepath,
                              int line,
                              int character,
                              char trigger_character = '\0');
  bool request_definition(const std::string &filepath, int line, int character);
  bool request_references(const std::string &filepath, int line, int character);
  bool request_document_symbols(const std::string &filepath);
  // Asks the server for code actions (quick fixes, refactors) at the given
  // position, passing the cursor-line diagnostics so servers can offer
  // fixes. Results arrive via consume_code_action_results().
  bool request_code_actions(const std::string &filepath,
                            int line,
                            int character,
                            const std::vector<Diagnostic> &diagnostics);
  std::vector<LSPCodeActionResult> consume_code_action_results();
  // Asks the server to rename the symbol at the given position. The returned
  // WorkspaceEdit is expanded into per-file edit lists (UTF-16 characters
  // converted to editor columns) and handed back through
  // consume_rename_results().
  bool request_rename(const std::string &filepath,
                      int line,
                      int character,
                      const std::string &new_name);
  std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> consume_rename_results();
  // Asks the server to format the whole document. Character offsets in the
  // returned edits are converted to editor (UTF-8 byte) columns before they
  // are handed back through consume_format_results().
  bool request_format(const std::string &filepath, int tab_size);
  std::vector<std::pair<std::string, std::vector<Diagnostic>>> consume_published_diagnostics();
  std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>> consume_completion_items();
  std::vector<LSPHoverResult> consume_hover_results();
  std::vector<LSPSignatureHelpResult> consume_signature_results();
  // Requests parameter/type inlay hints covering the given line range (byte
  // offsets). Results arrive via consume_inlay_hint_results().
  bool request_inlay_hints(const std::string &filepath,
                           int start_line,
                           int start_character,
                           int end_line,
                           int end_character);
  std::vector<LSPInlayHintResult> consume_inlay_hint_results();
  std::vector<LSPDefinitionResult> consume_definition_results();
  std::vector<LSPDefinitionResult> consume_reference_results();
  std::vector<LSPDocumentSymbolResult> consume_document_symbol_results();
  std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> consume_format_results();
  // True when this client has a document (didOpen without didClose). Used to
  // route close/change notifications to every server attached to a file.
  bool has_open_document(const std::string &filepath) const;

  bool is_running() const
  {
    return running;
  }
  bool is_initialized() const
  {
    return initialized;
  }

  const std::vector<std::pair<std::string, std::vector<Diagnostic>>> &last_diagnostics() const
  {
    return last_diagnostics_;
  }
  // What this server is working on right now, oldest token first.
  std::vector<LSPProgress> active_progress() const
  {
    std::vector<LSPProgress> out;
    out.reserve(progress_.size());
    for (const auto &entry : progress_)
    {
      out.push_back(entry.second);
    }
    return out;
  }
  // window/showMessage texts, drained once. A server talking to the user is an
  // event, unlike progress, so it does not belong in the polling accessor above.
  std::vector<std::string> consume_show_messages()
  {
    auto out = std::move(pending_show_messages);
    pending_show_messages.clear();
    return out;
  }
  const LSPHoverResult &last_hover() const
  {
    return last_hover_;
  }
  const std::vector<LSPDefinitionResult> &last_definitions() const
  {
    return last_definitions_;
  }
  const std::vector<LSPDocumentSymbolResult> &last_document_symbols() const
  {
    return last_symbols_;
  }
  int get_stdin_fd() const
  {
    return stdin_fd;
  }
  int get_stdout_fd() const
  {
    return stdout_fd;
  }
  int get_stderr_fd() const
  {
    return stderr_fd;
  }
  const std::string &get_language() const
  {
    return language;
  }
  // Identifies the server binary (e.g. "clangd", "rust-analyzer") from the
  // command's basename, falling back to the language name when the command is
  // empty. Used to pick presentation rules for completion rows, which differ per
  // server (see runtime/lua/features/ui/completion_label.lua).
  std::string server_id() const;
  const std::string &get_root_path() const
  {
    return root_path;
  }
  const std::string &get_last_error() const
  {
    return last_error;
  }
  std::string describe() const;

  static std::string file_uri_from_path(const std::string &path);
  static std::string file_path_from_uri(const std::string &uri);
  static int utf16_offset_from_utf8(const std::string &text, int byte_offset);
  static int utf8_offset_from_utf16(const std::string &text, int utf16_offset);
};

#endif
