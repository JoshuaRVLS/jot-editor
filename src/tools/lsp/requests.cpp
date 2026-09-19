// Outgoing LSP requests: document synchronization (didOpen/didChange/
// didSave/didClose) and the request builders for completion, hover,
// signature help, definition, document symbols, and formatting, plus
// the consume_* accessors the editor drains results through.
#include "tools/lsp/internal.h"
#include "tools/lsp/client.h"
#include "tools/string_util.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using namespace lsp_detail;

bool LSPClient::did_open(const std::string &filepath,
                         const std::string &language_id,
                         const std::string &text)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (file_versions.count(abs_path))
  {
    return did_change(abs_path, text);
  }
  file_versions[abs_path] = 1;
  document_texts[abs_path] = text;
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didOpen\","
       << "\"params\":{"
       << "\"textDocument\":{"
       << "\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\","
       << "\"languageId\":\"" << json_escape(language_id) << "\","
       << "\"version\":1,"
       << "\"text\":\"" << json_escape(text) << "\""
       << "}"
       << "}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::did_change(const std::string &filepath, const std::string &text)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.count(abs_path))
  {
    return did_open(abs_path, language_id_for(language, abs_path), text);
  }

  document_texts[abs_path] = text;
  int version = ++file_versions[abs_path];
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didChange\","
       << "\"params\":{"
       << "\"textDocument\":{"
       << "\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\","
       << "\"version\":" << version << "},"
       << "\"contentChanges\":[{\"text\":\"" << json_escape(text) << "\"}]"
       << "}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::did_save(const std::string &filepath, const std::string &text)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.count(abs_path))
  {
    if (!did_open(abs_path, language_id_for(language, abs_path), text))
    {
      return false;
    }
  }
  document_texts[abs_path] = text;
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didSave\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"text\":\"" << json_escape(text) << "\""
       << "}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::did_close(const std::string &filepath)
{
  if (!running)
  {
    return false;
  }

  const std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.erase(abs_path))
  {
    return true;
  }
  document_texts.erase(abs_path);
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didClose\","
       << "\"params\":{\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"}}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::request_completion(const std::string &filepath,
                                   int line,
                                   int character,
                                   char trigger_character)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_completion_requests.size() >= 64)
  {
    last_error = "too many pending LSP completion requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_completion_requests[request_id] =
      PendingDocumentRequest{abs_path, file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/completion\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}";

  if (trigger_character != '\0')
  {
    json << ",\"context\":{\"triggerKind\":2,\"triggerCharacter\":\""
         << json_escape(std::string(1, trigger_character)) << "\"}";
  }
  else
  {
    json << ",\"context\":{\"triggerKind\":1}";
  }

  json << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_completion_requests.erase(request_id);
    return false;
  }

  return true;
}

bool LSPClient::request_hover(const std::string &filepath, int line, int character)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_hover_requests.size() >= 64)
  {
    last_error = "too many pending LSP hover requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_hover_requests[request_id] = PendingPositionRequest{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/hover\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_hover_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_signature_help(const std::string &filepath,
                                       int line,
                                       int character,
                                       char trigger_character)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_signature_requests.size() >= 32)
  {
    return false;
  }
  int request_id = next_request_id++;
  pending_signature_requests[request_id] = PendingPositionRequest{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/signatureHelp\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}";

  if (trigger_character != '\0')
  {
    json << ",\"context\":{\"triggerKind\":2,\"triggerCharacter\":\""
         << json_escape(std::string(1, trigger_character)) << "\"}";
  }
  else
  {
    json << ",\"context\":{\"triggerKind\":1}";
  }

  json << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_signature_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_inlay_hints(const std::string &filepath,
                                    int start_line,
                                    int start_character,
                                    int end_line,
                                    int end_character)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_inlay_hint_requests.size() >= 16)
  {
    return false;
  }
  int request_id = next_request_id++;
  pending_inlay_hint_requests[request_id] = PendingInlayRequest{
      abs_path,
      std::max(0, start_line),
      std::max(0, start_character),
      std::max(0, end_line),
      std::max(0, end_character),
      file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/inlayHint\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"range\":{"
       << "\"start\":{\"line\":" << std::max(0, start_line) << ",\"character\":"
       << lsp_character(abs_path, start_line, start_character) << "},"
       << "\"end\":{\"line\":" << std::max(0, end_line) << ",\"character\":"
       << lsp_character(abs_path, end_line, end_character) << "}"
       << "}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_inlay_hint_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_definition(const std::string &filepath, int line, int character)
{
  return request_navigation(filepath, line, character, LSPNavigationKind::Definition);
}

bool LSPClient::request_navigation(const std::string &filepath,
                                   int line,
                                   int character,
                                   LSPNavigationKind kind)
{
  if (!running)
  {
    return false;
  }

  const char *method = lsp_detail::navigation_method_name(kind);

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_definition_requests.size() >= 64)
  {
    last_error = "too many pending LSP navigation requests";
    return false;
  }
  int request_id = next_request_id++;
  PendingPositionRequest pending{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};
  pending.navigation = kind;
  pending_definition_requests[request_id] = pending;

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"" << method << "\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_definition_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_switch_source_header(const std::string &filepath)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_switch_source_header_requests.size() >= 16)
  {
    last_error = "too many pending LSP switch-source-header requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_switch_source_header_requests[request_id] =
      PendingDocumentRequest{abs_path, file_versions[abs_path]};

  // clangd's extension takes the TextDocumentIdentifier itself as params (not
  // a {"textDocument": ...} wrapper), and answers with the paired file's URI
  // -- "" when it cannot tell which file that is.
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/switchSourceHeader\","
       << "\"params\":{"
       << "\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\""
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_switch_source_header_requests.erase(request_id);
    return false;
  }
  return true;
}

