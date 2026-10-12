#include "compile_parse.h"

#include <cctype>
#include <cstring>

namespace
{
  std::string trimmed(const std::string &s)
  {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
    {
      start++;
    }
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
    {
      end--;
    }
    return s.substr(start, end - start);
  }

  std::string lowered(const std::string &s)
  {
    std::string out = s;
    for (char &c : out)
    {
      c = (char)std::tolower((unsigned char)c);
    }
    return out;
  }

  bool ends_with_source_extension(const std::string &path)
  {
    // Spelled out rather than "any extension": a path with a space in it is
    // only taken as a path when its tail names a source file, which is what
    // keeps make's `[Makefile:3: all]` out of the list.
    static const char *const kExtensions[] = {
        ".c",   ".cc",  ".cpp", ".cxx", ".h",   ".hh",  ".hpp", ".hxx", ".m",   ".mm",
        ".rs",  ".py",  ".pyi", ".java", ".kt",  ".kts", ".go",  ".ts",  ".tsx", ".js",
        ".jsx", ".mjs", ".cjs", ".cs",  ".swift", ".zig", ".hs",  ".lhs", ".ml",  ".mli",
        ".lua", ".rb",  ".php", ".pl",  ".sh",  ".bash", ".s",   ".S",   ".asm", ".f90",
        ".jl",  ".dart", ".scala", ".clj", ".ex", ".exs", ".cu",  ".glsl", ".hlsl", ".sql",
        ".r",   ".R",   ".vb",  ".fs",  ".vhd", ".v",   ".nim", ".cr",  ".d",   ".groovy"};
    for (const char *ext : kExtensions)
    {
      const size_t len = std::strlen(ext);
      if (path.size() > len && path.compare(path.size() - len, len, ext) == 0)
      {
        return true;
      }
    }
    return false;
  }

  // Whether the text in front of a location can be the file it belongs to.
  // Context lines carry a location-looking run of digits too -- GCC's include
  // chain prints `In file included from x.cpp:3:0,` -- and those name no
  // problem, so a word in front of the path disqualifies it.
  bool looks_like_path(const std::string &prefix)
  {
    if (prefix.empty())
    {
      return false;
    }
    static const char *const kContextWords[] = {"from ", "at ", "in ", "In ", "on "};
    for (const char *word : kContextWords)
    {
      if (prefix.rfind(word, 0) == 0)
      {
        return false;
      }
    }
    if (prefix.find_first_of(" \t") == std::string::npos)
    {
      return true;
    }
    return ends_with_source_extension(prefix);
  }

  // Reads `line[:col]` at `pos` (which must sit on a digit) and reports where
  // the numbers end, without the separator. A second number is only a column
  // when it is itself followed by the separator: `a.c:12: 5 files` is a line
  // and a message, not a line and column five. `end_of_line_ok` is for the
  // tools that stop at the numbers instead of continuing with a message --
  // rustc's `--> src/main.rs:12:5`.
  bool read_location(
      const std::string &s, size_t pos, int &line, int &col, size_t &end, bool end_of_line_ok = false)
  {
    size_t i = pos;
    long long value = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i]))
    {
      value = value * 10 + (s[i] - '0');
      if (value > 10000000)
      {
        return false;
      }
      i++;
    }
    if (i == pos || i >= s.size() || s[i] != ':')
    {
      return false;
    }
    line = (int)value;
    col = 0;
    const size_t after = i + 1;
    if (after < s.size() && std::isdigit((unsigned char)s[after]))
    {
      size_t j = after;
      long long column = 0;
      while (j < s.size() && std::isdigit((unsigned char)s[j]))
      {
        column = column * 10 + (s[j] - '0');
        j++;
      }
      if (j < s.size() && s[j] == ':' && column <= 10000000)
      {
        col = (int)column;
        end = j + 1;
        return true;
      }
      if (j == s.size() && end_of_line_ok && column <= 10000000)
      {
        col = (int)column;
        end = j;
        return true;
      }
    }
    end = after;
    return true;
  }

  // Reads a severity keyword at the start of `rest` and the message after it.
  // The keyword may carry a code -- rustc's `error[E0425]`, tsc's `error
  // TS2322`, MSVC's `warning C4996` -- which belongs to the severity, not to
  // what the user reads. False when no keyword heads the text, which is how a
  // plain output line (and GCC's bare `In file included from ...`) is told
  // apart from a message that a later line will locate.
  bool read_severity_head(const std::string &rest, int &severity, std::string &message)
  {
    static const struct
    {
      const char *word;
      int severity;
    } kWords[] = {{"fatal error", 1}, {"error", 1}, {"warning", 2}, {"note", 3}, {"info", 3}};
    for (const auto &entry : kWords)
    {
      const size_t len = std::strlen(entry.word);
      if (rest.size() <= len || lowered(rest.substr(0, len)) != entry.word)
      {
        continue;
      }
      size_t i = len;
      if (i < rest.size() && (rest[i] == '[' || rest[i] == '('))
      {
        const char close = rest[i] == '[' ? ']' : ')';
        const size_t close_at = rest.find(close, i + 1);
        if (close_at == std::string::npos)
        {
          continue;
        }
        i = close_at + 1;
      }
      // `error TS2322:` / `warning C4996:`: a token carrying a digit, sitting
      // between the keyword and its separator, is the code. Without a digit it
      // is the message (`error: expected expression`) and stays put.
      while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
      {
        i++;
      }
      size_t word_end = i;
      while (word_end < rest.size()
             && (std::isalnum((unsigned char)rest[word_end]) || rest[word_end] == '_'))
      {
        word_end++;
      }
      const std::string token = rest.substr(i, word_end - i);
      const bool has_digit =
          !token.empty() && token.find_first_of("0123456789") != std::string::npos;
      if (has_digit && word_end < rest.size() && rest[word_end] == ':')
      {
        i = word_end;
      }
      if (i >= rest.size() || rest[i] != ':')
      {
        continue;
      }
      i++;
      while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
      {
        i++;
      }
      severity = entry.severity;
      message = rest.substr(i);
      return true;
    }
    return false;
  }

  // The same, for the tail of a line that already carries a location: without a
  // keyword the tail is the message and the finding is an error, the way
  // errorformat reads it.
  void split_severity(const std::string &rest, int &severity, std::string &message)
  {
    if (!read_severity_head(rest, severity, message))
    {
      severity = 1;
      message = rest;
    }
  }
} // namespace

