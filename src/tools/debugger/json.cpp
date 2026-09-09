// Minimal JSON parser used by the DAP client: tokenizes the wire format into
// Dap::Value trees, plus the Dap namespace helpers (escaping, base64, header
// parsing) declared in client.h.
#include "tools/debugger/client.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string.h>

namespace
{
void skip_ws(const std::string &text, size_t &pos)
{
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
  {
    pos++;
  }
}
bool parse_json_string(const std::string &text, size_t &pos, std::string &out)
{
  if (pos >= text.size() || text[pos] != '"')
  {
    return false;
  }
  pos++;
  out.clear();
  while (pos < text.size())
  {
    char c = text[pos++];
    if (c == '"')
    {
      return true;
    }
    if (c == '\\')
    {
      if (pos >= text.size())
      {
        return false;
      }
      char esc = text[pos++];
      switch (esc)
      {
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
      case 'u':
      {
        if (pos + 4 > text.size())
        {
          return false;
        }
        unsigned int codepoint = 0;
        for (int i = 0; i < 4; i++)
        {
          char h = text[pos++];
          codepoint <<= 4;
          if (h >= '0' && h <= '9')
          {
            codepoint |= (unsigned int)(h - '0');
          }
          else if (h >= 'a' && h <= 'f')
          {
            codepoint |= (unsigned int)(10 + h - 'a');
          }
          else if (h >= 'A' && h <= 'F')
          {
            codepoint |= (unsigned int)(10 + h - 'A');
          }
          else
          {
            return false;
          }
        }
        if (codepoint <= 0x7F)
        {
          out.push_back((char)codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
          out.push_back((char)(0xC0 | ((codepoint >> 6) & 0x1F)));
          out.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
        else
        {
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
bool parse_json_value(const std::string &text, size_t &pos, Dap::Value &out);

bool parse_json_array(const std::string &text, size_t &pos, Dap::Value &out)
{
  if (pos >= text.size() || text[pos] != '[')
  {
    return false;
  }
  pos++;
  out = Dap::Value{};
  out.type = Dap::Value::Array;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == ']')
  {
    pos++;
    return true;
  }
  while (pos < text.size())
  {
    Dap::Value item;
    if (!parse_json_value(text, pos, item))
    {
      return false;
    }
    out.array_value.push_back(std::move(item));
    skip_ws(text, pos);
    if (pos >= text.size())
    {
      return false;
    }
    if (text[pos] == ']')
    {
      pos++;
      return true;
    }
    if (text[pos] != ',')
    {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}
bool parse_json_object(const std::string &text, size_t &pos, Dap::Value &out)
{
  if (pos >= text.size() || text[pos] != '{')
  {
    return false;
  }
  pos++;
  out = Dap::Value{};
  out.type = Dap::Value::Object;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == '}')
  {
    pos++;
    return true;
  }
  while (pos < text.size())
  {
    std::string key;
    if (!parse_json_string(text, pos, key))
    {
      return false;
    }
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != ':')
    {
      return false;
    }
    pos++;
    Dap::Value value;
    if (!parse_json_value(text, pos, value))
    {
      return false;
    }
    out.object_value[key] = std::move(value);
    skip_ws(text, pos);
    if (pos >= text.size())
    {
      return false;
    }
    if (text[pos] == '}')
    {
      pos++;
      return true;
    }
    if (text[pos] != ',')
    {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}
bool parse_json_number(const std::string &text, size_t &pos, Dap::Value &out)
{
  size_t start = pos;
  if (pos < text.size() && text[pos] == '-')
  {
    pos++;
  }
  while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
  {
    pos++;
  }
  if (start == pos || (start + 1 == pos && text[start] == '-'))
  {
    return false;
  }
  out = Dap::Value{};
  out.type = Dap::Value::Number;
  out.number_value = std::strtoll(text.substr(start, pos - start).c_str(), nullptr, 10);
  while (pos < text.size()
         && (std::isdigit((unsigned char)text[pos]) || text[pos] == '.' || text[pos] == 'e'
             || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-'))
  {
    pos++;
  }
  return true;
}
bool parse_json_value(const std::string &text, size_t &pos, Dap::Value &out)
{
  skip_ws(text, pos);
  if (pos >= text.size())
  {
    return false;
  }
  char c = text[pos];
  if (c == '"')
  {
    out = Dap::Value{};
    out.type = Dap::Value::String;
    return parse_json_string(text, pos, out.string_value);
  }
  if (c == '{')
  {
    return parse_json_object(text, pos, out);
  }
  if (c == '[')
  {
    return parse_json_array(text, pos, out);
  }
  if (c == '-' || std::isdigit((unsigned char)c))
  {
    return parse_json_number(text, pos, out);
  }
  if (text.compare(pos, 4, "null") == 0)
  {
    out = Dap::Value{};
    out.type = Dap::Value::Null;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 4, "true") == 0)
  {
    out = Dap::Value{};
    out.type = Dap::Value::Bool;
    out.bool_value = true;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 5, "false") == 0)
  {
    out = Dap::Value{};
    out.type = Dap::Value::Bool;
    out.bool_value = false;
    pos += 5;
    return true;
  }
  return false;
}
} // namespace

namespace Dap
{
  bool parse_json(const std::string &text, Value &out)
  {
    size_t pos = 0;
    if (!parse_json_value(text, pos, out))
    {
      return false;
    }
    skip_ws(text, pos);
    return pos == text.size();
  }

  std::string json_escape(const std::string &value)
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
        out.push_back((char)c);
        break;
      }
    }
    return out;
  }

  const Value *object_get(const Value &value, const std::string &key)
  {
    if (value.type != Value::Object)
    {
      return nullptr;
    }
    auto it = value.object_value.find(key);
    return it == value.object_value.end() ? nullptr : &it->second;
  }

  std::string string_or_empty(const Value *value)
  {
    return value && value->type == Value::String ? value->string_value : "";
  }

  int int_or_default(const Value *value, int fallback)
  {
    return value && value->type == Value::Number ? (int)value->number_value : fallback;
  }

  bool bool_or_default(const Value *value, bool fallback)
  {
    return value && value->type == Value::Bool ? value->bool_value : fallback;
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
      if (line.rfind(prefix, 0) != 0)
      {
        continue;
      }
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
    return false;
  }

  bool decode_base64(const std::string &text, std::string &out)
  {
    static const char *const alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    decoded.reserve(text.size() * 3 / 4);
    unsigned int accumulator = 0;
    int bits = 0;
    for (char c : text)
    {
      if (c == '=')
      {
        break; // padding terminates the stream; trailing junk ignored
      }
      if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
      {
        continue;
      }
      const char *pos = strchr(alphabet, c);
      if (!pos)
      {
        return false;
      }
      accumulator = (accumulator << 6) | (unsigned int)(pos - alphabet);
      bits += 6;
      if (bits >= 8)
      {
        bits -= 8;
        decoded.push_back((char)((accumulator >> bits) & 0xFF));
      }
    }
    out = std::move(decoded);
    return true;
  }

  bool looks_like_address(const std::string &text)
  {
    if (text.empty())
    {
      return false;
    }
    size_t pos = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
      pos = 2;
      if (pos >= text.size())
      {
        return false;
      }
      for (; pos < text.size(); pos++)
      {
        if (!std::isxdigit((unsigned char)text[pos]))
        {
          return false;
        }
      }
      return true;
    }
    for (; pos < text.size(); pos++)
    {
      if (!std::isdigit((unsigned char)text[pos]))
      {
        return false;
      }
    }
    return true;
  }
} // namespace Dap
