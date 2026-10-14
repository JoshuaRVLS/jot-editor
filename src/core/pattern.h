// Glob patterns for event filters: the `pattern` option on jot.autocmd and
// jot.events.subscribe. One matcher serves both surfaces, so "which files does
// this hook want" answers the same way wherever it is asked, and the rules live
// in one place instead of being spelled out again per event source.
//
// `*` stands for any run of characters except a path separator, `**` for any run
// at all, `?` for one character that is not a separator, `[...]` for a set (with
// `-` ranges and a leading `!` or `^` to invert), and `\` escapes the next
// character. This is the dialect file globs are written in, which is the one a
// hook's pattern reads like; it is deliberately not a Lua pattern, whose `%`
// classes and quantifiers would mean a hook's `*.py` matches nothing at all if
// it were read as one.
//
// Matching is case-sensitive and whole-string: `*.txt` does not match
// "notes.txt.bak", because the pattern has to account for the entire path.

#ifndef PATTERN_H
#define PATTERN_H

#include <string>
#include <vector>

namespace jot_pattern
{
  // One pattern element: a literal (or `?`, an escape, a set), or a star run.
  // begin/end are the pattern positions the element spans, so the matcher can
  // read it without the pattern being rewritten into anything else.
  struct GlobToken
  {
    size_t begin = 0;
    size_t end = 0;
    bool star = false;
    // A star run of two or more, which also crosses path separators.
    bool deep = false;
  };

  // The position just past a bracket expression starting at `at` ('['). A ']'
  // in the first position is a member of the set, not the end of it, which is
  // how a set that contains a ']' is written. With no closing bracket at all
  // the '[' is a literal, and the answer is the position after it.
  [[nodiscard]] inline size_t class_end(const std::string &pattern, size_t at)
  {
    size_t i = at + 1;
    bool first = true;
    while (i < pattern.size())
    {
      if (pattern[i] == ']' && !first)
      {
        return i + 1;
      }
      first = false;
      if (pattern[i] == '\\' && i + 1 < pattern.size())
      {
        i++;
      }
      i++;
    }
    return at + 1;
  }

  // A bracket expression spanning [begin, end) against one character. An
  // unclosed '[' (a one-character span) matches only a literal '['.
  [[nodiscard]] inline bool class_hit(const std::string &pattern,
                                      size_t begin,
                                      size_t end,
                                      unsigned char c)
  {
    if (end <= begin + 1 || pattern[end - 1] != ']')
    {
      return c == '[';
    }
    size_t i = begin + 1;
    bool negate = false;
    if (pattern[i] == '!' || pattern[i] == '^')
    {
      negate = true;
      i++;
    }
    bool hit = false;
    while (i + 1 < end)
    {
      unsigned char low = (unsigned char)pattern[i];
      if (low == '\\' && i + 1 < end - 1)
      {
        low = (unsigned char)pattern[++i];
      }
      if (i + 2 < end && pattern[i + 1] == '-' && pattern[i + 2] != ']')
      {
        const unsigned char high = (unsigned char)pattern[i + 2];
        if (c >= low && c <= high)
        {
          hit = true;
        }
        i += 3;
      }
      else
      {
        if (c == low)
        {
          hit = true;
        }
        i++;
      }
    }
    return negate ? !hit : hit;
  }

  // One non-star element against one character.
  [[nodiscard]] inline bool token_matches(const std::string &pattern,
                                          const GlobToken &token,
                                          unsigned char c)
  {
    const unsigned char pc = (unsigned char)pattern[token.begin];
    if (pc == '?')
    {
      return c != '/';
    }
    if (pc == '\\' && token.begin + 1 < token.end)
    {
      return c == (unsigned char)pattern[token.begin + 1];
    }
    if (pc == '[')
    {
      return class_hit(pattern, token.begin, token.end, c);
    }
    return c == pc;
  }

  [[nodiscard]] inline std::vector<GlobToken> glob_tokens(const std::string &pattern)
  {
    std::vector<GlobToken> out;
    for (size_t i = 0; i < pattern.size();)
    {
      GlobToken token;
      token.begin = i;
      if (pattern[i] == '*')
      {
        while (i < pattern.size() && pattern[i] == '*')
        {
          i++;
        }
        token.star = true;
        token.deep = i - token.begin >= 2;
      }
      else if (pattern[i] == '[')
      {
        i = class_end(pattern, i);
      }
      else if (pattern[i] == '\\' && i + 1 < pattern.size())
      {
        i += 2;
      }
      else
      {
        i++;
      }
      token.end = i;
      out.push_back(token);
    }
    return out;
  }

  // The whole pattern against the whole text.
  //
  // The walk is a table read from the right: one row per pattern element, one
  // column per text position, and only two rows are ever live, because the
  // element after this one is the row just built. That is what makes every
  // star's choice revisitable. A single backtracking star is not enough here:
  // with "src/**/*.c" against "src/a/b/main.c" the second star runs into a
  // separator and has to give up, and the answer only comes from the `**`
  // before it taking one more character.
  [[nodiscard]] inline bool glob_match(const std::string &pattern, const std::string &text)
  {
    const std::vector<GlobToken> tokens = glob_tokens(pattern);
    const size_t len = text.size();
    // next[t]: the elements after this one match the text from t on.
    std::vector<char> next(len + 1, 0);
    std::vector<char> cur(len + 1, 0);
    next[len] = 1; // nothing left to match, and nothing left of the text
    for (size_t index = tokens.size(); index-- > 0;)
    {
      const GlobToken &token = tokens[index];
      cur[len] = token.star ? next[len] : 0;
      for (size_t t = len; t-- > 0;)
      {
        if (token.star)
        {
          // Taking nothing (as long as the rest matches here), or taking this
          // character and repeating. A separator is only offered to a star run
          // of two or more.
          const bool may_take = (token.deep || text[t] != '/') && cur[t + 1];
          cur[t] = (next[t] || may_take) ? 1 : 0;
        }
        else
        {
          cur[t] = (next[t + 1] && token_matches(pattern, token, (unsigned char)text[t])) ? 1 : 0;
        }
      }
      next.swap(cur);
    }
    return next[0] != 0;
  }

  // Whether a pattern filters anything at all. An empty pattern and a lone "*"
  // both mean "every file", which is what leaving the option out does, so a hook
  // or a subscriber that carries either takes everything.
  [[nodiscard]] inline bool is_filter(const std::string &pattern)
  {
    return !pattern.empty() && pattern != "*";
  }

  // A hook's pattern against the path of the event it is being offered. A pattern
  // with no separator in it is also tried against the last component, which is
  // what makes "*.py" do what it looks like it should for /home/me/run.py
  // instead of matching nothing outside the working directory. A pattern that
  // names a separator is about the whole path; "**/proj/*.py" is how one of
  // those is anchored anywhere.
  [[nodiscard]] inline bool path_match(const std::string &pattern, const std::string &path)
  {
    if (!is_filter(pattern))
    {
      return true;
    }
    if (glob_match(pattern, path))
    {
      return true;
    }
    if (pattern.find('/') != std::string::npos)
    {
      return false;
    }
    const size_t slash = path.find_last_of('/');
    return glob_match(pattern, slash == std::string::npos ? path : path.substr(slash + 1));
  }
} // namespace jot_pattern

#endif // PATTERN_H
