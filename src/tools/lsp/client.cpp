#include "tools/lsp/client.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
struct JsonValue {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool bool_value = false;
  long long number_value = 0;
  std::string string_value;
  std::vector<JsonValue> array_value;
  std::map<std::string, JsonValue> object_value;
};

bool set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string get_lsp_log_path(const std::string &language) {
  const char *home = getenv("HOME");
  fs::path base = home ? fs::path(home) / ".config" / "jot" / "logs"
                       : fs::temp_directory_path() / "jot-logs";
  std::error_code ec;
  fs::create_directories(base, ec);
  return (base / ("lsp_" + language + ".log")).string();
}

std::string to_file_uri(const std::string &path) {
  std::error_code ec;
  fs::path resolved = fs::absolute(path, ec);
  if (ec) {
    resolved = fs::path(path);
  }
  std::string uri = "file://" + resolved.lexically_normal().string();
  return uri;
}

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string language_id_for(const std::string &language,
                            const std::string &filepath) {
  if (language == "typescript") {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (ends_with(lower, ".jsx")) {
      return "javascriptreact";
    }
    if (ends_with(lower, ".tsx")) {
      return "typescriptreact";
    }
    if (ends_with(lower, ".js") || ends_with(lower, ".mjs") ||
        ends_with(lower, ".cjs")) {
      return "javascript";
    }
    return "typescript";
  }
  if (language == "cpp") {
    return "cpp";
  }
  return language;
}

void skip_ws(const std::string &text, size_t &pos) {
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    pos++;
  }
}

bool parse_json_string(const std::string &text, size_t &pos, std::string &out) {
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  pos++;
  out.clear();
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == '"') {
      return true;
    }
    if (c == '\\') {
      if (pos >= text.size()) {
        return false;
      }
      char esc = text[pos++];
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        if (pos + 4 > text.size()) {
          return false;
        }
        unsigned int codepoint = 0;
        for (int i = 0; i < 4; i++) {
          char h = text[pos++];
          codepoint <<= 4;
          if (h >= '0' && h <= '9') {
            codepoint |= (unsigned int)(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            codepoint |= (unsigned int)(10 + h - 'a');
          } else if (h >= 'A' && h <= 'F') {
            codepoint |= (unsigned int)(10 + h - 'A');
          } else {
            return false;
          }
        }
        if (codepoint <= 0x7F) {
          out.push_back((char)codepoint);
        } else if (codepoint <= 0x7FF) {
          out.push_back((char)(0xC0 | ((codepoint >> 6) & 0x1F)));
          out.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else {
          out.push_back((char)(0xE0 | ((codepoint >> 12) & 0x0F)));
          out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
          out.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
        break;
      }
      default:
        return false;
      }
      continue;
    }
    out.push_back(c);
  }
  return false;
}

bool parse_json_value(const std::string &text, size_t &pos, JsonValue &out);

bool parse_json_array(const std::string &text, size_t &pos, JsonValue &out) {
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  pos++;
  out = JsonValue{};
  out.type = JsonValue::Array;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == ']') {
    pos++;
    return true;
  }
  while (pos < text.size()) {
    JsonValue item;
    if (!parse_json_value(text, pos, item)) {
      return false;
    }
    out.array_value.push_back(std::move(item));
    skip_ws(text, pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == ']') {
      pos++;
      return true;
    }
    if (text[pos] != ',') {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}

bool parse_json_object(const std::string &text, size_t &pos, JsonValue &out) {
  if (pos >= text.size() || text[pos] != '{') {
    return false;
  }
  pos++;
  out = JsonValue{};
  out.type = JsonValue::Object;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == '}') {
    pos++;
    return true;
  }
  while (pos < text.size()) {
    std::string key;
    if (!parse_json_string(text, pos, key)) {
      return false;
    }
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != ':') {
      return false;
    }
    pos++;
    JsonValue value;
    if (!parse_json_value(text, pos, value)) {
      return false;
    }
    out.object_value[key] = std::move(value);
    skip_ws(text, pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == '}') {
      pos++;
      return true;
    }
    if (text[pos] != ',') {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}

bool parse_json_number(const std::string &text, size_t &pos, JsonValue &out) {
  size_t start = pos;
  if (pos < text.size() && text[pos] == '-') {
    pos++;
  }
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos]))) {
    pos++;
  }
  if (start == pos || (start + 1 == pos && text[start] == '-')) {
    return false;
  }
  out = JsonValue{};
  out.type = JsonValue::Number;
  out.number_value = std::strtoll(text.substr(start, pos - start).c_str(),
                                  nullptr, 10);
  if (pos < text.size() && (text[pos] == '.' || text[pos] == 'e' ||
                            text[pos] == 'E')) {
    while (pos < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[pos])) ||
            text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' ||
            text[pos] == '+' || text[pos] == '-')) {
      pos++;
    }
  }
  return true;
}