void compile_parse::Parser::reset()
{
  pending_message_.clear();
  pending_severity_ = 0;
}

bool compile_parse::Parser::feed(const std::string &raw, CompileFinding &out)
{
  const std::string line = trimmed(raw);
  if (line.empty())
  {
    return false;
  }

  // rustc's arrow: the message came on the line above.
  if (line.rfind("-->", 0) == 0)
  {
    const std::string tail = trimmed(line.substr(3));
    size_t pos = 0;
    while (pos < tail.size() && !std::isdigit((unsigned char)tail[pos]))
    {
      pos++;
    }
    int line_number = 0;
    int col = 0;
    size_t end = 0;
    if (pos >= tail.size()
        || !read_location(tail, pos, line_number, col, end, /*end_of_line_ok=*/true)
        || end != tail.size())
    {
      return false;
    }
    // The digit run starts after the path's own separator.
    std::string path = tail.substr(0, pos);
    if (!path.empty() && path.back() == ':')
    {
      path.pop_back();
    }
    if (!looks_like_path(path) || pending_message_.empty())
    {
      return false;
    }
    out.filepath = path;
    out.line = line_number;
    out.col = col;
    out.severity = pending_severity_ == 0 ? 1 : pending_severity_;
    out.message = pending_message_;
    pending_message_.clear();
    pending_severity_ = 0;
    return true;
  }

  // Python: `File "tools/build.py", line 42, in main`.
  if (line.rfind("File \"", 0) == 0)
  {
    const size_t close = line.find('"', 6);
    if (close != std::string::npos)
    {
      const size_t line_at = line.find(", line ", close);
      if (line_at != std::string::npos)
      {
        size_t pos = line_at + 7;
        size_t end = pos;
        long long number = 0;
        while (end < line.size() && std::isdigit((unsigned char)line[end]))
        {
          number = number * 10 + (line[end] - '0');
          end++;
        }
        if (end > pos)
        {
          out.filepath = line.substr(6, close - 6);
          out.line = (int)number;
          out.col = 0;
          out.severity = 1;
          out.message = line;
          return true;
        }
      }
    }
  }

  // `file(line[,col]): error CODE: message` -- MSVC and tsc.
  const size_t paren_at = line.find('(');
  if (paren_at != std::string::npos && paren_at > 0)
  {
    const size_t close = line.find(')', paren_at);
    const std::string inside = close == std::string::npos
                                   ? std::string()
                                   : line.substr(paren_at + 1, close - paren_at - 1);
    const size_t comma = inside.find(',');
    const std::string line_text = comma == std::string::npos ? inside : inside.substr(0, comma);
    const std::string col_text = comma == std::string::npos ? "" : inside.substr(comma + 1);
    // MSVC prints no column at all (`a.cpp(88): warning C4996: ...`), tsc
    // prints one; both are the same shape to the reader.
    const bool numbers =
        !line_text.empty() && line_text.find_first_not_of("0123456789") == std::string::npos
        && (col_text.empty() || col_text.find_first_not_of("0123456789") == std::string::npos);
    // Only when the numbers are followed straight away by the message's own
    // colon: a parenthesis elsewhere in the line (`a.c:12:5: call foo(int)`) is
    // not this shape.
    if (numbers && close + 1 < line.size() && line[close + 1] == ':'
        && looks_like_path(line.substr(0, paren_at)))
    {
      out.filepath = line.substr(0, paren_at);
      out.line = std::stoi(line_text);
      out.col = col_text.empty() ? 0 : std::stoi(col_text);
      split_severity(trimmed(line.substr(close + 2)), out.severity, out.message);
      return true;
    }
  }

  // `file:line[:col]: message` -- GCC, Clang, javac, Go, and the generic form.
  for (size_t i = 0; i < line.size(); i++)
  {
    if (line[i] != ':' || i == 0 || i + 1 >= line.size()
        || !std::isdigit((unsigned char)line[i + 1]))
    {
      continue;
    }
    int line_number = 0;
    int col = 0;
    size_t end = 0;
    if (!read_location(line, i + 1, line_number, col, end) || end >= line.size())
    {
      continue;
    }
    const std::string prefix = line.substr(0, i);
    if (!looks_like_path(prefix))
    {
      continue;
    }
    const std::string rest = trimmed(line.substr(end));
    if (rest.empty())
    {
      continue;
    }
    out.filepath = prefix;
    out.line = line_number;
    out.col = col;
    split_severity(rest, out.severity, out.message);
    return true;
  }

  // A severity head with no location: rustc prints the message on one line and
  // the file on the arrow line below it. Anything else is output, not a
  // finding.
  int severity = 0;
  std::string message;
  if (read_severity_head(line, severity, message))
  {
    pending_severity_ = severity;
    pending_message_ = message;
  }
  return false;
}
