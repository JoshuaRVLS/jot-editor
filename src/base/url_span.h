// Link detection for Ctrl+click (src/base/url_span.h).
//
// A text question with no editor state in it, kept apart from the mouse
// dispatcher so a unit test can feed it lines: a URL ends where the prose
// around it starts, and only explicit schemes count (`example.com` is a name in
// code as often as it is an address).

#ifndef URL_SPAN_H
#define URL_SPAN_H

#include <cctype>
#include <cstring>
#include <string>

namespace url_span
{
  // Case-insensitive compare of `literal` against the line at `at` (schemes are
  // case-insensitive, and `HTTPS://` in a comment is a link all the same).
  inline bool matches_at(const std::string &line, size_t at, const char *literal)
  {
    for (size_t i = 0; literal[i] != '\0'; i++)
    {
      if (at + i >= line.size())
        return false;
      const unsigned char a = (unsigned char)line[at + i];
      const unsigned char b = (unsigned char)literal[i];
      if (std::tolower(a) != std::tolower(b))
        return false;
    }
    return true;
  }

  // A scheme starts a link only at a word boundary: `xhttps://` is a name, not
  // a scheme (bytes >= 0x80 count as word bytes too).
  inline bool at_word_boundary(const std::string &line, size_t i)
  {
    if (i == 0)
      return true;
    const unsigned char prev = (unsigned char)line[i - 1];
    return !(std::isalnum(prev) || prev == '_' || prev >= 0x80);
  }

  // What may sit inside a link: everything printable except the delimiters that
  // end one in running text -- whitespace, quotes, angle brackets (markdown
  // autolinks), backticks and the table pipe. Brackets are kept because real
  // URLs carry them; the unbalanced tails are trimmed below.
  inline bool is_link_char(unsigned char c)
  {
    if (std::isspace(c))
      return false;
    switch (c)
    {
    case '<':
    case '>':
    case '"':
    case '\'':
    case '`':
    case '|':
      return false;
    default:
      return true;
    }
  }

  // Sentence punctuation after a link belongs to the sentence, not the link:
  // `see https://example.com.` must not ask for `example.com.`.
  inline bool is_prose_tail(unsigned char c)
  {
    switch (c)
    {
    case '.':
    case ',':
    case ':':
    case ';':
    case '!':
    case '?':
      return true;
    default:
      return false;
    }
  }

  // Trims what the prose added to the right end of [start, end): punctuation,
  // and any closing bracket the link does not open inside itself. The counts
  // are re-taken after every trim, so `(see https://example.com).` loses both
  // the dot and the paren, while a Wikipedia URL keeps the `)` it opened.
  inline void trim_link_tail(const std::string &line, int start, int &end)
  {
    while (end > start)
    {
      const char tail = line[(size_t)end - 1];
      if (is_prose_tail((unsigned char)tail))
      {
        end--;
        continue;
      }
      char opener = 0;
      if (tail == ')')
        opener = '(';
      else if (tail == ']')
        opener = '[';
      else if (tail == '}')
        opener = '{';
      if (opener == 0)
        return;
      int opened = 0;
      int closed = 0;
      for (int i = start; i < end - 1; i++) // the tail itself excluded
      {
        if (line[(size_t)i] == opener)
          opened++;
        else if (line[(size_t)i] == tail)
          closed++;
      }
      if (opened > closed)
        return; // an opener inside the link matches this closer
      end--;
    }
  }

  // The link containing byte column `col` of the logical line, as a half-open
  // [start, end). Longest match wins: a pointer inside a link answers for the
  // whole link, not for a piece of one nested inside another.
  inline bool find(const std::string &line, int col, int &start, int &end)
  {
    if (col < 0 || col >= (int)line.size())
      return false;
    static const char *const kSchemes[] = {"https://", "http://", "mailto:", "file://"};
    for (int i = 0; i < (int)line.size(); i++)
    {
      if (!at_word_boundary(line, (size_t)i))
        continue;
      int scheme_len = 0;
      for (const char *scheme : kSchemes)
      {
        if (matches_at(line, (size_t)i, scheme))
        {
          scheme_len = (int)std::strlen(scheme);
          break;
        }
      }
      if (scheme_len == 0)
        continue;
      int link_end = i + scheme_len;
      while (link_end < (int)line.size() && is_link_char((unsigned char)line[(size_t)link_end]))
        link_end++;
      trim_link_tail(line, i, link_end);
      if (link_end <= i + scheme_len)
        continue; // a bare `http://` names nothing to open
      if (col >= i && col < link_end)
      {
        start = i;
        end = link_end;
        return true;
      }
      i = link_end - 1; // no link starts inside a run the scan already took
    }
    return false;
  }
} // namespace url_span

#endif // URL_SPAN_H
