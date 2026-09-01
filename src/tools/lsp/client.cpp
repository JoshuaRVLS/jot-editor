#include "tools/lsp/client.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string.h>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
using ssize_t = intptr_t;
#define read _read
#define write _write
#define close _close
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
  (void)fd;
  return true;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

#ifdef _WIN32
std::string windows_error_message(DWORD code) {
  char *text = nullptr;
  DWORD len = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&text), 0, nullptr);
  std::string message = len && text ? std::string(text, len)
                                    : "Windows error " + std::to_string(code);
  if (text) {
    LocalFree(text);
  }
  while (!message.empty() &&
         (message.back() == '\r' || message.back() == '\n' ||
          message.back() == ' ')) {
    message.pop_back();
  }
  return message;
}

std::string quote_windows_arg(const std::string &arg) {
  if (arg.empty()) {
    return "\"\"";
  }
  bool needs_quotes = false;
  for (char c : arg) {
    if (std::isspace((unsigned char)c) || c == '"') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return arg;
  }
  std::string out = "\"";
  int backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      backslashes++;
      continue;
    }
    if (c == '"') {
      out.append((size_t)(backslashes * 2 + 1), '\\');
      out.push_back(c);
      backslashes = 0;
      continue;
    }
    out.append((size_t)backslashes, '\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append((size_t)(backslashes * 2), '\\');
  out.push_back('"');
  return out;
}

std::string windows_command_line(const std::vector<std::string> &argv) {
  std::string out;
  for (size_t i = 0; i < argv.size(); i++) {
    if (i > 0) {
      out.push_back(' ');
    }
    out += quote_windows_arg(argv[i]);
  }
  return out;
}

bool fd_has_data(int fd) {
  intptr_t os_handle = _get_osfhandle(fd);
  if (os_handle == -1) {
    return false;
  }
  DWORD available = 0;
  if (!PeekNamedPipe(reinterpret_cast<HANDLE>(os_handle), nullptr, 0, nullptr,
                     &available, nullptr)) {
    return false;
  }
  return available > 0;
}

unsigned long current_process_id() { return GetCurrentProcessId(); }
#else
bool fd_has_data(int) { return true; }
long current_process_id() { return getpid(); }
#endif

std::string get_lsp_log_path(const std::string &language) {
  const char *override_home = getenv("JOT_CONFIG_HOME");
  if (override_home && *override_home) {
    fs::path base = fs::path(override_home) / "logs";
    std::error_code ec;
    fs::create_directories(base, ec);
    return (base / ("lsp_" + language + ".log")).string();
  }
#ifdef _WIN32
  const char *app_data = getenv("APPDATA");
  fs::path base = app_data && *app_data ? fs::path(app_data) / "jot" / "logs"
                                       : fs::temp_directory_path() / "jot-logs";
#else
  const char *home = getenv("HOME");
  fs::path base = home ? fs::path(home) / ".config" / "jot" / "logs"
                       : fs::temp_directory_path() / "jot-logs";
#endif
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
  std::string normalized = resolved.lexically_normal().generic_string();
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(normalized.size());
  for (unsigned char ch : normalized) {
    const bool unreserved = std::isalnum(ch) || ch == '-' || ch == '.' ||
                            ch == '_' || ch == '~' || ch == '/' || ch == ':';
    if (unreserved) {
      encoded.push_back(static_cast<char>(ch));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[ch >> 4]);
      encoded.push_back(hex[ch & 0x0f]);
    }
  }
#ifdef _WIN32
  if (encoded.size() >= 2 && encoded[1] == ':') {
    return "file:///" + encoded;
  }
#endif
  return "file://" + encoded;
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
#ifdef _WIN32
  if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') {
    decoded.erase(decoded.begin());
  }
  std::replace(decoded.begin(), decoded.end(), '/', '\\');
#endif
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

std::string LSPClient::file_uri_from_path(const std::string &path) {
  return to_file_uri(path);
}

std::string LSPClient::file_path_from_uri(const std::string &uri) {
  return from_file_uri(uri);
}

