// Internal helpers shared by the LSP client modules (src/tools/lsp/*.cpp):
// the JSON value + parser, file-URI / offset / language-id conversions, and
// the LSP result parsers. Definitions live in json.cpp / protocol.cpp.
#pragma once

#include "tools/lsp/client.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lsp_detail
{
  struct JsonValue
  {
    enum Type
    {
      Null,
      Bool,
      Number,
      String,
      Array,
      Object
    } type = Null;
    bool bool_value = false;
    long long number_value = 0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::map<std::string, JsonValue> object_value;
  };

  // --- JSON parser (json.cpp) ---
  bool parse_json_value(const std::string &text, size_t &pos, JsonValue &out);
  const JsonValue *json_object_get(const JsonValue &value, const std::string &key);
  std::string json_string_or_empty(const JsonValue *value);
  int json_int_or_default(const JsonValue *value, int fallback);

  // --- URI / language conversions (protocol.cpp) ---
  std::string to_file_uri(const std::string &path);
  std::string from_file_uri(const std::string &uri);
  std::string language_id_for(const std::string &language, const std::string &filepath);
  bool extract_content_length(const std::string &headers, size_t &length_out);

  // --- LSP result parsers (protocol.cpp) ---
  std::vector<Diagnostic> diagnostics_from_json(const JsonValue &diagnostics);
  std::vector<LSPCompletionItem> completion_items_from_json(const JsonValue &result);
  std::string trim_copy(std::string value);
  std::string normalize_hover_text(std::string text);
  std::string hover_content_from_json(const JsonValue &contents);
  std::string hover_text_from_result(const JsonValue &result);
  // SignatureHelp documentation is a plain string or a MarkupContent object
  // ({kind, value}); both collapse to text.
  std::string signature_doc_from_json(const JsonValue *doc);
  LSPSignatureHelpResult signature_help_from_result(const JsonValue &result);
  // Parses a textDocument/inlayHint response. Positions are left in the
  // server's negotiated encoding (usually UTF-16); the caller converts
  // characters to byte offsets.
  std::vector<LSPInlayHint> inlay_hints_from_result(const JsonValue &result);
  // Expands a textDocument/rename WorkspaceEdit into per-file edit lists
  // ({filepath, edits}). Handles both `changes` and `documentChanges` forms.
  void workspace_edit_from_result(
      const JsonValue &result,
      std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> &out);
  bool location_from_json(const JsonValue &item, LSPLocation &out);
  std::vector<LSPLocation> definition_locations_from_result(const JsonValue &result);
  std::vector<LSPSymbol> document_symbols_from_result(const JsonValue &result,
                                                      const std::string &filepath);
  std::string symbol_kind_name(int kind);
  // textDocument/formatting returns an array of TextEdit objects
  // ({range:{start,end}, newText}). Positions are returned in the negotiated
  // encoding (usually UTF-16); character offsets stay raw here and the caller
  // converts them to editor columns so a per-document text map is available.
  void format_edits_from_result(const JsonValue &result, std::vector<LSPTextEdit> &out);
} // namespace lsp_detail