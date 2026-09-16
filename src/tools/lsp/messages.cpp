// Incoming LSP transport data: framing (Content-Length headers), JSON
// dispatch, and fan-out of results to the pending-* queues the editor
// drains through the consume_* accessors.
#include "tools/lsp/internal.h"
#include "tools/lsp/client.h"
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using namespace lsp_detail;

void LSPClient::handle_stdout_data(const std::string &data)
{
  constexpr size_t kMaxLspHeaderBytes = 64 * 1024;
  constexpr size_t kMaxLspMessageBytes = 16 * 1024 * 1024;
  stdout_buffer += data;
  append_log_line("RECV ", data);

  if (stdout_buffer.size() > kMaxLspHeaderBytes + kMaxLspMessageBytes)
  {
    last_error = "LSP message exceeds size limit";
    stdout_buffer.clear();
    return;
  }

  while (true)
  {
    const size_t header_end = stdout_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
      if (stdout_buffer.size() > kMaxLspHeaderBytes)
      {
        last_error = "LSP header exceeds size limit";
        stdout_buffer.clear();
      }
      return;
    }

    if (header_end > kMaxLspHeaderBytes)
    {
      last_error = "LSP header exceeds size limit";
      stdout_buffer.clear();
      return;
    }

    size_t content_length = 0;
    if (!extract_content_length(stdout_buffer.substr(0, header_end), content_length))
    {
      append_log_line("PARSE-ERR ", "Missing Content-Length header");
      stdout_buffer.erase(0, header_end + 4);
      continue;
    }
    if (content_length > kMaxLspMessageBytes)
    {
      last_error = "LSP message exceeds size limit";
      stdout_buffer.clear();
      return;
    }

    const size_t body_start = header_end + 4;
    if (stdout_buffer.size() < body_start + content_length)
    {
      return;
    }

    std::string message = stdout_buffer.substr(body_start, content_length);
    stdout_buffer.erase(0, body_start + content_length);

    size_t pos = 0;
    JsonValue root;
    if (!parse_json_value(message, pos, root))
    {
      append_log_line("PARSE-ERR ", "Invalid JSON payload");
      continue;
    }

    const JsonValue *method = json_object_get(root, "method");
    if (method && method->type == JsonValue::String
        && method->string_value == "textDocument/publishDiagnostics")
    {
      const JsonValue *params = json_object_get(root, "params");
      const JsonValue *uri = params ? json_object_get(*params, "uri") : nullptr;
      const JsonValue *diagnostics = params ? json_object_get(*params, "diagnostics") : nullptr;
      if (!uri || !diagnostics)
      {
        continue;
      }

      const std::string filepath = from_file_uri(json_string_or_empty(uri));
      const JsonValue *version = params ? json_object_get(*params, "version") : nullptr;
      auto current_version = file_versions.find(fs::absolute(filepath).string());
      if (version && version->type == JsonValue::Number && current_version != file_versions.end()
          && current_version->second != (int)version->number_value)
      {
        continue;
      }
      auto parsed = diagnostics_from_json(*diagnostics);
      for (auto &diagnostic : parsed)
      {
        diagnostic.col = editor_character(filepath, diagnostic.line, diagnostic.col);
        diagnostic.end_col = editor_character(filepath, diagnostic.end_line, diagnostic.end_col);
      }
      pending_diagnostics.push_back({filepath, std::move(parsed)});
      continue;
    }

    // Server -> client progress ($/progress, LSP 3.15+). Servers report work
    // under a token: begin sets the title, report updates the message and
    // optional percentage, end clears it. Kept as state on the client rather
    // than a queue -- the statusline asks what is running now, and a burst of
    // reports must collapse to the latest, not replay in order.
    if (method && method->type == JsonValue::String && method->string_value == "$/progress")
    {
      const JsonValue *params = json_object_get(root, "params");
      const JsonValue *token = params ? json_object_get(*params, "token") : nullptr;
      const JsonValue *value = params ? json_object_get(*params, "value") : nullptr;
      if (!token || !value || value->type != JsonValue::Object)
      {
        continue;
      }
      std::string key;
      if (token->type == JsonValue::String)
      {
        key = token->string_value;
      }
      else if (token->type == JsonValue::Number)
      {
        key = std::to_string((long long)token->number_value);
      }
      else
      {
        continue;
      }
      int percentage = -1;
      if (const JsonValue *pct = json_object_get(*value, "percentage"))
      {
        if (pct->type == JsonValue::Number)
        {
          percentage = (int)pct->number_value;
        }
      }
      // Logged because progress is otherwise invisible: it drives a statusline
      // spinner and leaves no other trace, so a probe (or a bug report) cannot
      // tell "the server never reported" from "we dropped it".
      append_log_line("RECV ",
                      "$/progress " + key + " "
                          + json_string_or_empty(json_object_get(*value, "kind")));
      apply_lsp_progress(progress_,
                         key,
                         json_string_or_empty(json_object_get(*value, "kind")),
                         json_string_or_empty(json_object_get(*value, "title")),
                         json_string_or_empty(json_object_get(*value, "message")),
                         percentage);
      continue;
    }

    // window/showMessage: the server talking to the user ("indexing failed",
    // "configuration invalid"). Queued for the editor to raise as a toast.
    if (method && method->type == JsonValue::String && method->string_value == "window/showMessage")
    {
      const JsonValue *params = json_object_get(root, "params");
      const std::string text = params ? json_string_or_empty(json_object_get(*params, "message")) : "";
      if (!text.empty())
      {
        pending_show_messages.push_back(text);
      }
      continue;
    }

    // Server -> client request: answer workspace/configuration pulls so
    // servers that support it (lua-language-server) receive our client-side
    // defaults (e.g. the bundled jot API stub registered as a Lua library).
    const JsonValue *id = json_object_get(root, "id");
    if (method && method->type == JsonValue::String && id && id->type == JsonValue::Number
        && (method->string_value == "client/registerCapability"
            || method->string_value == "client/unregisterCapability"))
    {
      // The server dynamically registering a feature (clangd registers watched
      // files this way). A client that never answers leaves the server waiting
      // on the request, and whatever it was going to do once registered never
      // happens.
      std::ostringstream ok;
      ok << "{\"jsonrpc\":\"2.0\",\"id\":" << (int)id->number_value << ",\"result\":null}";
      send_message(ok.str(), true);
      continue;
    }
    if (method && method->type == JsonValue::String && id && id->type == JsonValue::Number
        && method->string_value == "workspace/configuration")
    {
      std::ostringstream cfg;
      cfg << "{\"jsonrpc\":\"2.0\",\"id\":" << (int)id->number_value << ",\"result\":[";
      cfg << (library_dirs.empty() ? "null" : lua_settings_json());
      cfg << ",null,null,null,null]}";
      send_message(cfg.str(), true);
      continue;
    }

    // window/workDoneProgress/create: the server asking permission to report
    // progress under a token. Answering null accepts it; until it is answered
    // the server waits and reports nothing at all -- clangd asks for
    // "backgroundIndexProgress" and stays silent without this reply.
    if (method && method->type == JsonValue::String && id && id->type == JsonValue::Number
        && method->string_value == "window/workDoneProgress/create")
    {
      const JsonValue *params = json_object_get(root, "params");
      const JsonValue *token = params ? json_object_get(*params, "token") : nullptr;
      append_log_line("RECV ",
                      "window/workDoneProgress/create "
                          + (token && token->type == JsonValue::String ? token->string_value
                                                                      : std::string("?")));
      std::ostringstream ok;
      ok << "{\"jsonrpc\":\"2.0\",\"id\":" << (int)id->number_value << ",\"result\":null}";
      send_message(ok.str(), true);
      continue;
    }

    if (!id || id->type != JsonValue::Number)
    {
      continue;
    }

    int request_id = (int)id->number_value;
    const JsonValue *result = json_object_get(root, "result");

    if (request_id == initialize_request_id)
    {
      if (!result || result->type != JsonValue::Object)
      {
        last_error = "LSP initialize request failed";
        stop();
        return;
      }
      const JsonValue *capabilities = json_object_get(*result, "capabilities");
      const JsonValue *encoding =
          capabilities ? json_object_get(*capabilities, "positionEncoding") : nullptr;
      uses_utf8_positions =
          encoding && encoding->type == JsonValue::String && encoding->string_value == "utf-8";
      initialized = true;
      send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}", true);
      // Hand the server our client-side defaults (lowest config priority, so
      // a workspace .luarc.json still wins). For lua this registers the
      // bundled jot API stub as a library, declares the jot global, and
      // enables completion, so user scripts get the whole jot.* surface
      // without "undefined global" warnings.
      if (!library_dirs.empty())
      {
        send_message("{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeConfiguration\","
                         "\"params\":{\"settings\":{\"Lua\":" + lua_settings_json() + "}}}",
                     true);
      }
      auto queued = std::move(deferred_messages);
      deferred_messages.clear();
      for (const auto &message : queued)
      {
        if (!send_message(message, true))
        {
          break;
        }
      }
      append_log_line("INFO ", "Started " + describe());
      continue;
    }

    if (request_id == shutdown_request_id)
    {
      shutdown_complete = true;
      continue;
    }

    auto completion_it = pending_completion_requests.find(request_id);
    if (completion_it != pending_completion_requests.end())
    {
      const auto current_version = file_versions.find(completion_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != completion_it->second.version)
      {
        pending_completion_requests.erase(completion_it);
        continue;
      }
      if (result)
      {
        auto items = completion_items_from_json(*result);
        for (auto &item : items)
        {
          if (item.has_text_edit_range)
          {
            item.edit_start_char = editor_character(
                completion_it->second.filepath, item.edit_start_line, item.edit_start_char);
            item.edit_end_char = editor_character(
                completion_it->second.filepath, item.edit_end_line, item.edit_end_char);
          }
        }
        pending_completions.push_back({completion_it->second.filepath, std::move(items)});
      }
      else
      {
        pending_completions.push_back({completion_it->second.filepath, {}});
      }
      pending_completion_requests.erase(completion_it);
      continue;
    }

    auto hover_it = pending_hover_requests.find(request_id);
    if (hover_it != pending_hover_requests.end())
    {
      const auto current_version = file_versions.find(hover_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != hover_it->second.version)
      {
        pending_hover_requests.erase(hover_it);
        continue;
      }
      LSPHoverResult hover;
      hover.origin_filepath = hover_it->second.filepath;
      hover.origin_line = hover_it->second.line;
      hover.origin_character = hover_it->second.character;
      if (result)
      {
        hover.contents = hover_text_from_result(*result);
      }
      pending_hovers.push_back(std::move(hover));
      pending_hover_requests.erase(hover_it);
      continue;
    }

    auto signature_it = pending_signature_requests.find(request_id);
    if (signature_it != pending_signature_requests.end())
    {
      const auto current_version = file_versions.find(signature_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != signature_it->second.version)
      {
        pending_signature_requests.erase(signature_it);
        continue;
      }
      LSPSignatureHelpResult signature_help;
      signature_help.origin_filepath = signature_it->second.filepath;
      signature_help.origin_line = signature_it->second.line;
      signature_help.origin_character = signature_it->second.character;
      if (result)
      {
        signature_help = signature_help_from_result(*result);
        signature_help.origin_filepath = signature_it->second.filepath;
        signature_help.origin_line = signature_it->second.line;
        signature_help.origin_character = signature_it->second.character;
      }
      pending_signatures.push_back(std::move(signature_help));
      pending_signature_requests.erase(signature_it);
      continue;
    }

    auto inlay_it = pending_inlay_hint_requests.find(request_id);
    if (inlay_it != pending_inlay_hint_requests.end())
    {
      const auto current_version = file_versions.find(inlay_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != inlay_it->second.version)
      {
        pending_inlay_hint_requests.erase(inlay_it);
        continue;
      }
      LSPInlayHintResult inlay;
      inlay.origin_filepath = inlay_it->second.filepath;
      inlay.start_line = inlay_it->second.start_line;
      inlay.start_character = inlay_it->second.start_character;
      inlay.end_line = inlay_it->second.end_line;
      inlay.end_character = inlay_it->second.end_character;
      inlay.version = inlay_it->second.version;
      if (result)
      {
        inlay.hints = inlay_hints_from_result(*result);
        // The server answered in the negotiated encoding (UTF-16 unless it
        // picked UTF-8): convert hint characters to byte offsets so the
        // editor can anchor them against the buffer text.
        if (!uses_utf8_positions)
        {
          for (auto &hint : inlay.hints)
          {
            hint.character = utf8_offset_from_utf16(document_line(inlay.origin_filepath, hint.line),
                                                    hint.character);
          }
        }
      }
      pending_inlay_hints.push_back(std::move(inlay));
      pending_inlay_hint_requests.erase(inlay_it);
      continue;
    }

    auto definition_it = pending_definition_requests.find(request_id);
    if (definition_it != pending_definition_requests.end())
    {
      const auto current_version = file_versions.find(definition_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != definition_it->second.version)
      {
        pending_definition_requests.erase(definition_it);
        continue;
      }
      LSPDefinitionResult definition;
      definition.origin_filepath = definition_it->second.filepath;
      definition.origin_line = definition_it->second.line;
      definition.origin_character = definition_it->second.character;
      if (result)
      {
        definition.locations = definition_locations_from_result(*result);
        for (auto &location : definition.locations)
        {
          location.character =
              editor_character(location.filepath, location.line, location.character);
          location.end_character =
              editor_character(location.filepath, location.end_line, location.end_character);
        }
      }
      pending_definitions.push_back(std::move(definition));
      pending_definition_requests.erase(definition_it);
      continue;
    }

    auto reference_it = pending_reference_requests.find(request_id);
    if (reference_it != pending_reference_requests.end())
    {
      const auto current_version = file_versions.find(reference_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != reference_it->second.version)
      {
        pending_reference_requests.erase(reference_it);
        continue;
      }
      LSPDefinitionResult references;
      references.origin_filepath = reference_it->second.filepath;
      references.origin_line = reference_it->second.line;
      references.origin_character = reference_it->second.character;
      if (result)
      {
        references.locations = definition_locations_from_result(*result);
        for (auto &location : references.locations)
        {
          location.character =
              editor_character(location.filepath, location.line, location.character);
          location.end_character =
              editor_character(location.filepath, location.end_line, location.end_character);
        }
      }
      pending_references.push_back(std::move(references));
      pending_reference_requests.erase(reference_it);
      continue;
    }

    auto code_action_it = pending_code_action_requests.find(request_id);
    if (code_action_it != pending_code_action_requests.end())
    {
      const auto current_version = file_versions.find(code_action_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != code_action_it->second.version)
      {
        pending_code_action_requests.erase(code_action_it);
        continue;
      }
      LSPCodeActionResult actions;
      actions.origin_filepath = code_action_it->second.filepath;
      actions.origin_line = code_action_it->second.line;
      actions.origin_character = code_action_it->second.character;
      if (result)
      {
        code_actions_from_result(*result, actions.actions);
        for (auto &action : actions.actions)
        {
          for (auto &entry : action.edits)
          {
            for (auto &edit : entry.second)
            {
              edit.start_char = editor_character(
                  entry.first, edit.start_line, edit.start_char);
              edit.end_char = editor_character(entry.first, edit.end_line, edit.end_char);
            }
          }
        }
      }
      pending_code_actions.push_back(std::move(actions));
      pending_code_action_requests.erase(code_action_it);
      continue;
    }

    auto symbol_it = pending_document_symbol_requests.find(request_id);
    if (symbol_it != pending_document_symbol_requests.end())
    {
      const auto current_version = file_versions.find(symbol_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != symbol_it->second.version)
      {
        pending_document_symbol_requests.erase(symbol_it);
        continue;
      }
      LSPDocumentSymbolResult symbols;
      symbols.filepath = symbol_it->second.filepath;
      if (result)
      {
        symbols.symbols = document_symbols_from_result(*result, symbols.filepath);
      }
      pending_document_symbols.push_back(std::move(symbols));
      pending_document_symbol_requests.erase(symbol_it);
      continue;
    }

    auto format_it = pending_format_requests.find(request_id);
    if (format_it != pending_format_requests.end())
    {
      const auto current_version = file_versions.find(format_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != format_it->second.version)
      {
        pending_format_requests.erase(format_it);
        continue;
      }
      std::vector<LSPTextEdit> edits;
      if (result)
      {
        format_edits_from_result(*result, edits);
        for (auto &edit : edits)
        {
          edit.start_char = editor_character(
              format_it->second.filepath, edit.start_line, edit.start_char);
          edit.end_char = editor_character(
              format_it->second.filepath, edit.end_line, edit.end_char);
        }
      }
      pending_formats.push_back({format_it->second.filepath, std::move(edits)});
      pending_format_requests.erase(format_it);
      continue;
    }

    auto rename_it = pending_rename_requests.find(request_id);
    if (rename_it != pending_rename_requests.end())
    {
      const auto current_version = file_versions.find(rename_it->second.filepath);
      if (current_version == file_versions.end()
          || current_version->second != rename_it->second.version)
      {
        pending_rename_requests.erase(rename_it);
        continue;
      }
      if (result)
      {
        std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> changes;
        workspace_edit_from_result(*result, changes);
        for (auto &entry : changes)
        {
          for (auto &edit : entry.second)
          {
            edit.start_char = editor_character(
                entry.first, edit.start_line, edit.start_char);
            edit.end_char = editor_character(entry.first, edit.end_line, edit.end_char);
          }
          pending_renames.push_back(std::move(entry));
        }
      }
      pending_rename_requests.erase(rename_it);
      continue;
    }
  }
}

void LSPClient::handle_stderr_data(const std::string &data)
{
  stderr_buffer += data;
  constexpr size_t kMaxLspStderrBytes = 64 * 1024;
  if (stderr_buffer.size() > kMaxLspStderrBytes)
  {
    stderr_buffer.erase(0, stderr_buffer.size() - kMaxLspStderrBytes);
  }
  append_log_line("STDERR ", data);
}


void apply_lsp_progress(std::map<std::string, LSPProgress> &tokens,
                        const std::string &token,
                        const std::string &kind,
                        const std::string &title,
                        const std::string &message,
                        int percentage)
{
  if (token.empty())
  {
    return;
  }
  // "end" (and any unknown kind) retires the token: a server that stops
  // reporting must not leave a spinner running forever.
  if (kind != "begin" && kind != "report")
  {
    tokens.erase(token);
    return;
  }
  LSPProgress &entry = tokens[token];
  // A begin carries the title; reports carry moving text. Empty fields are
  // "unchanged", not "cleared" -- servers omit what did not move.
  if (!title.empty())
  {
    entry.title = title;
  }
  if (!message.empty())
  {
    entry.message = message;
  }
  if (percentage >= 0)
  {
    entry.percentage = percentage;
  }
}