int LSPClient::utf16_offset_from_utf8(const std::string &text, int byte_offset) {
  const int end = std::clamp(byte_offset, 0, (int)text.size());
  int utf16_offset = 0;
  for (int i = 0; i < end;) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    int width = 1;
    unsigned int codepoint = first;
    if ((first & 0xe0) == 0xc0 && i + 1 < end) {
      width = 2;
      codepoint = ((first & 0x1f) << 6) |
                  (static_cast<unsigned char>(text[i + 1]) & 0x3f);
    } else if ((first & 0xf0) == 0xe0 && i + 2 < end) {
      width = 3;
      codepoint = ((first & 0x0f) << 12) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 2]) & 0x3f);
    } else if ((first & 0xf8) == 0xf0 && i + 3 < end) {
      width = 4;
      codepoint = ((first & 0x07) << 18) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12) |
                  ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 3]) & 0x3f);
    }
    if (i + width > end) {
      break;
    }
    utf16_offset += codepoint > 0xffff ? 2 : 1;
    i += width;
  }
  return utf16_offset;
}

int LSPClient::utf8_offset_from_utf16(const std::string &text, int utf16_offset) {
  const int target = std::max(0, utf16_offset);
  int units = 0;
  for (int i = 0; i < (int)text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    int width = 1;
    unsigned int codepoint = first;
    if ((first & 0xe0) == 0xc0 && i + 1 < (int)text.size()) {
      width = 2;
      codepoint = ((first & 0x1f) << 6) |
                  (static_cast<unsigned char>(text[i + 1]) & 0x3f);
    } else if ((first & 0xf0) == 0xe0 && i + 2 < (int)text.size()) {
      width = 3;
      codepoint = ((first & 0x0f) << 12) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 2]) & 0x3f);
    } else if ((first & 0xf8) == 0xf0 && i + 3 < (int)text.size()) {
      width = 4;
      codepoint = ((first & 0x07) << 18) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12) |
                  ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 3]) & 0x3f);
    }
    if (units >= target) {
      return i;
    }
    units += codepoint > 0xffff ? 2 : 1;
    i += width;
    if (units >= target) {
      return i;
    }
  }
  return (int)text.size();
}

std::string LSPClient::document_line(const std::string &filepath, int line) const {
  if (line < 0) {
    return "";
  }
  const std::string absolute = fs::absolute(filepath).string();
  auto document = document_texts.find(absolute);
  if (document != document_texts.end()) {
    std::istringstream lines(document->second);
    std::string value;
    for (int current = 0; std::getline(lines, value); current++) {
      if (current == line) {
        return value;
      }
    }
    return "";
  }

  std::ifstream file(absolute);
  std::string value;
  for (int current = 0; std::getline(file, value); current++) {
    if (current == line) {
      return value;
    }
  }
  return "";
}

int LSPClient::lsp_character(const std::string &filepath, int line,
                             int byte_character) const {
  if (uses_utf8_positions) {
    return std::max(0, byte_character);
  }
  return utf16_offset_from_utf8(document_line(filepath, line), byte_character);
}

int LSPClient::editor_character(const std::string &filepath, int line,
                                int character) const {
  if (uses_utf8_positions) {
    return std::max(0, character);
  }
  return utf8_offset_from_utf16(document_line(filepath, line), character);
}

LSPClient::LSPClient(const std::string &language_name,
                     const std::string &workspace_root,
                     const std::vector<std::string> &argv)
    : language(language_name), root_path(workspace_root), command(argv),
      stdin_fd(-1), stdout_fd(-1), stderr_fd(-1), child_pid(-1),
      running(false), initialized(false), uses_utf8_positions(false),
      shutdown_complete(false), next_request_id(1), initialize_request_id(0),
      shutdown_request_id(0) {}

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
      if (c < 0x20) {
        static constexpr char hex[] = "0123456789abcdef";
        out += "\\u00";
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
      } else {
        out.push_back((char)c);
      }
      break;
    }
  }
  return out;
}

void LSPClient::append_log_line(const std::string &prefix,
                                const std::string &line) {
  constexpr std::uintmax_t kMaxLspLogBytes = 1024 * 1024;
  constexpr size_t kMaxLspLogLineBytes = 16 * 1024;
  const std::string path = get_lsp_log_path(language);
  std::error_code ec;
  if (fs::file_size(path, ec) > kMaxLspLogBytes) {
    std::ofstream truncated_log(path, std::ios::trunc);
  }
  std::ofstream log(path, std::ios::app);
  if (!log.is_open()) {
    return;
  }
  log << prefix << line.substr(0, kMaxLspLogLineBytes) << "\n";
}

