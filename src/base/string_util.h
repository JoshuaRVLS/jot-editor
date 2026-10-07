// Shared text helpers. The *_view forms point into the caller's buffer and
// allocate nothing, so the caller keeps it alive.

#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace string_util
{
  [[nodiscard]] inline bool is_space(char c)
  {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  }

  [[nodiscard]] inline std::string_view ltrim_view(std::string_view s)
  {
    size_t i = 0;
    while (i < s.size() && is_space(s[i]))
    {
      ++i;
    }
    return s.substr(i);
  }

  [[nodiscard]] inline std::string_view rtrim_view(std::string_view s)
  {
    size_t n = s.size();
    while (n > 0 && is_space(s[n - 1]))
    {
      --n;
    }
    return s.substr(0, n);
  }

  [[nodiscard]] inline std::string_view trim_view(std::string_view s)
  {
    return rtrim_view(ltrim_view(s));
  }

  [[nodiscard]] inline std::string trim_copy(std::string_view s)
  {
    return std::string(trim_view(s));
  }

  [[nodiscard]] inline std::string ltrim_copy(std::string_view s)
  {
    return std::string(ltrim_view(s));
  }

  inline void to_lower(std::string &s)
  {
    for (char &c : s)
    {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }

  [[nodiscard]] inline std::string lower_copy(std::string s)
  {
    to_lower(s);
    return s;
  }

  [[nodiscard]] inline std::string_view first_line_view(std::string_view text)
  {
    const size_t end = text.find_first_of("\r\n");
    return end == std::string_view::npos ? text : text.substr(0, end);
  }

  [[nodiscard]] inline std::string first_line_copy(std::string_view text)
  {
    return std::string(first_line_view(text));
  }

  [[nodiscard]] inline bool starts_with(std::string_view value, std::string_view prefix)
  {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
  }

  [[nodiscard]] inline bool ends_with(std::string_view value, std::string_view suffix)
  {
    return value.size() >= suffix.size()
           && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  [[nodiscard]] inline bool starts_with_icase(std::string_view value, std::string_view prefix)
  {
    if (prefix.size() > value.size())
    {
      return false;
    }
    for (size_t i = 0; i < prefix.size(); i++)
    {
      if (std::tolower(static_cast<unsigned char>(value[i]))
          != std::tolower(static_cast<unsigned char>(prefix[i])))
      {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] inline bool contains(std::string_view haystack, std::string_view needle)
  {
    return haystack.find(needle) != std::string_view::npos;
  }

  // Decimal int, nothing when there is no number to read. Unlike std::stoi this
  // reports the failure instead of throwing.
  [[nodiscard]] inline std::optional<int> to_int(std::string_view text)
  {
    std::string_view digits = trim_view(text);
    if (digits.empty())
    {
      return std::nullopt;
    }
    int value = 0;
    const char *end = digits.data() + digits.size();
    const std::from_chars_result result = std::from_chars(digits.data(), end, value);
    if (result.ec != std::errc() || result.ptr == digits.data())
    {
      return std::nullopt;
    }
    return value;
  }

  // Every field between the delimiters, empty ones included, so joining the
  // result puts the text back together.
  [[nodiscard]] inline std::vector<std::string_view> split(std::string_view text, char delimiter)
  {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (true)
    {
      const size_t at = text.find(delimiter, start);
      if (at == std::string_view::npos)
      {
        out.push_back(text.substr(start));
        break;
      }
      out.push_back(text.substr(start, at - start));
      start = at + 1;
    }
    return out;
  }

  // The fields of `text`, trimmed, with the empty ones dropped.
  [[nodiscard]] inline std::vector<std::string_view> fields(std::string_view text, char delimiter)
  {
    std::vector<std::string_view> out;
    for (std::string_view field : split(text, delimiter))
    {
      const std::string_view trimmed = trim_view(field);
      if (!trimmed.empty())
      {
        out.push_back(trimmed);
      }
    }
    return out;
  }

  template <typename Range>
  [[nodiscard]] inline std::string join(const Range &parts, std::string_view separator)
  {
    std::string out;
    bool first = true;
    for (const auto &part : parts)
    {
      if (!first)
      {
        out += separator;
      }
      first = false;
      out += std::string_view(part);
    }
    return out;
  }

  // A trailing newline closes the line it ends instead of opening an empty one,
  // and no newline at all still leaves one line -- the count std::getline gives.
  [[nodiscard]] inline size_t line_count(std::string_view text)
  {
    if (text.empty())
    {
      return 0;
    }
    const size_t breaks = (size_t)std::count(text.begin(), text.end(), '\n');
    return breaks + (text.back() == '\n' ? 0 : 1);
  }

  // Every line of `text`, without its break, and without a '\r' a CRLF left.
  [[nodiscard]] inline std::vector<std::string_view> lines(std::string_view text)
  {
    std::vector<std::string_view> out;
    out.reserve(line_count(text));
    size_t pos = 0;
    while (pos < text.size())
    {
      const size_t nl = text.find('\n', pos);
      std::string_view line =
          nl == std::string_view::npos ? text.substr(pos) : text.substr(pos, nl - pos);
      if (!line.empty() && line.back() == '\r')
      {
        line.remove_suffix(1);
      }
      out.push_back(line);
      if (nl == std::string_view::npos)
      {
        break;
      }
      pos = nl + 1;
    }
    return out;
  }

  // The `count` lines that end `skip` lines before the end. A short text gives
  // back fewer; nothing is copied, so only the window costs anything.
  [[nodiscard]] inline std::vector<std::string_view>
  tail_lines(std::string_view text, size_t count, size_t skip = 0)
  {
    std::vector<std::string_view> out;
    const size_t total = line_count(text);
    if (count == 0 || total <= skip)
    {
      return out;
    }
    const size_t take = std::min(count, total - skip);
    const size_t first = total - skip - take;
    out.reserve(take);

    size_t pos = 0;
    for (size_t index = 0; index < total; index++)
    {
      const size_t nl = text.find('\n', pos);
      const size_t end = nl == std::string_view::npos ? text.size() : nl;
      if (index >= first)
      {
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
        {
          line.remove_suffix(1);
        }
        out.push_back(line);
        if (out.size() == take)
        {
          break;
        }
      }
      if (nl == std::string_view::npos)
      {
        break;
      }
      pos = nl + 1;
    }
    return out;
  }

  // The opening `max_lines` lines, then a "..." line when the text ran on.
  [[nodiscard]] inline std::string limit_lines(std::string_view text, int max_lines)
  {
    std::string out;
    if (max_lines <= 0)
    {
      return out;
    }
    size_t pos = 0;
    for (int taken = 0; taken < max_lines && pos < text.size(); taken++)
    {
      const size_t nl = text.find('\n', pos);
      std::string_view line =
          nl == std::string_view::npos ? text.substr(pos) : text.substr(pos, nl - pos);
      if (!line.empty() && line.back() == '\r')
      {
        line.remove_suffix(1);
      }
      if (taken > 0)
      {
        out += '\n';
      }
      out += line;
      if (nl == std::string_view::npos)
      {
        return out;
      }
      pos = nl + 1;
    }
    if (pos < text.size())
    {
      out += "\n...";
    }
    return out;
  }

  // The tab-separated session files: \t, \n and \\ have to survive inside a
  // field, so they are written escaped.
  [[nodiscard]] inline std::string escape_field(std::string_view input)
  {
    std::string out;
    out.reserve(input.size());
    for (char c : input)
    {
      if (c == '\\')
      {
        out += "\\\\";
      }
      else if (c == '\t')
      {
        out += "\\t";
      }
      else if (c == '\n')
      {
        out += "\\n";
      }
      else
      {
        out.push_back(c);
      }
    }
    return out;
  }

  [[nodiscard]] inline std::string unescape_field(std::string_view input)
  {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++)
    {
      if (input[i] == '\\' && i + 1 < input.size())
      {
        const char next = input[i + 1];
        if (next == 't')
        {
          out.push_back('\t');
          i++;
          continue;
        }
        if (next == 'n')
        {
          out.push_back('\n');
          i++;
          continue;
        }
        if (next == '\\')
        {
          out.push_back('\\');
          i++;
          continue;
        }
      }
      out.push_back(input[i]);
    }
    return out;
  }
} // namespace string_util

#endif // STRING_UTIL_H
