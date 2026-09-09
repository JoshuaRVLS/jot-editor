// LSP protocol conversion: file URIs, UTF-16/UTF-8 offsets, language
// ids, and the JSON -> result-struct parsers for every feature the
// client supports. The LSPClient members here are pure conversions;
// transport and request/response plumbing live in client.cpp and
// messages.cpp.
#include "tools/lsp/internal.h"
#include "tools/lsp/client.h"
#include "tools/string_util.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using namespace lsp_detail;

namespace lsp_detail
{
  std::string to_file_uri(const std::string &path)
  {
    std::error_code ec;
    fs::path resolved = fs::absolute(path, ec);
    if (ec)
    {
      resolved = fs::path(path);
    }
    std::string normalized = resolved.lexically_normal().generic_string();
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(normalized.size());
    for (unsigned char ch : normalized)
    {
      const bool unreserved = std::isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~'
                              || ch == '/' || ch == ':';
      if (unreserved)
      {
        encoded.push_back(static_cast<char>(ch));
      }
      else
      {
        encoded.push_back('%');
        encoded.push_back(hex[ch >> 4]);
        encoded.push_back(hex[ch & 0x0f]);
      }
    }
#ifdef _WIN32
    if (encoded.size() >= 2 && encoded[1] == ':')
    {
      return "file:///" + encoded;
    }
#endif
    return "file://" + encoded;
  }

  std::string language_id_for(const std::string &language, const std::string &filepath)
  {
    if (language == "typescript")
    {
      std::string lower = filepath;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (string_util::ends_with(lower, ".jsx"))
      {
        return "javascriptreact";
      }
      if (string_util::ends_with(lower, ".tsx"))
      {
        return "typescriptreact";
      }
      if (string_util::ends_with(lower, ".js") || string_util::ends_with(lower, ".mjs")
          || string_util::ends_with(lower, ".cjs"))
      {
        return "javascript";
      }
      return "typescript";
    }
    if (language == "cpp")
    {
      return "cpp";
    }
    return language;
  }

