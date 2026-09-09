// Minimal JSON parser for LSP messages (jsonrpc payloads). Owns the
// JsonValue tree type and the recursive-descent parse; result parsers
// live in protocol.cpp.
#include "tools/lsp/internal.h"
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lsp_detail
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

  bool parse_json_array(const std::string &text, size_t &pos, JsonValue &out)
  {
    if (pos >= text.size() || text[pos] != '[')
    {
      return false;
    }
    pos++;
    out = JsonValue{};
    out.type = JsonValue::Array;
    skip_ws(text, pos);
    if (pos < text.size() && text[pos] == ']')
    {
      pos++;
      return true;
    }
    while (pos < text.size())
    {
      JsonValue item;
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

  bool parse_json_object(const std::string &text, size_t &pos, JsonValue &out)
  {
    if (pos >= text.size() || text[pos] != '{')
    {
      return false;
    }
    pos++;
    out = JsonValue{};
    out.type = JsonValue::Object;
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
      JsonValue value;
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

  bool parse_json_number(const std::string &text, size_t &pos, JsonValue &out)
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
    out = JsonValue{};
    out.type = JsonValue::Number;
    out.number_value = std::strtoll(text.substr(start, pos - start).c_str(), nullptr, 10);
    if (pos < text.size() && (text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E'))
    {
      while (pos < text.size()
             && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.'
                 || text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-'))
      {
        pos++;
      }
    }
    return true;
  }

  bool parse_json_value(const std::string &text, size_t &pos, JsonValue &out)
  {
    skip_ws(text, pos);
    if (pos >= text.size())
    {
      return false;
    }

    char c = text[pos];
    if (c == '"')
    {
      out = JsonValue{};
      out.type = JsonValue::String;
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
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
    {
      return parse_json_number(text, pos, out);
    }
    if (text.compare(pos, 4, "null") == 0)
    {
      out = JsonValue{};
      out.type = JsonValue::Null;
      pos += 4;
      return true;
    }
    if (text.compare(pos, 4, "true") == 0)
    {
      out = JsonValue{};
      out.type = JsonValue::Bool;
      out.bool_value = true;
      pos += 4;
      return true;
    }
    if (text.compare(pos, 5, "false") == 0)
    {
      out = JsonValue{};
      out.type = JsonValue::Bool;
      out.bool_value = false;
      pos += 5;
      return true;
    }
    return false;
  }

  const JsonValue *json_object_get(const JsonValue &value, const std::string &key)
  {
    if (value.type != JsonValue::Object)
    {
      return nullptr;
    }
    auto it = value.object_value.find(key);
    if (it == value.object_value.end())
    {
      return nullptr;
    }
    return &it->second;
  }

  std::string json_string_or_empty(const JsonValue *value)
  {
    if (!value || value->type != JsonValue::String)
    {
      return "";
    }
    return value->string_value;
  }

  int json_int_or_default(const JsonValue *value, int fallback)
  {
    if (!value || value->type != JsonValue::Number)
    {
      return fallback;
    }
    return (int)value->number_value;
  }

} // namespace lsp_detail
