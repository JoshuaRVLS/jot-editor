#include "autoclose.h"

#include <algorithm>
#include <cctype>

namespace
{
  bool is_word_char(char c)
  {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_';
  }

  bool is_quote(char c)
  {
    return c == '"' || c == '\'';
  }

  // Whether an opener of this kind is still waiting for the closer at `pos`
  // somewhere to its left on the same line. Such a closer is already spoken for,
  // so a typed opener there has to bring a partner of its own.
  bool opener_unmatched_before(const std::string &line, int pos, char opener, char closer)
  {
    int depth = 0;
    for (int i = 0; i < pos; i++)
    {
      if (line[i] == opener)
        depth++;
      else if (line[i] == closer && depth > 0)
        depth--;
    }
    return depth > 0;
  }
} // namespace

bool AutoClose::should_auto_close(char c)
{
  return c == '(' || c == '{' || c == '[' || is_quote(c);
}

char AutoClose::get_closing_bracket(char c)
{
  switch (c)
  {
  case '(':
    return ')';
  case '{':
    return '}';
  case '[':
    return ']';
  case '"':
    return '"';
  case '\'':
    return '\'';
  default:
    return '\0';
  }
}

bool AutoClose::is_closing_bracket(char c)
{
  return c == ')' || c == '}' || c == ']' || is_quote(c);
}

bool AutoClose::is_escaped(const std::string &line, int pos)
{
  int backslashes = 0;
  for (int i = pos - 1; i >= 0 && line[i] == '\\'; i--)
    backslashes++;
  return backslashes % 2 == 1;
}

char AutoClose::open_quote_at(const std::string &line, int pos)
{
  char open = '\0';
  const int end = std::clamp(pos, 0, (int)line.size());
  for (int i = 0; i < end; i++)
  {
    const char c = line[i];
    if (c == '\\')
    {
      // The next character is escaped, so it is content: it can neither open a
      // string nor close the one already open.
      i++;
      continue;
    }
    if (open == '\0')
    {
      if (is_quote(c))
        open = c;
      continue;
    }
    if (c == open)
      open = '\0';
  }
  return open;
}

bool AutoClose::should_insert_pair(char c, const std::string &line, int pos)
{
  const char closing = get_closing_bracket(c);
  if (closing == '\0')
    return false;

  const int at = std::clamp(pos, 0, (int)line.size());
  const char prev = at > 0 ? line[at - 1] : '\0';
  const char next = at < (int)line.size() ? line[at] : '\0';

  // A backslash escapes what follows it, so the quote is literal text.
  if (is_escaped(line, at))
    return false;

  if (is_quote(c))
  {
    // Inside a string the caret is in that string's content, so the quote being
    // typed is text or the one that closes the string, never a pair opening:
    // that is what keeps `f"{x}"` and `print('a')` at one quote each, and what
    // stops the closer landing beside a bracket from dragging a partner in.
    if (open_quote_at(line, at) != '\0')
      return false;
    // Outside a string a quote beside a word is text rather than an opening
    // pair: it closes a string that was never closed (`s = "|foo`), or sits
    // inside a word (`don't`, `word"`). What is left stands on its own, which
    // is a pair being opened.
    if (is_word_char(next) || is_word_char(prev))
      return false;
    return true;
  }

  if (next != closing)
    return true;

  // The matching closer already sits under the caret. When an opener to its
  // left is still waiting on it, that closer is already that one's partner and
  // the typed bracket has to open a pair inside it -- `(|)` becomes `((|))`
  // rather than swallowing the closer. With nothing waiting on it the closer is
  // unclaimed, and taking it is what keeps the spare off the line.
  return opener_unmatched_before(line, at, c, closing);
}

bool AutoClose::should_skip_closing(char c, const std::string &line, int pos)
{
  if (!is_closing_bracket(c))
    return false;
  if (pos < 0 || pos >= (int)line.length())
    return false;
  if (line[pos] != c)
    return false;
  // An escaped quote is content, so it is not the closer to step over.
  return !(is_quote(c) && is_escaped(line, pos));
}