std::vector<std::string> LSPClient::consume_switch_source_header_results()
{
  std::vector<std::string> out;
  out.swap(pending_switch_source_headers);
  return out;
}

bool LSPClient::request_references(const std::string &filepath, int line, int character)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_reference_requests.size() >= 64)
  {
    last_error = "too many pending LSP reference requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_reference_requests[request_id] = PendingPositionRequest{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/references\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "},"
       << "\"context\":{\"includeDeclaration\":true}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_reference_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_code_actions(const std::string &filepath,
                                     int line,
                                     int character,
                                     const std::vector<Diagnostic> &diagnostics)
{
  if (!running || !initialized)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_code_action_requests.size() >= 64)
  {
    last_error = "too many pending LSP code action requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_code_action_requests[request_id] = PendingPositionRequest{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/codeAction\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"range\":{"
       << "\"start\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "},"
       << "\"end\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}"
       << "},"
       << "\"context\":{\"diagnostics\":[";
  bool first = true;
  for (const auto &diag : diagnostics)
  {
    if (!first)
    {
      json << ",";
    }
    first = false;
    json << "{"
         << "\"range\":{"
         << "\"start\":{\"line\":" << diag.line
         << ",\"character\":" << lsp_character(abs_path, diag.line, diag.col) << "},"
         << "\"end\":{\"line\":" << diag.end_line
         << ",\"character\":" << lsp_character(abs_path, diag.end_line, diag.end_col) << "}"
         << "},"
         << "\"severity\":" << diag.severity << ","
         << "\"message\":\"" << json_escape(diag.message) << "\""
         << "}";
  }
  json << "]}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_code_action_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_rename(const std::string &filepath,
                               int line,
                               int character,
                               const std::string &new_name)
{
  if (!running || !initialized)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_rename_requests.size() >= 64)
  {
    last_error = "too many pending LSP rename requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_rename_requests[request_id] = PendingPositionRequest{
      abs_path, std::max(0, line), std::max(0, character), file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/rename\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "},"
       << "\"newName\":\"" << json_escape(new_name) << "\""
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_rename_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_document_symbols(const std::string &filepath)
{
  if (!running)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_document_symbol_requests.size() >= 64)
  {
    last_error = "too many pending LSP symbol requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_document_symbol_requests[request_id] =
      PendingDocumentRequest{abs_path, file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/documentSymbol\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_document_symbol_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_workspace_symbols(const std::string &query)
{
  if (!running)
  {
    return false;
  }
  if (pending_workspace_symbol_requests.size() >= 64)
  {
    last_error = "too many pending LSP workspace symbol requests";
    return false;
  }
  const int request_id = next_request_id++;
  // No file to key on: a workspace query is not tied to an open buffer, so there
  // is no version to check the answer against.
  pending_workspace_symbol_requests.insert(request_id);

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"workspace/symbol\","
       << "\"params\":{\"query\":\"" << json_escape(query) << "\"}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_workspace_symbol_requests.erase(request_id);
    return false;
  }
  return true;
}

std::vector<std::pair<std::string, std::vector<Diagnostic>>>
LSPClient::consume_published_diagnostics()
{
  auto out = std::move(pending_diagnostics);
  pending_diagnostics.clear();
  last_diagnostics_ = out;
  return out;
}

std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>>
LSPClient::consume_completion_items()
{
  auto out = std::move(pending_completions);
  pending_completions.clear();
  return out;
}

std::vector<LSPHoverResult> LSPClient::consume_hover_results()
{
  auto out = std::move(pending_hovers);
  pending_hovers.clear();
  if (!out.empty())
  {
    last_hover_ = out.back();
  }
  return out;
}

std::vector<LSPSignatureHelpResult> LSPClient::consume_signature_results()
{
  auto out = std::move(pending_signatures);
  pending_signatures.clear();
  return out;
}

std::vector<LSPInlayHintResult> LSPClient::consume_inlay_hint_results()
{
  auto out = std::move(pending_inlay_hints);
  pending_inlay_hints.clear();
  return out;
}

std::vector<LSPDocumentSymbolResult> LSPClient::consume_workspace_symbol_results()
{
  auto out = std::move(pending_workspace_symbols);
  pending_workspace_symbols.clear();
  return out;
}

std::vector<LSPDefinitionResult> LSPClient::consume_definition_results()
{
  auto out = std::move(pending_definitions);
  pending_definitions.clear();
  if (!out.empty())
  {
    last_definitions_ = out;
  }
  return out;
}

std::vector<LSPDocumentSymbolResult> LSPClient::consume_document_symbol_results()
{
  auto out = std::move(pending_document_symbols);
  pending_document_symbols.clear();
  if (!out.empty())
  {
    last_symbols_ = out;
  }
  return out;
}

bool LSPClient::request_format(const std::string &filepath, int tab_size)
{
  if (!running || !initialized)
  {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_format_requests.size() >= 64)
  {
    last_error = "too many pending LSP format requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_format_requests[request_id] =
      PendingDocumentRequest{abs_path, file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/formatting\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\"},"
       << "\"options\":{\"tabSize\":" << std::max(1, tab_size)
       << ",\"insertSpaces\":true}"
       << "}"
       << "}";

  if (!send_message(json.str()))
  {
    pending_format_requests.erase(request_id);
    return false;
  }
  return true;
}

std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> LSPClient::consume_format_results()
{
  auto out = std::move(pending_formats);
  pending_formats.clear();
  return out;
}

std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> LSPClient::consume_rename_results()
{
  auto out = std::move(pending_renames);
  pending_renames.clear();
  return out;
}

std::vector<LSPDefinitionResult> LSPClient::consume_reference_results()
{
  auto out = std::move(pending_references);
  pending_references.clear();
  return out;
}

std::vector<LSPCodeActionResult> LSPClient::consume_code_action_results()
{
  auto out = std::move(pending_code_actions);
  pending_code_actions.clear();
  return out;
}

bool LSPClient::has_open_document(const std::string &filepath) const
{
  if (filepath.empty())
  {
    return false;
  }
  return file_versions.find(fs::absolute(filepath).string()) != file_versions.end();
}

std::string LSPClient::describe() const
{
  return language + " @ " + root_path;
}

// The server binary's basename, which is what the presentation rules for
// completion rows key on. Servers are launched by absolute path often enough
// (`/usr/bin/clangd`, a managed install under the cache dir) that the basename is
// the only stable identifier; the language name stands in when there is no
// command to look at.
std::string LSPClient::server_id() const
{
  if (!command.empty() && !command.front().empty())
  {
    const std::string &exe = command.front();
    const size_t slash = exe.find_last_of("/\\");
    std::string base = slash == std::string::npos ? exe : exe.substr(slash + 1);
    // Strip a Windows executable suffix so "clangd.exe" and "clangd" agree.
    const std::string lower = string_util::lower_copy(base);
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".exe") == 0)
    {
      base = base.substr(0, base.size() - 4);
    }
    if (!base.empty())
    {
      return base;
    }
  }
  return language;
}