bool LSPClient::send_message(const std::string &json,
                             bool allow_during_initialization) {
  constexpr size_t kMaxLspMessageBytes = 16 * 1024 * 1024;
  constexpr size_t kMaxDeferredMessages = 256;
  constexpr size_t kMaxOutboundBytes = 32 * 1024 * 1024;
  if (!running || stdin_fd < 0) {
    return false;
  }
  if (json.size() > kMaxLspMessageBytes) {
    last_error = "LSP outbound message exceeds size limit";
    return false;
  }
  if (!initialized && !allow_during_initialization) {
    if (deferred_messages.size() >= kMaxDeferredMessages) {
      last_error = "LSP initialization queue is full";
      return false;
    }
    deferred_messages.push_back(json);
    return true;
  }

  std::ostringstream payload;
  payload << "Content-Length: " << json.size() << "\r\n\r\n" << json;
  if (outbound_buffer.size() >= kMaxOutboundBytes ||
      (size_t)payload.tellp() > kMaxOutboundBytes - outbound_buffer.size()) {
    last_error = "LSP outbound queue is full";
    return false;
  }
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

#ifndef _WIN32
  static const bool sigpipe_ignored = [] {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)sigpipe_ignored;
#endif

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;

  auto close_handle = [](HANDLE &handle) {
    if (handle) {
      CloseHandle(handle);
      handle = nullptr;
    }
  };

  if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0) ||
      !CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
      !CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_read);
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stdout_write);
    close_handle(stderr_read);
    close_handle(stderr_write);
    return false;
  }

  SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = stdin_read;
  startup.hStdOutput = stdout_write;
  startup.hStdError = stderr_write;

  PROCESS_INFORMATION process{};
  std::string command_line = windows_command_line(command);
  std::string cwd = root_path.empty() ? std::string() : root_path;
  BOOL created = CreateProcessA(
      nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
      nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);

  close_handle(stdin_read);
  close_handle(stdout_write);
  close_handle(stderr_write);

  if (!created) {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stderr_read);
    return false;
  }

  CloseHandle(process.hThread);
  child_process_handle = process.hProcess;
  child_pid = (int)process.dwProcessId;

  stdin_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdin_write),
                             _O_WRONLY | _O_BINARY);
  stdout_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdout_read),
                              _O_RDONLY | _O_BINARY);
  stderr_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stderr_read),
                              _O_RDONLY | _O_BINARY);
  if (stdin_fd < 0 || stdout_fd < 0 || stderr_fd < 0) {
    last_error = "failed to convert process pipes to file descriptors";
    stop();
    return false;
  }
#else
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  auto close_pipe = [](int pipe_fds[2]) {
    if (pipe_fds[0] >= 0) close(pipe_fds[0]);
    if (pipe_fds[1] >= 0) close(pipe_fds[1]);
    pipe_fds[0] = -1;
    pipe_fds[1] = -1;
  };
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    last_error = strerror(errno);
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    last_error = strerror(errno);
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
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
#endif
  running = true;
  initialized = false;
  uses_utf8_positions = false;
  shutdown_complete = false;
  next_request_id = 1;
  initialize_request_id = 0;
  shutdown_request_id = 0;
  file_versions.clear();
  document_texts.clear();
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
  deferred_messages.clear();
  last_error.clear();

  set_non_blocking(stdin_fd);
  set_non_blocking(stdout_fd);
  set_non_blocking(stderr_fd);

  std::ostringstream init;
  initialize_request_id = next_request_id++;
  init << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << initialize_request_id << ","
       << "\"method\":\"initialize\","
       << "\"params\":{"
       << "\"processId\":" << current_process_id() << ","
       << "\"rootUri\":\"" << json_escape(to_file_uri(root_path)) << "\","
       << "\"rootPath\":\"" << json_escape(root_path) << "\","
       << "\"capabilities\":{"
       << "\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
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
  if (!send_message(init.str(), true)) {
    stop();
    return false;
  }

  append_log_line("INFO ", "Initializing " + describe());
  return true;
}