  std::string from_file_uri(const std::string &uri)
  {
    const std::string prefix = "file://";
    if (uri.rfind(prefix, 0) != 0)
    {
      return uri;
    }
    std::string path = uri.substr(prefix.size());
    std::string decoded;
    decoded.reserve(path.size());
    for (size_t i = 0; i < path.size(); i++)
    {
      if (path[i] == '%' && i + 2 < path.size())
      {
        auto hex = [](char ch) -> int
        {
          if (ch >= '0' && ch <= '9')
            return ch - '0';
          if (ch >= 'a' && ch <= 'f')
            return 10 + ch - 'a';
          if (ch >= 'A' && ch <= 'F')
            return 10 + ch - 'A';
          return -1;
        };
        int hi = hex(path[i + 1]);
        int lo = hex(path[i + 2]);
        if (hi >= 0 && lo >= 0)
        {
          decoded.push_back((char)((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      decoded.push_back(path[i]);
    }
#ifdef _WIN32
    if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':')
    {
      decoded.erase(decoded.begin());
    }
    std::replace(decoded.begin(), decoded.end(), '/', '\\');
#endif
    return decoded;
  }

  bool extract_content_length(const std::string &headers, size_t &length_out)
  {
    std::istringstream stream(headers);
    std::string line;
    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      const std::string prefix = "Content-Length:";
      if (line.rfind(prefix, 0) == 0)
      {
        std::string number = line.substr(prefix.size());
        size_t pos = 0;
        while (pos < number.size() && std::isspace(static_cast<unsigned char>(number[pos])))
        {
          pos++;
        }
        if (pos >= number.size())
        {
          return false;
        }
        length_out = (size_t)std::strtoull(number.c_str() + pos, nullptr, 10);
        return true;
      }
    }
    return false;
  }

  std::vector<Diagnostic> diagnostics_from_json(const JsonValue &diagnostics)
  {
    std::vector<Diagnostic> parsed;
    if (diagnostics.type != JsonValue::Array)
    {
      return parsed;
    }

    for (const auto &item : diagnostics.array_value)
    {
      if (item.type != JsonValue::Object)
      {
        continue;
      }
      const JsonValue *range = json_object_get(item, "range");
      const JsonValue *start = range ? json_object_get(*range, "start") : nullptr;
      const JsonValue *end = range ? json_object_get(*range, "end") : nullptr;

      Diagnostic diag;
      diag.line = json_int_or_default(start ? json_object_get(*start, "line") : nullptr, 0);
      diag.col = json_int_or_default(start ? json_object_get(*start, "character") : nullptr, 0);
      diag.end_line = json_int_or_default(end ? json_object_get(*end, "line") : nullptr, diag.line);
      diag.end_col =
          json_int_or_default(end ? json_object_get(*end, "character") : nullptr, diag.col);
      diag.message = json_string_or_empty(json_object_get(item, "message"));
      diag.severity = json_int_or_default(json_object_get(item, "severity"), 1);
      parsed.push_back(std::move(diag));
    }

    return parsed;
  }

  std::vector<LSPCompletionItem> completion_items_from_json(const JsonValue &result)
  {
    const JsonValue *items = nullptr;
    if (result.type == JsonValue::Array)
    {
      items = &result;
    }
    else if (result.type == JsonValue::Object)
    {
      items = json_object_get(result, "items");
    }

    std::vector<LSPCompletionItem> parsed;
    if (!items || items->type != JsonValue::Array)
    {
      return parsed;
    }

    parsed.reserve(items->array_value.size());
    for (const auto &item : items->array_value)
    {
      if (item.type != JsonValue::Object)
      {
        continue;
      }

      LSPCompletionItem completion;
      completion.label = json_string_or_empty(json_object_get(item, "label"));
      completion.insert_text = json_string_or_empty(json_object_get(item, "insertText"));
      completion.detail = json_string_or_empty(json_object_get(item, "detail"));
      const JsonValue *documentation = json_object_get(item, "documentation");
      if (documentation && documentation->type == JsonValue::String)
      {
        completion.documentation = documentation->string_value;
      }
      else if (documentation && documentation->type == JsonValue::Object)
      {
        completion.documentation = json_string_or_empty(json_object_get(*documentation, "value"));
      }
      completion.filter_text = json_string_or_empty(json_object_get(item, "filterText"));
      completion.sort_text = json_string_or_empty(json_object_get(item, "sortText"));
      const JsonValue *commit_chars = json_object_get(item, "commitCharacters");
      if (commit_chars && commit_chars->type == JsonValue::Array)
      {
        for (const auto &commit_char : commit_chars->array_value)
        {
          if (commit_char.type == JsonValue::String && !commit_char.string_value.empty())
          {
            completion.commit_characters.push_back(commit_char.string_value);
          }
        }
      }
      completion.kind = json_int_or_default(json_object_get(item, "kind"), 0);
      completion.insert_text_format =
          json_int_or_default(json_object_get(item, "insertTextFormat"), 1);
      const JsonValue *deprecated = json_object_get(item, "deprecated");
      if (deprecated && deprecated->type == JsonValue::Bool)
      {
        completion.deprecated = deprecated->bool_value;
      }
      const JsonValue *tags = json_object_get(item, "tags");
      if (tags && tags->type == JsonValue::Array)
      {
        for (const auto &tag : tags->array_value)
        {
          if (tag.type == JsonValue::Number && tag.number_value == 1)
          {
            completion.deprecated = true;
          }
        }
      }
      const JsonValue *preselect = json_object_get(item, "preselect");
      if (preselect && preselect->type == JsonValue::Bool)
      {
        completion.preselect = preselect->bool_value;
      }

      if (completion.insert_text.empty())
      {
        const JsonValue *text_edit = json_object_get(item, "textEdit");
        const JsonValue *new_text = text_edit ? json_object_get(*text_edit, "newText") : nullptr;
        completion.insert_text = json_string_or_empty(new_text);

        const JsonValue *range = text_edit ? json_object_get(*text_edit, "range") : nullptr;
        const JsonValue *start = range ? json_object_get(*range, "start") : nullptr;
        const JsonValue *end = range ? json_object_get(*range, "end") : nullptr;
        if (start && end)
        {
          completion.has_text_edit_range = true;
          completion.edit_start_line = json_int_or_default(json_object_get(*start, "line"), 0);
          completion.edit_start_char = json_int_or_default(json_object_get(*start, "character"), 0);
          completion.edit_end_line = json_int_or_default(json_object_get(*end, "line"), 0);
          completion.edit_end_char = json_int_or_default(json_object_get(*end, "character"), 0);
        }
      }
      if (completion.insert_text.empty())
      {
        completion.insert_text = completion.label;
      }
      if (completion.label.empty())
      {
        completion.label = completion.insert_text;
      }
      if (completion.label.empty())
      {
        continue;
      }

      parsed.push_back(std::move(completion));
    }

    return parsed;
  }

  std::string trim_copy(std::string value)
  {
    return string_util::trim_copy(value);
  }

  std::string normalize_hover_text(std::string text)
  {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return trim_copy(text);
  }

  std::string hover_content_from_json(const JsonValue &contents)
  {
    if (contents.type == JsonValue::String)
    {
      return normalize_hover_text(contents.string_value);
    }
    if (contents.type == JsonValue::Object)
    {
      const JsonValue *value = json_object_get(contents, "value");
      if (value && value->type == JsonValue::String)
      {
        return normalize_hover_text(value->string_value);
      }
    }
    if (contents.type == JsonValue::Array)
    {
      std::string joined;
      for (const auto &item : contents.array_value)
      {
        std::string part = hover_content_from_json(item);
        if (part.empty())
        {
          continue;
        }
        if (!joined.empty())
        {
          joined += "\n\n";
        }
        joined += part;
      }
      return normalize_hover_text(joined);
    }
    return "";
  }

  std::string hover_text_from_result(const JsonValue &result)
  {
    if (result.type != JsonValue::Object)
    {
      return "";
    }
    const JsonValue *contents = json_object_get(result, "contents");
    if (!contents)
    {
      return "";
    }
    return hover_content_from_json(*contents);
  }

  // SignatureHelp documentation is a plain string or a MarkupContent object
  // ({kind, value}); both collapse to text.
  std::string signature_doc_from_json(const JsonValue *doc)
  {
    if (!doc)
    {
      return "";
    }
    if (doc->type == JsonValue::String)
    {
      return doc->string_value;
    }
    if (doc->type == JsonValue::Object)
    {
      return json_string_or_empty(json_object_get(*doc, "value"));
    }
    return "";
  }

  LSPSignatureHelpResult signature_help_from_result(const JsonValue &result)
  {
    LSPSignatureHelpResult parsed;
    if (result.type != JsonValue::Object)
    {
      return parsed;
    }
    const JsonValue *signatures = json_object_get(result, "signatures");
    if (!signatures || signatures->type != JsonValue::Array)
    {
      return parsed;
    }
    parsed.active_signature = json_int_or_default(json_object_get(result, "activeSignature"), 0);
    for (const auto &sig : signatures->array_value)
    {
      if (sig.type != JsonValue::Object)
      {
        continue;
      }
      LSPSignature parsed_sig;
      parsed_sig.label = json_string_or_empty(json_object_get(sig, "label"));
      parsed_sig.documentation = signature_doc_from_json(json_object_get(sig, "documentation"));
      parsed_sig.active_parameter =
          json_int_or_default(json_object_get(sig, "activeParameter"), -1);
      const JsonValue *parameters = json_object_get(sig, "parameters");
      if (parameters && parameters->type == JsonValue::Array)
      {
        for (const auto &param : parameters->array_value)
        {
          if (param.type != JsonValue::Object)
          {
            continue;
          }
          LSPSignatureParameter parsed_param;
          const JsonValue *label = json_object_get(param, "label");
          if (label && label->type == JsonValue::Array && label->array_value.size() == 2
              && label->array_value[0].type == JsonValue::Number
              && label->array_value[1].type == JsonValue::Number)
          {
            // Offsets into the signature label: [start, end).
            parsed_param.label_start = (int)label->array_value[0].number_value;
            parsed_param.label_end = (int)label->array_value[1].number_value;
            const size_t s = (size_t)std::max(0, parsed_param.label_start);
            const size_t e = (size_t)std::min((int)parsed_sig.label.size(),
                                              std::max(parsed_param.label_start, parsed_param.label_end));
            if (e > s)
            {
              parsed_param.label = parsed_sig.label.substr(s, e - s);
            }
          }
          else
          {
            parsed_param.label = label && label->type == JsonValue::String
                                     ? label->string_value
                                     : "";
          }
          parsed_param.documentation =
              signature_doc_from_json(json_object_get(param, "documentation"));
          parsed_sig.parameters.push_back(std::move(parsed_param));
        }
      }
      parsed.signatures.push_back(std::move(parsed_sig));
    }
    return parsed;
  }

  std::vector<LSPInlayHint> inlay_hints_from_result(const JsonValue &result)
  {
    std::vector<LSPInlayHint> hints;
    if (result.type != JsonValue::Array)
    {
      return hints;
    }
    for (const auto &item : result.array_value)
    {
      if (item.type != JsonValue::Object)
      {
        continue;
      }
      const JsonValue *position = json_object_get(item, "position");
      if (!position || position->type != JsonValue::Object)
      {
        continue;
      }
      LSPInlayHint hint;
      hint.line = json_int_or_default(json_object_get(*position, "line"), 0);
      hint.character = json_int_or_default(json_object_get(*position, "character"), 0);
      hint.kind = json_int_or_default(json_object_get(item, "kind"), 0);
      hint.padding_left =
          json_object_get(item, "paddingLeft")
              && json_object_get(item, "paddingLeft")->type == JsonValue::Bool
              && json_object_get(item, "paddingLeft")->bool_value;
      hint.padding_right =
          json_object_get(item, "paddingRight")
              && json_object_get(item, "paddingRight")->type == JsonValue::Bool
              && json_object_get(item, "paddingRight")->bool_value;
      const JsonValue *label = json_object_get(item, "label");
      if (label && label->type == JsonValue::String)
      {
        hint.label = label->string_value;
      }
      else if (label && label->type == JsonValue::Array)
      {
        // InlayHintLabelPart list: concatenate the rendered values.
        for (const auto &part : label->array_value)
        {
          if (part.type != JsonValue::Object)
          {
            continue;
          }
          hint.label += json_string_or_empty(json_object_get(part, "value"));
        }
      }
      if (!hint.label.empty())
      {
        hints.push_back(std::move(hint));
      }
    }
    return hints;
  }

  bool location_from_json(const JsonValue &item, LSPLocation &out)
  {
    if (item.type != JsonValue::Object)
    {
      return false;
    }

    const JsonValue *uri = json_object_get(item, "uri");
    const JsonValue *range = json_object_get(item, "range");
    if (!uri || !range)
    {
      uri = json_object_get(item, "targetUri");
      range = json_object_get(item, "targetSelectionRange");
      if (!range)
      {
        range = json_object_get(item, "targetRange");
      }
    }
    if (!uri || uri->type != JsonValue::String || !range || range->type != JsonValue::Object)
    {
      return false;
    }

    const JsonValue *start = json_object_get(*range, "start");
    const JsonValue *end = json_object_get(*range, "end");
    if (!start || start->type != JsonValue::Object)
    {
      return false;
    }

    out.filepath = from_file_uri(uri->string_value);
    out.line = json_int_or_default(json_object_get(*start, "line"), 0);
    out.character = json_int_or_default(json_object_get(*start, "character"), 0);
    out.end_line = end && end->type == JsonValue::Object
                       ? json_int_or_default(json_object_get(*end, "line"), out.line)
                       : out.line;
    out.end_character = end && end->type == JsonValue::Object
                            ? json_int_or_default(json_object_get(*end, "character"), out.character)
                            : out.character;
    return !out.filepath.empty();
  }

  std::vector<LSPLocation> definition_locations_from_result(const JsonValue &result)
  {
    std::vector<LSPLocation> locations;
    if (result.type == JsonValue::Array)
    {
      for (const auto &item : result.array_value)
      {
        LSPLocation loc;
        if (location_from_json(item, loc))
        {
          locations.push_back(std::move(loc));
        }
      }
      return locations;
    }

    LSPLocation loc;
    if (location_from_json(result, loc))
    {
      locations.push_back(std::move(loc));
    }
    return locations;
  }

  std::string symbol_kind_name(int kind)
  {
    switch (kind)
    {
    case 2:
      return "module";
    case 3:
      return "namespace";
    case 4:
      return "package";
    case 5:
      return "class";
    case 6:
      return "method";
    case 7:
      return "property";
    case 8:
      return "field";
    case 9:
      return "constructor";
    case 10:
      return "enum";
    case 11:
      return "interface";
    case 12:
      return "function";
    case 13:
      return "variable";
    case 14:
      return "constant";
    case 23:
      return "struct";
    case 26:
      return "type";
    default:
      return "symbol";
    }
  }

  bool parse_range_start(
      const JsonValue *range, int &line, int &character, int &end_line, int &end_character)
  {
    if (!range || range->type != JsonValue::Object)
    {
      return false;
    }
    const JsonValue *start = json_object_get(*range, "start");
    const JsonValue *end = json_object_get(*range, "end");
    if (!start || start->type != JsonValue::Object)
    {
      return false;
    }
    line = json_int_or_default(json_object_get(*start, "line"), 0);
    character = json_int_or_default(json_object_get(*start, "character"), 0);
    end_line = end && end->type == JsonValue::Object
                   ? json_int_or_default(json_object_get(*end, "line"), line)
                   : line;
    end_character = end && end->type == JsonValue::Object
                        ? json_int_or_default(json_object_get(*end, "character"), character)
                        : character;
    return true;
  }

  void append_document_symbol(const JsonValue &item,
                              const std::string &filepath,
                              std::vector<LSPSymbol> &out)
  {
    if (item.type != JsonValue::Object)
    {
      return;
    }
    std::string name = json_string_or_empty(json_object_get(item, "name"));
    if (name.empty())
    {
      return;
    }

    int line = 0;
    int character = 0;
    int end_line = 0;
    int end_character = 0;
    const JsonValue *range = json_object_get(item, "selectionRange");
    if (!parse_range_start(range, line, character, end_line, end_character))
    {
      range = json_object_get(item, "range");
      if (!parse_range_start(range, line, character, end_line, end_character))
      {
        return;
      }
    }

    int kind = json_int_or_default(json_object_get(item, "kind"), 0);
    LSPSymbol symbol;
    symbol.name = std::move(name);
    symbol.kind = symbol_kind_name(kind);
    symbol.detail = json_string_or_empty(json_object_get(item, "detail"));
    symbol.filepath = filepath;
    symbol.line = line;
    symbol.character = character;
    symbol.end_line = end_line;
    symbol.end_character = end_character;
    out.push_back(std::move(symbol));

    const JsonValue *children = json_object_get(item, "children");
    if (children && children->type == JsonValue::Array)
    {
      for (const auto &child : children->array_value)
      {
        append_document_symbol(child, filepath, out);
      }
    }
  }

  void append_symbol_information(const JsonValue &item,
                                 const std::string &fallback_filepath,
                                 std::vector<LSPSymbol> &out)
  {
    if (item.type != JsonValue::Object)
    {
      return;
    }
    std::string name = json_string_or_empty(json_object_get(item, "name"));
    if (name.empty())
    {
      return;
    }
    const JsonValue *location = json_object_get(item, "location");
    if (!location || location->type != JsonValue::Object)
    {
      return;
    }
    const JsonValue *uri = json_object_get(*location, "uri");
    const JsonValue *range = json_object_get(*location, "range");
    int line = 0;
    int character = 0;
    int end_line = 0;
    int end_character = 0;
    if (!parse_range_start(range, line, character, end_line, end_character))
    {
      return;
    }

    int kind = json_int_or_default(json_object_get(item, "kind"), 0);
    LSPSymbol symbol;
    symbol.name = std::move(name);
    symbol.kind = symbol_kind_name(kind);
    symbol.detail = json_string_or_empty(json_object_get(item, "containerName"));
    symbol.filepath = uri && uri->type == JsonValue::String ? from_file_uri(uri->string_value)
                                                            : fallback_filepath;
    symbol.line = line;
    symbol.character = character;
    symbol.end_line = end_line;
    symbol.end_character = end_character;
    if (!symbol.filepath.empty())
    {
      out.push_back(std::move(symbol));
    }
  }

  std::vector<LSPSymbol> document_symbols_from_result(const JsonValue &result,
                                                      const std::string &filepath)
  {
    std::vector<LSPSymbol> symbols;
    if (result.type != JsonValue::Array)
    {
      return symbols;
    }
    for (const auto &item : result.array_value)
    {
      if (item.type != JsonValue::Object)
      {
        continue;
      }
      if (json_object_get(item, "location"))
      {
        append_symbol_information(item, filepath, symbols);
      }
      else
      {
        append_document_symbol(item, filepath, symbols);
      }
    }
    return symbols;
  }

  // textDocument/formatting returns an array of TextEdit objects
  // ({range:{start,end}, newText}). Positions are returned in the negotiated
  // encoding (usually UTF-16); character offsets stay raw here and the caller
  // converts them to editor columns so a per-document text map is available.
  void format_edits_from_result(const JsonValue &result, std::vector<LSPTextEdit> &out)
  {
    if (result.type != JsonValue::Array)
    {
      return;
    }
    for (const auto &item : result.array_value)
    {
      if (item.type != JsonValue::Object)
      {
        continue;
      }
      LSPTextEdit edit;
      const JsonValue *range = json_object_get(item, "range");
      if (!parse_range_start(range,
                             edit.start_line,
                             edit.start_char,
                             edit.end_line,
                             edit.end_char))
      {
        continue;
      }
      edit.new_text = json_string_or_empty(json_object_get(item, "newText"));
      out.push_back(std::move(edit));
    }
  }
} // namespace lsp_detail

std::string LSPClient::file_uri_from_path(const std::string &path)
{
  return to_file_uri(path);
}

std::string LSPClient::file_path_from_uri(const std::string &uri)
{
  return from_file_uri(uri);
}

int LSPClient::utf16_offset_from_utf8(const std::string &text, int byte_offset)
{
  const int end = std::clamp(byte_offset, 0, (int)text.size());
  int utf16_offset = 0;
  for (int i = 0; i < end;)
  {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    int width = 1;
    unsigned int codepoint = first;
    if ((first & 0xe0) == 0xc0 && i + 1 < end)
    {
      width = 2;
      codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3f);
    }
    else if ((first & 0xf0) == 0xe0 && i + 2 < end)
    {
      width = 3;
      codepoint = ((first & 0x0f) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6)
                  | (static_cast<unsigned char>(text[i + 2]) & 0x3f);
    }
    else if ((first & 0xf8) == 0xf0 && i + 3 < end)
    {
      width = 4;
      codepoint = ((first & 0x07) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12)
                  | ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6)
                  | (static_cast<unsigned char>(text[i + 3]) & 0x3f);
    }
    if (i + width > end)
    {
      break;
    }
    utf16_offset += codepoint > 0xffff ? 2 : 1;
    i += width;
  }
  return utf16_offset;
}

int LSPClient::utf8_offset_from_utf16(const std::string &text, int utf16_offset)
{
  const int target = std::max(0, utf16_offset);
  int units = 0;
  for (int i = 0; i < (int)text.size();)
  {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    int width = 1;
    unsigned int codepoint = first;
    if ((first & 0xe0) == 0xc0 && i + 1 < (int)text.size())
    {
      width = 2;
      codepoint = ((first & 0x1f) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3f);
    }
    else if ((first & 0xf0) == 0xe0 && i + 2 < (int)text.size())
    {
      width = 3;
      codepoint = ((first & 0x0f) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6)
                  | (static_cast<unsigned char>(text[i + 2]) & 0x3f);
    }
    else if ((first & 0xf8) == 0xf0 && i + 3 < (int)text.size())
    {
      width = 4;
      codepoint = ((first & 0x07) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12)
                  | ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6)
                  | (static_cast<unsigned char>(text[i + 3]) & 0x3f);
    }
    if (units >= target)
    {
      return i;
    }
    units += codepoint > 0xffff ? 2 : 1;
    i += width;
    if (units >= target)
    {
      return i;
    }
  }
  return (int)text.size();
}