bool parse_json_value(const std::string &text, size_t &pos, JsonValue &out) {
  skip_ws(text, pos);
  if (pos >= text.size()) {
    return false;
  }

  char c = text[pos];
  if (c == '"') {
    out = JsonValue{};
    out.type = JsonValue::String;
    return parse_json_string(text, pos, out.string_value);
  }
  if (c == '{') {
    return parse_json_object(text, pos, out);
  }
  if (c == '[') {
    return parse_json_array(text, pos, out);
  }
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
    return parse_json_number(text, pos, out);
  }
  if (text.compare(pos, 4, "null") == 0) {
    out = JsonValue{};
    out.type = JsonValue::Null;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 4, "true") == 0) {
    out = JsonValue{};
    out.type = JsonValue::Bool;
    out.bool_value = true;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 5, "false") == 0) {
    out = JsonValue{};
    out.type = JsonValue::Bool;
    out.bool_value = false;
    pos += 5;
    return true;
  }
  return false;
}

const JsonValue *json_object_get(const JsonValue &value,
                                 const std::string &key) {
  if (value.type != JsonValue::Object) {
    return nullptr;
  }
  auto it = value.object_value.find(key);
  if (it == value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string json_string_or_empty(const JsonValue *value) {
  if (!value || value->type != JsonValue::String) {
    return "";
  }
  return value->string_value;
}

int json_int_or_default(const JsonValue *value, int fallback) {
  if (!value || value->type != JsonValue::Number) {
    return fallback;
  }
  return (int)value->number_value;
}

std::string from_file_uri(const std::string &uri) {
  const std::string prefix = "file://";
  if (uri.rfind(prefix, 0) != 0) {
    return uri;
  }
  std::string path = uri.substr(prefix.size());
  std::string decoded;
  decoded.reserve(path.size());
  for (size_t i = 0; i < path.size(); i++) {
    if (path[i] == '%' && i + 2 < path.size()) {
      auto hex = [](char ch) -> int {
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
      if (hi >= 0 && lo >= 0) {
        decoded.push_back((char)((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    decoded.push_back(path[i]);
  }
  return decoded;
}

bool extract_content_length(const std::string &headers, size_t &length_out) {
  std::istringstream stream(headers);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string prefix = "Content-Length:";
    if (line.rfind(prefix, 0) == 0) {
      std::string number = line.substr(prefix.size());
      size_t pos = 0;
      while (pos < number.size() &&
             std::isspace(static_cast<unsigned char>(number[pos]))) {
        pos++;
      }
      if (pos >= number.size()) {
        return false;
      }
      length_out = (size_t)std::strtoull(number.c_str() + pos, nullptr, 10);
      return true;
    }
  }
  return false;
}

std::vector<Diagnostic> diagnostics_from_json(const JsonValue &diagnostics) {
  std::vector<Diagnostic> parsed;
  if (diagnostics.type != JsonValue::Array) {
    return parsed;
  }

  for (const auto &item : diagnostics.array_value) {
    if (item.type != JsonValue::Object) {
      continue;
    }
    const JsonValue *range = json_object_get(item, "range");
    const JsonValue *start = range ? json_object_get(*range, "start") : nullptr;
    const JsonValue *end = range ? json_object_get(*range, "end") : nullptr;

    Diagnostic diag;
    diag.line = json_int_or_default(start ? json_object_get(*start, "line")
                                          : nullptr,
                                    0);
    diag.col = json_int_or_default(start ? json_object_get(*start, "character")
                                         : nullptr,
                                   0);
    diag.end_line =
        json_int_or_default(end ? json_object_get(*end, "line") : nullptr,
                            diag.line);
    diag.end_col =
        json_int_or_default(end ? json_object_get(*end, "character") : nullptr,
                            diag.col);
    diag.message = json_string_or_empty(json_object_get(item, "message"));
    diag.severity =
        json_int_or_default(json_object_get(item, "severity"), 1);
    parsed.push_back(std::move(diag));
  }

  return parsed;
}

std::vector<LSPCompletionItem> completion_items_from_json(
    const JsonValue &result) {
  const JsonValue *items = nullptr;
  if (result.type == JsonValue::Array) {
    items = &result;
  } else if (result.type == JsonValue::Object) {
    items = json_object_get(result, "items");
  }

  std::vector<LSPCompletionItem> parsed;
  if (!items || items->type != JsonValue::Array) {
    return parsed;
  }

  parsed.reserve(items->array_value.size());
  for (const auto &item : items->array_value) {
    if (item.type != JsonValue::Object) {
      continue;
    }

    LSPCompletionItem completion;
    completion.label = json_string_or_empty(json_object_get(item, "label"));
    completion.insert_text =
        json_string_or_empty(json_object_get(item, "insertText"));
    completion.detail = json_string_or_empty(json_object_get(item, "detail"));
    const JsonValue *documentation = json_object_get(item, "documentation");
    if (documentation && documentation->type == JsonValue::String) {
      completion.documentation = documentation->string_value;
    } else if (documentation && documentation->type == JsonValue::Object) {
      completion.documentation =
          json_string_or_empty(json_object_get(*documentation, "value"));
    }
    completion.filter_text =
        json_string_or_empty(json_object_get(item, "filterText"));
    completion.sort_text = json_string_or_empty(json_object_get(item, "sortText"));
    const JsonValue *commit_chars = json_object_get(item, "commitCharacters");
    if (commit_chars && commit_chars->type == JsonValue::Array) {
      for (const auto &commit_char : commit_chars->array_value) {
        if (commit_char.type == JsonValue::String &&
            !commit_char.string_value.empty()) {
          completion.commit_characters.push_back(commit_char.string_value);
        }
      }
    }
    completion.kind = json_int_or_default(json_object_get(item, "kind"), 0);
    completion.insert_text_format =
        json_int_or_default(json_object_get(item, "insertTextFormat"), 1);
    const JsonValue *deprecated = json_object_get(item, "deprecated");
    if (deprecated && deprecated->type == JsonValue::Bool) {
      completion.deprecated = deprecated->bool_value;
    }
    const JsonValue *tags = json_object_get(item, "tags");
    if (tags && tags->type == JsonValue::Array) {
      for (const auto &tag : tags->array_value) {
        if (tag.type == JsonValue::Number && tag.number_value == 1) {
          completion.deprecated = true;
        }
      }
    }
    const JsonValue *preselect = json_object_get(item, "preselect");
    if (preselect && preselect->type == JsonValue::Bool) {
      completion.preselect = preselect->bool_value;
    }

    if (completion.insert_text.empty()) {
      const JsonValue *text_edit = json_object_get(item, "textEdit");
      const JsonValue *new_text =
          text_edit ? json_object_get(*text_edit, "newText") : nullptr;
      completion.insert_text = json_string_or_empty(new_text);

      const JsonValue *range =
          text_edit ? json_object_get(*text_edit, "range") : nullptr;
      const JsonValue *start = range ? json_object_get(*range, "start") : nullptr;
      const JsonValue *end = range ? json_object_get(*range, "end") : nullptr;
      if (start && end) {
        completion.has_text_edit_range = true;
        completion.edit_start_line =
            json_int_or_default(json_object_get(*start, "line"), 0);
        completion.edit_start_char =
            json_int_or_default(json_object_get(*start, "character"), 0);
        completion.edit_end_line =
            json_int_or_default(json_object_get(*end, "line"), 0);
        completion.edit_end_char =
            json_int_or_default(json_object_get(*end, "character"), 0);
      }
    }
    if (completion.insert_text.empty()) {
      completion.insert_text = completion.label;
    }
    if (completion.label.empty()) {
      completion.label = completion.insert_text;
    }
    if (completion.label.empty()) {
      continue;
    }

    parsed.push_back(std::move(completion));
  }

  return parsed;
}

std::string trim_copy(std::string value) {
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isspace(ch);
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [](unsigned char ch) {
                             return !std::isspace(ch);
                           })
                  .base(),
              value.end());
  return value;
}

std::string normalize_hover_text(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return trim_copy(text);
}

std::string hover_content_from_json(const JsonValue &contents) {
  if (contents.type == JsonValue::String) {
    return normalize_hover_text(contents.string_value);
  }
  if (contents.type == JsonValue::Object) {
    const JsonValue *value = json_object_get(contents, "value");
    if (value && value->type == JsonValue::String) {
      return normalize_hover_text(value->string_value);
    }
  }
  if (contents.type == JsonValue::Array) {
    std::string joined;
    for (const auto &item : contents.array_value) {
      std::string part = hover_content_from_json(item);
      if (part.empty()) {
        continue;
      }
      if (!joined.empty()) {
        joined += "\n\n";
      }
      joined += part;
    }
    return normalize_hover_text(joined);
  }
  return "";
}

std::string hover_text_from_result(const JsonValue &result) {
  if (result.type != JsonValue::Object) {
    return "";
  }
  const JsonValue *contents = json_object_get(result, "contents");
  if (!contents) {
    return "";
  }
  return hover_content_from_json(*contents);
}

bool location_from_json(const JsonValue &item, LSPLocation &out) {
  if (item.type != JsonValue::Object) {
    return false;
  }

  const JsonValue *uri = json_object_get(item, "uri");
  const JsonValue *range = json_object_get(item, "range");
  if (!uri || !range) {
    uri = json_object_get(item, "targetUri");
    range = json_object_get(item, "targetSelectionRange");
    if (!range) {
      range = json_object_get(item, "targetRange");
    }
  }
  if (!uri || uri->type != JsonValue::String || !range ||
      range->type != JsonValue::Object) {
    return false;
  }

  const JsonValue *start = json_object_get(*range, "start");
  const JsonValue *end = json_object_get(*range, "end");
  if (!start || start->type != JsonValue::Object) {
    return false;
  }

  out.filepath = from_file_uri(uri->string_value);
  out.line = json_int_or_default(json_object_get(*start, "line"), 0);
  out.character =
      json_int_or_default(json_object_get(*start, "character"), 0);
  out.end_line = end && end->type == JsonValue::Object
                     ? json_int_or_default(json_object_get(*end, "line"),
                                           out.line)
                     : out.line;
  out.end_character =
      end && end->type == JsonValue::Object
          ? json_int_or_default(json_object_get(*end, "character"),
                                out.character)
          : out.character;
  return !out.filepath.empty();
}

std::vector<LSPLocation> definition_locations_from_result(
    const JsonValue &result) {
  std::vector<LSPLocation> locations;
  if (result.type == JsonValue::Array) {
    for (const auto &item : result.array_value) {
      LSPLocation loc;
      if (location_from_json(item, loc)) {
        locations.push_back(std::move(loc));
      }
    }
    return locations;
  }

  LSPLocation loc;
  if (location_from_json(result, loc)) {
    locations.push_back(std::move(loc));
  }
  return locations;
}

std::string symbol_kind_name(int kind) {
  switch (kind) {
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

bool parse_range_start(const JsonValue *range, int &line, int &character,
                       int &end_line, int &end_character) {
  if (!range || range->type != JsonValue::Object) {
    return false;
  }
  const JsonValue *start = json_object_get(*range, "start");
  const JsonValue *end = json_object_get(*range, "end");
  if (!start || start->type != JsonValue::Object) {
    return false;
  }
  line = json_int_or_default(json_object_get(*start, "line"), 0);
  character = json_int_or_default(json_object_get(*start, "character"), 0);
  end_line = end && end->type == JsonValue::Object
                 ? json_int_or_default(json_object_get(*end, "line"), line)
                 : line;
  end_character =
      end && end->type == JsonValue::Object
          ? json_int_or_default(json_object_get(*end, "character"), character)
          : character;
  return true;
}

void append_document_symbol(const JsonValue &item, const std::string &filepath,
                            std::vector<LSPSymbol> &out) {
  if (item.type != JsonValue::Object) {
    return;
  }
  std::string name = json_string_or_empty(json_object_get(item, "name"));
  if (name.empty()) {
    return;
  }

  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
  const JsonValue *range = json_object_get(item, "selectionRange");
  if (!parse_range_start(range, line, character, end_line, end_character)) {
    range = json_object_get(item, "range");
    if (!parse_range_start(range, line, character, end_line, end_character)) {
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
  if (children && children->type == JsonValue::Array) {
    for (const auto &child : children->array_value) {
      append_document_symbol(child, filepath, out);
    }
  }
}

void append_symbol_information(const JsonValue &item,
                               const std::string &fallback_filepath,
                               std::vector<LSPSymbol> &out) {
  if (item.type != JsonValue::Object) {
    return;
  }
  std::string name = json_string_or_empty(json_object_get(item, "name"));
  if (name.empty()) {
    return;
  }
  const JsonValue *location = json_object_get(item, "location");
  if (!location || location->type != JsonValue::Object) {
    return;
  }
  const JsonValue *uri = json_object_get(*location, "uri");
  const JsonValue *range = json_object_get(*location, "range");
  int line = 0;
  int character = 0;
  int end_line = 0;
  int end_character = 0;
  if (!parse_range_start(range, line, character, end_line, end_character)) {
    return;
  }

  int kind = json_int_or_default(json_object_get(item, "kind"), 0);
  LSPSymbol symbol;
  symbol.name = std::move(name);
  symbol.kind = symbol_kind_name(kind);
  symbol.detail =
      json_string_or_empty(json_object_get(item, "containerName"));
  symbol.filepath =
      uri && uri->type == JsonValue::String ? from_file_uri(uri->string_value)
                                            : fallback_filepath;
  symbol.line = line;
  symbol.character = character;
  symbol.end_line = end_line;
  symbol.end_character = end_character;
  if (!symbol.filepath.empty()) {
    out.push_back(std::move(symbol));
  }
}

std::vector<LSPSymbol> document_symbols_from_result(
    const JsonValue &result, const std::string &filepath) {
  std::vector<LSPSymbol> symbols;
  if (result.type != JsonValue::Array) {
    return symbols;
  }
  for (const auto &item : result.array_value) {
    if (item.type != JsonValue::Object) {
      continue;
    }
    if (json_object_get(item, "location")) {
      append_symbol_information(item, filepath, symbols);
    } else {
      append_document_symbol(item, filepath, symbols);
    }
  }
  return symbols;
}
} // namespace

LSPClient::LSPClient(const std::string &language_name,
                     const std::string &workspace_root,
                     const std::vector<std::string> &argv)
    : language(language_name), root_path(workspace_root), command(argv),
      stdin_fd(-1), stdout_fd(-1), stderr_fd(-1), child_pid(-1),
      running(false), initialized(false), next_request_id(1) {}

LSPClient::~LSPClient() { stop(); }

std::string LSPClient::json_escape(const std::string &value) const {
  std::string out;
  out.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
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
      out.push_back((char)c);
      break;
    }
  }
  return out;
}

void LSPClient::append_log_line(const std::string &prefix,
                                const std::string &line) {
  std::ofstream log(get_lsp_log_path(language), std::ios::app);
  if (!log.is_open()) {
    return;
  }
  log << prefix << line << "\n";
}

bool LSPClient::send_message(const std::string &json) {
  if (!running || stdin_fd < 0) {
    return false;
  }

  std::ostringstream payload;
  payload << "Content-Length: " << json.size() << "\r\n\r\n" << json;
  outbound_buffer += payload.str();

  append_log_line("SEND ", json);
  return flush_pending_writes();
}

bool LSPClient::flush_pending_writes() {
  if (!running || stdin_fd < 0) {
    return false;
  }

  while (!outbound_buffer.empty()) {
    ssize_t written =
        write(stdin_fd, outbound_buffer.data(), outbound_buffer.size());
    if (written > 0) {
      outbound_buffer.erase(0, (size_t)written);
      continue;
    }

    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }

    last_error = written < 0 ? strerror(errno) : "LSP stdin closed";
    append_log_line("SEND-ERR ", last_error);
    running = false;
    initialized = false;
    return false;
  }

  return true;
}

bool LSPClient::start() {
  if (running) {
    return true;
  }
  if (command.empty()) {
    last_error = "empty command";
    return false;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    last_error = strerror(errno);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    last_error = strerror(errno);
    return false;
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (const auto &arg : command) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    if (chdir(root_path.c_str()) != 0) {
      _exit(127);
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  stdin_fd = stdin_pipe[1];
  stdout_fd = stdout_pipe[0];
  stderr_fd = stderr_pipe[0];
  child_pid = pid;
  running = true;
  initialized = false;
  next_request_id = 1;
  file_versions.clear();
  pending_completion_requests.clear();
  pending_hover_requests.clear();
  pending_definition_requests.clear();
  pending_document_symbol_requests.clear();
  pending_completions.clear();
  pending_hovers.clear();
  pending_definitions.clear();
  pending_document_symbols.clear();
  stdout_buffer.clear();
  stderr_buffer.clear();
  outbound_buffer.clear();
  last_error.clear();

  set_non_blocking(stdin_fd);
  set_non_blocking(stdout_fd);
  set_non_blocking(stderr_fd);

  std::ostringstream init;
  init << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << next_request_id++ << ","
       << "\"method\":\"initialize\","
       << "\"params\":{"
       << "\"processId\":" << getpid() << ","
       << "\"rootUri\":\"" << json_escape(to_file_uri(root_path)) << "\","
       << "\"rootPath\":\"" << json_escape(root_path) << "\","
       << "\"capabilities\":{"
       << "\"textDocument\":{"
       << "\"completion\":{"
       << "\"dynamicRegistration\":false,"
       << "\"contextSupport\":true,"
       << "\"completionItem\":{"
       << "\"snippetSupport\":true,"
       << "\"deprecatedSupport\":true,"
       << "\"preselectSupport\":true,"
       << "\"commitCharactersSupport\":true,"
       << "\"documentationFormat\":[\"markdown\",\"plaintext\"],"
       << "\"tagSupport\":{\"valueSet\":[1]},"
       << "\"insertReplaceSupport\":false,"
       << "\"resolveSupport\":{\"properties\":[\"documentation\",\"detail\"]}"
       << "}"
       << "},"
       << "\"hover\":{\"dynamicRegistration\":false,"
       << "\"contentFormat\":[\"markdown\",\"plaintext\"]},"
       << "\"definition\":{\"dynamicRegistration\":false,"
       << "\"linkSupport\":true},"
       << "\"documentSymbol\":{\"dynamicRegistration\":false,"
       << "\"hierarchicalDocumentSymbolSupport\":true,"
       << "\"symbolKind\":{\"valueSet\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
          "16,17,18,19,20,21,22,23,24,25,26]}}"
       << "}"
       << "},"
       << "\"workspaceFolders\":[{\"uri\":\""
       << json_escape(to_file_uri(root_path)) << "\",\"name\":\""
       << json_escape(fs::path(root_path).filename().string()) << "\"}]"
       << "}"
       << "}";
  if (!send_message(init.str())) {
    stop();
    return false;
  }

  send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
  initialized = true;
  append_log_line("INFO ", "Started " + describe());
  return true;
}

void LSPClient::stop() {
  if (!running && stdin_fd < 0 && stdout_fd < 0 && stderr_fd < 0 &&
      child_pid <= 0) {
    return;
  }

  if (running && stdin_fd >= 0) {
    send_message("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    flush_pending_writes();
  }
  running = false;
  initialized = false;

  if (child_pid > 0) {
    kill(child_pid, SIGTERM);
    waitpid(child_pid, nullptr, WNOHANG);
  }

  if (stdin_fd >= 0)
    close(stdin_fd);
  if (stdout_fd >= 0)
    close(stdout_fd);
  if (stderr_fd >= 0)
    close(stderr_fd);

  stdin_fd = -1;
  stdout_fd = -1;
  stderr_fd = -1;
  child_pid = -1;
  running = false;
  initialized = false;
  file_versions.clear();
  pending_completion_requests.clear();
  pending_hover_requests.clear();
  pending_definition_requests.clear();
  pending_document_symbol_requests.clear();
  pending_completions.clear();
  pending_hovers.clear();
  pending_definitions.clear();
  pending_document_symbols.clear();
  outbound_buffer.clear();
}

bool LSPClient::restart() {
  stop();
  return start();
}

void LSPClient::handle_stdout_data(const std::string &data) {
  stdout_buffer += data;
  append_log_line("RECV ", data);

  while (true) {
    const size_t header_end = stdout_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return;
    }

    size_t content_length = 0;
    if (!extract_content_length(stdout_buffer.substr(0, header_end),
                                content_length)) {
      append_log_line("PARSE-ERR ", "Missing Content-Length header");
      stdout_buffer.erase(0, header_end + 4);
      continue;
    }

    const size_t body_start = header_end + 4;
    if (stdout_buffer.size() < body_start + content_length) {
      return;
    }

    std::string message = stdout_buffer.substr(body_start, content_length);
    stdout_buffer.erase(0, body_start + content_length);

    size_t pos = 0;
    JsonValue root;
    if (!parse_json_value(message, pos, root)) {
      append_log_line("PARSE-ERR ", "Invalid JSON payload");
      continue;
    }

    const JsonValue *method = json_object_get(root, "method");
    if (method && method->type == JsonValue::String &&
        method->string_value == "textDocument/publishDiagnostics") {
      const JsonValue *params = json_object_get(root, "params");
      const JsonValue *uri = params ? json_object_get(*params, "uri") : nullptr;
      const JsonValue *diagnostics =
          params ? json_object_get(*params, "diagnostics") : nullptr;
      if (!uri || !diagnostics) {
        continue;
      }

      pending_diagnostics.push_back(
          {from_file_uri(json_string_or_empty(uri)),
           diagnostics_from_json(*diagnostics)});
      continue;
    }

    const JsonValue *id = json_object_get(root, "id");
    if (!id || id->type != JsonValue::Number) {
      continue;
    }

    int request_id = (int)id->number_value;
    const JsonValue *result = json_object_get(root, "result");

    auto completion_it = pending_completion_requests.find(request_id);
    if (completion_it != pending_completion_requests.end()) {
      if (result) {
        pending_completions.push_back(
            {completion_it->second, completion_items_from_json(*result)});
      } else {
        pending_completions.push_back({completion_it->second, {}});
      }
      pending_completion_requests.erase(completion_it);
      continue;
    }

    auto hover_it = pending_hover_requests.find(request_id);
    if (hover_it != pending_hover_requests.end()) {
      LSPHoverResult hover;
      hover.origin_filepath = hover_it->second.filepath;
      hover.origin_line = hover_it->second.line;
      hover.origin_character = hover_it->second.character;
      if (result) {
        hover.contents = hover_text_from_result(*result);
      }
      pending_hovers.push_back(std::move(hover));
      pending_hover_requests.erase(hover_it);
      continue;
    }

    auto definition_it = pending_definition_requests.find(request_id);
    if (definition_it != pending_definition_requests.end()) {
      LSPDefinitionResult definition;
      definition.origin_filepath = definition_it->second.filepath;
      definition.origin_line = definition_it->second.line;
      definition.origin_character = definition_it->second.character;
      if (result) {
        definition.locations = definition_locations_from_result(*result);
      }
      pending_definitions.push_back(std::move(definition));
      pending_definition_requests.erase(definition_it);
      continue;
    }

    auto symbol_it = pending_document_symbol_requests.find(request_id);
    if (symbol_it != pending_document_symbol_requests.end()) {
      LSPDocumentSymbolResult symbols;
      symbols.filepath = symbol_it->second;
      if (result) {
        symbols.symbols = document_symbols_from_result(*result, symbols.filepath);
      }
      pending_document_symbols.push_back(std::move(symbols));
      pending_document_symbol_requests.erase(symbol_it);
      continue;
    }
  }
}

void LSPClient::handle_stderr_data(const std::string &data) {
  stderr_buffer += data;
  append_log_line("STDERR ", data);
}

bool LSPClient::poll() {
  if (!running) {
    return false;
  }

  bool changed = false;
  if (!outbound_buffer.empty()) {
    changed = true;
    if (!flush_pending_writes()) {
      return true;
    }
  }

  char buf[4096];
  while (stdout_fd >= 0) {
    ssize_t n = read(stdout_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stdout_data(std::string(buf, buf + n));
    changed = true;
  }
  while (stderr_fd >= 0) {
    ssize_t n = read(stderr_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stderr_data(std::string(buf, buf + n));
    changed = true;
  }

  int status = 0;
  if (child_pid > 0) {
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid) {
      running = false;
      initialized = false;
      if (WIFEXITED(status)) {
        last_error = "process exited with status " + std::to_string(WEXITSTATUS(status));
      } else {
        last_error = "process exited unexpectedly";
      }
      append_log_line("INFO ", last_error);
      changed = true;
    }
  }

  return changed;
}

bool LSPClient::did_open(const std::string &filepath,
                         const std::string &language_id,
                         const std::string &text) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  file_versions[abs_path] = 1;
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

bool LSPClient::did_change(const std::string &filepath, const std::string &text) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.count(abs_path)) {
    return did_open(abs_path, language_id_for(language, abs_path), text);
  }

  int version = ++file_versions[abs_path];
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didChange\","
       << "\"params\":{"
       << "\"textDocument\":{"
       << "\"uri\":\"" << json_escape(to_file_uri(abs_path)) << "\","
       << "\"version\":" << version
       << "},"
       << "\"contentChanges\":[{\"text\":\"" << json_escape(text) << "\"}]"
       << "}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::did_save(const std::string &filepath, const std::string &text) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.count(abs_path)) {
    did_open(abs_path, language_id_for(language, abs_path), text);
  }
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didSave\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"text\":\"" << json_escape(text) << "\""
       << "}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::request_completion(const std::string &filepath, int line,
                                   int character, char trigger_character) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  int request_id = next_request_id++;
  pending_completion_requests[request_id] = abs_path;

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/completion\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << std::max(0, character) << "}";

  if (trigger_character != '\0') {
    json << ",\"context\":{\"triggerKind\":2,\"triggerCharacter\":\""
         << json_escape(std::string(1, trigger_character)) << "\"}";
  } else {
    json << ",\"context\":{\"triggerKind\":1}";
  }

  json << "}"
       << "}";

  if (!send_message(json.str())) {
    pending_completion_requests.erase(request_id);
    return false;
  }

  return true;
}

bool LSPClient::request_hover(const std::string &filepath, int line,
                              int character) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  int request_id = next_request_id++;
  pending_hover_requests[request_id] =
      PendingPositionRequest{abs_path, std::max(0, line), std::max(0, character)};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/hover\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << std::max(0, character) << "}"
       << "}"
       << "}";

  if (!send_message(json.str())) {
    pending_hover_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_definition(const std::string &filepath, int line,
                                   int character) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  int request_id = next_request_id++;
  pending_definition_requests[request_id] =
      PendingPositionRequest{abs_path, std::max(0, line), std::max(0, character)};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/definition\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << std::max(0, character) << "}"
       << "}"
       << "}";

  if (!send_message(json.str())) {
    pending_definition_requests.erase(request_id);
    return false;
  }
  return true;
}

bool LSPClient::request_document_symbols(const std::string &filepath) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  int request_id = next_request_id++;
  pending_document_symbol_requests[request_id] = abs_path;

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/documentSymbol\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"}"
       << "}"
       << "}";

  if (!send_message(json.str())) {
    pending_document_symbol_requests.erase(request_id);
    return false;
  }
  return true;
}

std::vector<std::pair<std::string, std::vector<Diagnostic>>>
LSPClient::consume_published_diagnostics() {
  auto out = std::move(pending_diagnostics);
  pending_diagnostics.clear();
  return out;
}

std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>>
LSPClient::consume_completion_items() {
  auto out = std::move(pending_completions);
  pending_completions.clear();
  return out;
}

std::vector<LSPHoverResult> LSPClient::consume_hover_results() {
  auto out = std::move(pending_hovers);
  pending_hovers.clear();
  return out;
}

std::vector<LSPDefinitionResult> LSPClient::consume_definition_results() {
  auto out = std::move(pending_definitions);
  pending_definitions.clear();
  return out;
}

std::vector<LSPDocumentSymbolResult>
LSPClient::consume_document_symbol_results() {
  auto out = std::move(pending_document_symbols);
  pending_document_symbols.clear();
  return out;
}

std::string LSPClient::describe() const {
  return language + " @ " + root_path;
}