void LSPClient::stop() {
  if (!running && stdin_fd < 0 && stdout_fd < 0 && stderr_fd < 0 &&
      child_pid <= 0) {
    return;
  }

  if (running && stdin_fd >= 0 && initialized) {
    shutdown_complete = false;
    shutdown_request_id = next_request_id++;
    std::ostringstream shutdown;
    shutdown << "{\"jsonrpc\":\"2.0\",\"id\":" << shutdown_request_id
             << ",\"method\":\"shutdown\",\"params\":null}";
    if (send_message(shutdown.str(), true)) {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(200);
      while (!shutdown_complete && std::chrono::steady_clock::now() < deadline) {
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }
  if (running && stdin_fd >= 0) {
    send_message("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}",
                 true);
    flush_pending_writes();
  }
  running = false;
  initialized = false;

  if (child_pid > 0) {
#ifdef _WIN32
    if (child_process_handle) {
      if (WaitForSingleObject(reinterpret_cast<HANDLE>(child_process_handle), 200) ==
          WAIT_TIMEOUT) {
        TerminateProcess(reinterpret_cast<HANDLE>(child_process_handle), 1);
      }
      CloseHandle(reinterpret_cast<HANDLE>(child_process_handle));
      child_process_handle = nullptr;
    }
#else
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(200);
    while (waitpid(child_pid, &status, WNOHANG) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (waitpid(child_pid, &status, WNOHANG) == 0) {
      kill(child_pid, SIGTERM);
      const auto kill_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(200);
      while (waitpid(child_pid, &status, WNOHANG) == 0 &&
             std::chrono::steady_clock::now() < kill_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (waitpid(child_pid, &status, WNOHANG) == 0) {
        kill(child_pid, SIGKILL);
        waitpid(child_pid, &status, 0);
      }
    }
#endif
  }

  close_transport();
  child_pid = -1;
#ifdef _WIN32
  child_process_handle = nullptr;
#endif
  running = false;
  initialized = false;
  uses_utf8_positions = false;
  shutdown_complete = false;
  initialize_request_id = 0;
  shutdown_request_id = 0;
  file_versions.clear();
  document_texts.clear();
  pending_completion_requests.clear();
  pending_hover_requests.clear();
  pending_definition_requests.clear();
  pending_document_symbol_requests.clear();
  pending_completions.clear();
  pending_hovers.clear();
  pending_definitions.clear();
  pending_document_symbols.clear();
  outbound_buffer.clear();
  deferred_messages.clear();
}

void LSPClient::close_transport() {
  if (stdin_fd >= 0) close(stdin_fd);
  if (stdout_fd >= 0) close(stdout_fd);
  if (stderr_fd >= 0) close(stderr_fd);
  stdin_fd = -1;
  stdout_fd = -1;
  stderr_fd = -1;
}

bool LSPClient::restart() {
  stop();
  return start();
}

void LSPClient::handle_stdout_data(const std::string &data) {
  constexpr size_t kMaxLspHeaderBytes = 64 * 1024;
  constexpr size_t kMaxLspMessageBytes = 16 * 1024 * 1024;
  stdout_buffer += data;
  append_log_line("RECV ", data);

  if (stdout_buffer.size() > kMaxLspHeaderBytes + kMaxLspMessageBytes) {
    last_error = "LSP message exceeds size limit";
    stdout_buffer.clear();
    return;
  }

  while (true) {
    const size_t header_end = stdout_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      if (stdout_buffer.size() > kMaxLspHeaderBytes) {
        last_error = "LSP header exceeds size limit";
        stdout_buffer.clear();
      }
      return;
    }

    if (header_end > kMaxLspHeaderBytes) {
      last_error = "LSP header exceeds size limit";
      stdout_buffer.clear();
      return;
    }

    size_t content_length = 0;
    if (!extract_content_length(stdout_buffer.substr(0, header_end),
                                content_length)) {
      append_log_line("PARSE-ERR ", "Missing Content-Length header");
      stdout_buffer.erase(0, header_end + 4);
      continue;
    }
    if (content_length > kMaxLspMessageBytes) {
      last_error = "LSP message exceeds size limit";
      stdout_buffer.clear();
      return;
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

      const std::string filepath = from_file_uri(json_string_or_empty(uri));
      const JsonValue *version = params ? json_object_get(*params, "version") : nullptr;
      auto current_version = file_versions.find(fs::absolute(filepath).string());
      if (version && version->type == JsonValue::Number &&
          current_version != file_versions.end() &&
          current_version->second != (int)version->number_value) {
        continue;
      }
      auto parsed = diagnostics_from_json(*diagnostics);
      for (auto &diagnostic : parsed) {
        diagnostic.col = editor_character(filepath, diagnostic.line, diagnostic.col);
        diagnostic.end_col =
            editor_character(filepath, diagnostic.end_line, diagnostic.end_col);
      }
      pending_diagnostics.push_back({filepath, std::move(parsed)});
      continue;
    }

    const JsonValue *id = json_object_get(root, "id");
    if (!id || id->type != JsonValue::Number) {
      continue;
    }

    int request_id = (int)id->number_value;
    const JsonValue *result = json_object_get(root, "result");

    if (request_id == initialize_request_id) {
      if (!result || result->type != JsonValue::Object) {
        last_error = "LSP initialize request failed";
        stop();
        return;
      }
      const JsonValue *capabilities = json_object_get(*result, "capabilities");
      const JsonValue *encoding =
          capabilities ? json_object_get(*capabilities, "positionEncoding") : nullptr;
      uses_utf8_positions =
          encoding && encoding->type == JsonValue::String &&
          encoding->string_value == "utf-8";
      initialized = true;
      send_message("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}",
                   true);
      auto queued = std::move(deferred_messages);
      deferred_messages.clear();
      for (const auto &message : queued) {
        if (!send_message(message, true)) {
          break;
        }
      }
      append_log_line("INFO ", "Started " + describe());
      continue;
    }

    if (request_id == shutdown_request_id) {
      shutdown_complete = true;
      continue;
    }

    auto completion_it = pending_completion_requests.find(request_id);
    if (completion_it != pending_completion_requests.end()) {
      const auto current_version = file_versions.find(completion_it->second.filepath);
      if (current_version == file_versions.end() ||
          current_version->second != completion_it->second.version) {
        pending_completion_requests.erase(completion_it);
        continue;
      }
      if (result) {
        auto items = completion_items_from_json(*result);
        for (auto &item : items) {
          if (item.has_text_edit_range) {
            item.edit_start_char = editor_character(
                completion_it->second.filepath, item.edit_start_line, item.edit_start_char);
            item.edit_end_char = editor_character(
                completion_it->second.filepath, item.edit_end_line, item.edit_end_char);
          }
        }
        pending_completions.push_back(
            {completion_it->second.filepath, std::move(items)});
      } else {
        pending_completions.push_back({completion_it->second.filepath, {}});
      }
      pending_completion_requests.erase(completion_it);
      continue;
    }

    auto hover_it = pending_hover_requests.find(request_id);
    if (hover_it != pending_hover_requests.end()) {
      const auto current_version = file_versions.find(hover_it->second.filepath);
      if (current_version == file_versions.end() ||
          current_version->second != hover_it->second.version) {
        pending_hover_requests.erase(hover_it);
        continue;
      }
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
      const auto current_version = file_versions.find(definition_it->second.filepath);
      if (current_version == file_versions.end() ||
          current_version->second != definition_it->second.version) {
        pending_definition_requests.erase(definition_it);
        continue;
      }
      LSPDefinitionResult definition;
      definition.origin_filepath = definition_it->second.filepath;
      definition.origin_line = definition_it->second.line;
      definition.origin_character = definition_it->second.character;
      if (result) {
        definition.locations = definition_locations_from_result(*result);
        for (auto &location : definition.locations) {
          location.character =
              editor_character(location.filepath, location.line, location.character);
          location.end_character = editor_character(location.filepath, location.end_line,
                                                     location.end_character);
        }
      }
      pending_definitions.push_back(std::move(definition));
      pending_definition_requests.erase(definition_it);
      continue;
    }

    auto symbol_it = pending_document_symbol_requests.find(request_id);
    if (symbol_it != pending_document_symbol_requests.end()) {
      const auto current_version = file_versions.find(symbol_it->second.filepath);
      if (current_version == file_versions.end() ||
          current_version->second != symbol_it->second.version) {
        pending_document_symbol_requests.erase(symbol_it);
        continue;
      }
      LSPDocumentSymbolResult symbols;
      symbols.filepath = symbol_it->second.filepath;
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
  constexpr size_t kMaxLspStderrBytes = 64 * 1024;
  if (stderr_buffer.size() > kMaxLspStderrBytes) {
    stderr_buffer.erase(0, stderr_buffer.size() - kMaxLspStderrBytes);
  }
  append_log_line("STDERR ", data);
}

bool LSPClient::poll() {
  if (!running) {
    return false;
  }

  bool changed = false;
  if (!outbound_buffer.empty()) {
    if (!flush_pending_writes()) {
      return true;
    }
  }

  char buf[4096];
  size_t bytes_polled = 0;
  constexpr size_t kPollByteLimit = 256 * 1024;
  while (stdout_fd >= 0 && bytes_polled < kPollByteLimit) {
    if (!fd_has_data(stdout_fd)) {
      break;
    }
    ssize_t n = read(stdout_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stdout_data(std::string(buf, buf + n));
    bytes_polled += (size_t)n;
    changed = true;
  }
  while (stderr_fd >= 0 && bytes_polled < kPollByteLimit) {
    if (!fd_has_data(stderr_fd)) {
      break;
    }
    ssize_t n = read(stderr_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stderr_data(std::string(buf, buf + n));
    bytes_polled += (size_t)n;
  }

  int status = 0;
  if (child_pid > 0) {
#ifdef _WIN32
    HANDLE process = reinterpret_cast<HANDLE>(child_process_handle);
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      DWORD exit_code = 0;
      GetExitCodeProcess(process, &exit_code);
      running = false;
      initialized = false;
      last_error = "process exited with status " + std::to_string(exit_code);
      append_log_line("INFO ", last_error);
      CloseHandle(process);
      child_process_handle = nullptr;
      close_transport();
      child_pid = -1;
      changed = true;
    }
#else
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
      close_transport();
      child_pid = -1;
      changed = true;
    }
#endif
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
  if (file_versions.count(abs_path)) {
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

bool LSPClient::did_change(const std::string &filepath, const std::string &text) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.count(abs_path)) {
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
    if (!did_open(abs_path, language_id_for(language, abs_path), text)) {
      return false;
    }
  }
  document_texts[abs_path] = text;
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

bool LSPClient::did_close(const std::string &filepath) {
  if (!running) {
    return false;
  }

  const std::string abs_path = fs::absolute(filepath).string();
  if (!file_versions.erase(abs_path)) {
    return true;
  }
  document_texts.erase(abs_path);
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"textDocument/didClose\","
       << "\"params\":{\"textDocument\":{\"uri\":\""
       << json_escape(to_file_uri(abs_path)) << "\"}}"
       << "}";
  return send_message(json.str());
}

bool LSPClient::request_completion(const std::string &filepath, int line,
                                   int character, char trigger_character) {
  if (!running) {
    return false;
  }

  std::string abs_path = fs::absolute(filepath).string();
  if (pending_completion_requests.size() >= 64) {
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
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}";

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
  if (pending_hover_requests.size() >= 64) {
    last_error = "too many pending LSP hover requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_hover_requests[request_id] =
      PendingPositionRequest{abs_path, std::max(0, line), std::max(0, character),
                             file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/hover\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}"
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
  if (pending_definition_requests.size() >= 64) {
    last_error = "too many pending LSP definition requests";
    return false;
  }
  int request_id = next_request_id++;
  pending_definition_requests[request_id] =
      PendingPositionRequest{abs_path, std::max(0, line), std::max(0, character),
                             file_versions[abs_path]};

  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << request_id << ","
       << "\"method\":\"textDocument/definition\","
       << "\"params\":{"
       << "\"textDocument\":{\"uri\":\"" << json_escape(to_file_uri(abs_path))
       << "\"},"
       << "\"position\":{\"line\":" << std::max(0, line)
       << ",\"character\":" << lsp_character(abs_path, line, character) << "}"
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
  if (pending_document_symbol_requests.size() >= 64) {
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