std::string LSPClient::document_line(const std::string &filepath, int line) const
{
  if (line < 0)
  {
    return "";
  }
  const std::string absolute = fs::absolute(filepath).string();
  auto document = document_texts.find(absolute);
  if (document != document_texts.end())
  {
    std::istringstream lines(document->second);
    std::string value;
    for (int current = 0; std::getline(lines, value); current++)
    {
      if (current == line)
      {
        return value;
      }
    }
    return "";
  }

  std::ifstream file(absolute);
  std::string value;
  for (int current = 0; std::getline(file, value); current++)
  {
    if (current == line)
    {
      return value;
    }
  }
  return "";
}

int LSPClient::lsp_character(const std::string &filepath, int line, int byte_character) const
{
  if (uses_utf8_positions)
  {
    return std::max(0, byte_character);
  }
  return utf16_offset_from_utf8(document_line(filepath, line), byte_character);
}

int LSPClient::editor_character(const std::string &filepath, int line, int character) const
{
  if (uses_utf8_positions)
  {
    return std::max(0, character);
  }
  return utf8_offset_from_utf16(document_line(filepath, line), character);
}

std::string LSPClient::lua_settings_json() const
{
  std::ostringstream out;
  out << "{\"workspace\":{\"library\":[";
  for (size_t i = 0; i < library_dirs.size(); i++)
  {
    if (i > 0)
    {
      out << ",";
    }
    out << "\"" << json_escape(library_dirs[i]) << "\"";
  }
  out << "]},\"diagnostics\":{\"globals\":[\"jot\"]},"
      << "\"completion\":{\"enable\":true}}";
  return out.str();
}

std::string LSPClient::json_escape(const std::string &value) const
{
  std::string out;
  out.reserve(value.size() + 8);
  for (unsigned char c : value)
  {
    switch (c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20)
      {
        static constexpr char hex[] = "0123456789abcdef";
        out += "\\u00";
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
      }
      else
      {
        out.push_back((char)c);
      }
      break;
    }
  }
  return out;
}

