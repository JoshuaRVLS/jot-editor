// String helpers (src/tools/string_util.h).
//
// Header-only, dependency-free helpers for the small string transforms that
// used to be copy-pasted across the codebase: case folding (lower_copy),
// whitespace trimming (trim_copy / ltrim_copy), and first-line extraction
// (first_line_copy). Trimming is based on std::isspace so trailing \r from
// CRLF files and \n from line-oriented parsing are handled uniformly. This
// is the single source of truth — workspace code, tools, input commands,
// and the Lua bridge all include it instead of keeping private copies.

#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <cctype>
#include <string>

namespace string_util
{
  // Returns a lowercase copy of `s` (ASCII-aware, via std::tolower).
  inline std::string lower_copy(std::string s)
  {
    for (char &c : s)
    {
      c = (char)std::tolower((unsigned char)c);
    }
    return s;
  }

  // Returns a copy of `s` with leading and trailing whitespace removed.
  inline std::string trim_copy(const std::string &s)
  {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a]))
    {
      ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1]))
    {
      --b;
    }
    return s.substr(a, b - a);
  }

  // Returns a copy of `s` with leading whitespace removed.
  inline std::string ltrim_copy(const std::string &s)
  {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i]))
    {
      ++i;
    }
    return s.substr(i);
  }

  // Returns the first line of `text` (everything up to the first \r or \n,
  // or the whole string when there is no line break).
  inline std::string first_line_copy(const std::string &text)
  {
    size_t end = text.find_first_of("\r\n");
    if (end == std::string::npos)
    {
      return text;
    }
    return text.substr(0, end);
  }

  // True when `s` ends with `suffix`.
  inline bool ends_with(const std::string &s, const std::string &suffix)
  {
    return s.size() >= suffix.size()
           && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  // True when `value` starts with `prefix`.
  inline bool starts_with(const std::string &value, const std::string &prefix)
  {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
  }

  // True when `value` starts with `prefix`, ignoring ASCII case.
  inline bool starts_with_icase(const std::string &value, const std::string &prefix)
  {
    if (prefix.size() > value.size())
    {
      return false;
    }
    for (size_t i = 0; i < prefix.size(); i++)
    {
      if (std::tolower((unsigned char)value[i]) != std::tolower((unsigned char)prefix[i]))
      {
        return false;
      }
    }
    return true;
  }
} // namespace string_util

#endif // STRING_UTIL_H