#include "save_hygiene.h"

#include "text_features.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace
{
  // What a language's block structure is made of, which is also how its
  // indentation is read back.
  enum class ReindentStyle
  {
    None,
    Braced,  // `{` bodies, with `case`/`default` labels inside the braces
    CFamily, // the C-family dialect: labels and preprocessor lines step out
    Lua,     // keyword blocks: `then`/`do`/`repeat` open, `end`/`until` close
    Rust,    // braces plus the `where` clause
    Python,  // indentation-significant: block boundaries come from the columns
  };

// The extensions where a line ending in spaces is a line break rather than an
// accident: markdown is the one format a stray blank is not stray in.
bool is_line_break_sensitive(const std::string &extension)
{
  return extension == ".md" || extension == ".markdown" || extension == ".mdown"
         || extension == ".mkd" || extension == ".mdx";
}

// The extension of a path, lowercased, or empty when it has none. A dot in a
// directory name (`~/.config/jot/a`) is not an extension.
std::string extension_of(const std::string &path)
{
  const size_t slash = path.find_last_of("/\\");
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
  {
    return {};
  }
  std::string extension = path.substr(dot);
  std::transform(extension.begin(),
                 extension.end(),
                 extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

bool is_one_of(const std::string &value, const char *const *candidates, size_t count)
{
  for (size_t i = 0; i < count; i++)
  {
    if (value == candidates[i])
    {
      return true;
    }
  }
  return false;
}

// The C-family extensions, whose brace dialect also pulls `case`/`default`
// labels and preprocessor lines out to the item's own column.
bool is_c_family_extension(const std::string &extension)
{
  static const char *const kExtensions[] = {".c",
                                            ".h",
                                            ".cc",
                                            ".hh",
                                            ".cxx",
                                            ".hxx",
                                            ".cpp",
                                            ".hpp",
                                            ".c++",
                                            ".h++",
                                            ".ipp",
                                            ".tpp",
                                            ".tcc",
                                            ".inl",
                                            ".cu",
                                            ".cuh",
                                            ".m",
                                            ".mm"};
  return is_one_of(extension, kExtensions, sizeof(kExtensions) / sizeof(kExtensions[0]));
}

// The other brace languages. JavaScript and its neighbours, the JVM set, the
// systems set, proto/thrift, and the styles and data formats that nest with
// nothing but braces and brackets. Ruby, shell and the markup formats are not
// here: their blocks are keywords or tags, which these rules do not read.
bool is_braced_extension(const std::string &extension)
{
  static const char *const kExtensions[] = {
      ".js",    ".mjs",    ".cjs", ".jsx", ".ts",   ".tsx",   ".mts",  ".cts",
      ".java",  ".cs",     ".csx", ".kt",  ".kts",  ".scala", ".dart", ".groovy",
      ".proto", ".thrift", ".zig", ".css", ".scss", ".less",  ".json", ".jsonc"};
  return is_one_of(extension, kExtensions, sizeof(kExtensions) / sizeof(kExtensions[0]));
}

ReindentStyle reindent_style(const std::string &extension)
{
  if (extension == ".py")
  {
    return ReindentStyle::Python;
  }
  if (extension == ".lua")
  {
    return ReindentStyle::Lua;
  }
  if (extension == ".rs")
  {
    return ReindentStyle::Rust;
  }
  if (is_c_family_extension(extension) || extension == ".go" || extension == ".swift")
  {
    // go and swift write their `case` labels at the switch's own column, and
    // swift's `#if` belongs at the file's margin, which is the C-family dialect.
    return ReindentStyle::CFamily;
  }
  if (is_braced_extension(extension))
  {
    return ReindentStyle::Braced;
  }
  return ReindentStyle::None;
}

std::string trim_left(const std::string &line)
{
  const size_t start = line.find_first_not_of(" \t");
  return start == std::string::npos ? std::string() : line.substr(start);
}

// `line` with its comments blanked to spaces and the contents of its string and
// character literals blanked the same way, so a scan reads syntax rather than
// text. The length is kept, so a column into the result is a column into the
// line. `marker` is the line-comment lead ("//", "--" or "#"); a `//` that
// follows a colon is a URL scheme rather than a comment, and block comments are
// read whatever the marker is.
std::string code_only(const std::string &line, const char *marker)
{
  std::string out = line;
  const size_t marker_len = std::strlen(marker);
  char literal = '\0';
  bool in_block = false;
  for (size_t i = 0; i < out.size(); i++)
  {
    const char c = out[i];
    if (in_block)
    {
      if (c == '*' && i + 1 < out.size() && out[i + 1] == '/')
      {
        out[i] = ' ';
        out[i + 1] = ' ';
        i++;
        in_block = false;
        continue;
      }
      out[i] = ' ';
      continue;
    }
    if (literal != '\0')
    {
      if (c == '\\' && i + 1 < out.size())
      {
        out[i] = ' ';
        out[i + 1] = ' ';
        i++;
        continue;
      }
      if (c == literal)
      {
        literal = '\0';
      }
      out[i] = ' ';
      continue;
    }
    if (c == '"' || c == '\'')
    {
      literal = c;
      out[i] = ' ';
      continue;
    }
    if (c == '/' && i + 1 < out.size() && out[i + 1] == '*')
    {
      out[i] = ' ';
      out[i + 1] = ' ';
      i++;
      in_block = true;
      continue;
    }
    if (marker_len > 0 && c == marker[0] && i + marker_len <= out.size()
        && out.compare(i, marker_len, marker) == 0)
    {
      if (marker[0] == '/' && i > 0 && out[i - 1] == ':')
      {
        continue;
      }
      for (size_t j = i; j < out.size(); j++)
      {
        out[j] = ' ';
      }
      break;
    }
  }
  return out;
}

// A control statement whose body is on the next line without a brace of its
// own: `if (x)`, `for (...)`, `else`, `do`. Every language reindented here
// indents such a body, and Enter's own C-family rule does not step in for a
// control statement (its brace stands on that line instead), so without this a
// braceless body would be pulled back to the header's column.
bool is_braceless_header(const std::string &code)
{
  const std::string trimmed = trim_left(EditorFeatures::trim_right(code));
  if (trimmed.empty())
  {
    return false;
  }
  static const char *const kWords[] = {"if", "for", "while", "else", "do", "catch"};
  bool named = false;
  for (const char *word : kWords)
  {
    const size_t length = std::strlen(word);
    if (trimmed.size() < length || trimmed.compare(0, length, word) != 0)
    {
      continue;
    }
    if (trimmed.size() > length
        && (std::isalnum(static_cast<unsigned char>(trimmed[length])) || trimmed[length] == '_'))
    {
      continue;
    }
    named = true;
    break;
  }
  if (!named)
  {
    return false;
  }
  return trimmed.back() == ')' || trimmed == "else" || trimmed == "do";
}

// Whether a line's column is the author's rather than the block's: the body of
// a braceless control statement, or a wrapped expression's alignment (the line
// before ends on an operator, or this one starts on one). Recomputing those
// would pull them back to the statement's column, so the reindent leaves them
// where the author put them.
bool keeps_own_column(const std::string &previous_code,
                      const std::string &content_code,
                      const std::string &content)
{
  if (!content.empty() && content[0] != '{' && is_braceless_header(previous_code))
  {
    return true;
  }
  const std::string previous = EditorFeatures::trim_right(previous_code);
  if (!previous.empty() && std::strchr("=+-*/%&|^?,<>\\", previous.back()) != nullptr)
  {
    return true;
  }
  const size_t start = content_code.find_first_not_of(" \t");
  if (start == std::string::npos)
  {
    return false;
  }
  return std::strchr(".?+-*/%&|^<>,:", content_code[start]) != nullptr;
}

// Whether the line is a label belonging one level out from the body it
// introduces: `case ...:` and `default:` in a brace language, and the
// C-family's access specifiers too.
bool is_label_line(ReindentStyle style, const std::string &content)
{
  if (style == ReindentStyle::CFamily)
  {
    return EditorFeatures::should_cpp_dedent(content);
  }
  if (style != ReindentStyle::Braced)
  {
    return false;
  }
  const std::string trimmed = trim_left(EditorFeatures::trim_right(content));
  if (trimmed.empty() || trimmed.back() != ':')
  {
    return false;
  }
  if (trimmed == "default:")
  {
    return true;
  }
  return trimmed.size() > 5 && trimmed.compare(0, 5, "case ") == 0;
}

// Rust's `where` clause: a bound list indented one step under the header, with
// the item's brace back out at the header's own column. The shared rules read
// `where` as an ordinary line, so the clause is tracked on its own.
bool rust_where_header(const std::string &code)
{
  const std::string trimmed = EditorFeatures::trim_right(code);
  if (trimmed == "where")
  {
    return true;
  }
  return trimmed.size() > 6 && trimmed.compare(trimmed.size() - 6, 6, " where") == 0;
}

bool rust_where_ends(const std::string &code)
{
  const std::string trimmed = trim_left(code);
  return !trimmed.empty() && (trimmed[0] == '{' || trimmed[0] == '}' || trimmed[0] == ';');
}

// Updates `inside` for the line just read and reports whether the line begins
// inside a `/* ... */` comment. Those lines are the comment's own text, so the
// reindent has to leave them exactly as they are.
bool block_comment_span(const std::string &line, bool &inside)
{
  const bool started_inside = inside;
  char literal = '\0';
  for (size_t i = 0; i < line.size(); i++)
  {
    const char c = line[i];
    if (inside)
    {
      if (c == '*' && i + 1 < line.size() && line[i + 1] == '/')
      {
        inside = false;
        i++;
      }
      continue;
    }
    if (literal != '\0')
    {
      if (c == '\\' && i + 1 < line.size())
      {
        i++;
        continue;
      }
      if (c == literal)
      {
        literal = '\0';
      }
      continue;
    }
    if (c == '"' || c == '\'')
    {
      literal = c;
      continue;
    }
    if (c == '/' && i + 1 < line.size() && line[i + 1] == '*')
    {
      inside = true;
      i++;
    }
  }
  return started_inside;
}

// The same for a backtick string: Go's raw string and JavaScript's template
// literal both run across lines, and their body is text.
bool backtick_span(const std::string &line, bool &inside)
{
  const bool started_inside = inside;
  for (size_t i = 0; i < line.size(); i++)
  {
    if (line[i] != '`')
    {
      continue;
    }
    size_t slashes = 0;
    for (size_t j = i; j > 0 && line[j - 1] == '\\'; j--)
    {
      slashes++;
    }
    if ((slashes % 2) == 1 && !inside)
    {
      continue;
    }
    inside = !inside;
  }
  return started_inside;
}

// The same for a Python-style triple-quoted string, which Java calls a text
// block and Kotlin, Scala and Swift call a raw string.
bool triple_quote_span(const std::string &line, std::string &quote)
{
  const bool started_inside = !quote.empty();
  size_t i = 0;
  while (i + 3 <= line.size())
  {
    if (line.compare(i, 3, "\"\"\"") != 0 && line.compare(i, 3, "'''") != 0)
    {
      i++;
      continue;
    }
    const std::string three = line.substr(i, 3);
    if (quote.empty())
    {
      quote = three;
    }
    else if (quote == three)
    {
      quote.clear();
    }
    i += 3;
  }
  return started_inside;
}

// The closer for the Lua long bracket that opens at `at` (`[`, some `=`, `[`),
// or empty when none does. `width` receives the opener's length.
std::string lua_long_bracket_at(const std::string &line, size_t at, size_t &width)
{
  width = 0;
  if (line[at] != '[')
  {
    return {};
  }
  size_t i = at + 1;
  while (i < line.size() && line[i] == '=')
  {
    i++;
  }
  if (i >= line.size() || line[i] != '[')
  {
    return {};
  }
  width = i - at + 1;
  return "]" + std::string(i - at - 1, '=') + "]";
}

// Updates `closer` for the line just read and reports whether it begins inside
// a Lua long bracket: `[[ ... ]]`, `[=[ ... ]=]`, and the `--[[ ... ]]` comment
// form, whose body is text a reindent must leave alone.
bool lua_long_span(const std::string &line, std::string &closer)
{
  const bool started_inside = !closer.empty();
  size_t i = 0;
  while (i < line.size())
  {
    if (!closer.empty())
    {
      if (line.compare(i, closer.size(), closer) == 0)
      {
        i += closer.size();
        closer.clear();
        continue;
      }
      i++;
      continue;
    }
    if (line[i] == '-' && i + 1 < line.size() && line[i + 1] == '-')
    {
      size_t width = 0;
      const std::string matched = lua_long_bracket_at(line, i + 2, width);
      if (matched.empty())
      {
        break; // A `--` line comment: nothing after it counts.
      }
      closer = matched;
      i += 2 + width;
      continue;
    }
    if (line[i] == '"' || line[i] == '\'')
    {
      const char quote = line[i++];
      while (i < line.size() && line[i] != quote)
      {
        if (line[i] == '\\')
        {
          i++;
        }
        i++;
      }
      if (i < line.size())
      {
        i++;
      }
      continue;
    }
    size_t width = 0;
    const std::string matched = lua_long_bracket_at(line, i, width);
    if (matched.empty())
    {
      i++;
      continue;
    }
    closer = matched;
    i += width;
  }
  return started_inside;
}

// One level of the indentation stack: the column a plain line in it sits at,
// and what put it there. A closer steps out of the block its brace holds, so
// the alignments and label bodies above that brace come off with it.
enum class FrameKind
{
  Brace,
  Wrap,  // a wrapped expression lined up under its opener
  Label, // the body under a `case`/`default` label
};

struct IndentFrame
{
  int indent;
  FrameKind kind;
};

// The index of the innermost `{` frame, which is what a label belongs to.
int innermost_brace(const std::vector<IndentFrame> &frames)
{
  for (size_t i = frames.size(); i-- > 0;)
  {
    if (frames[i].kind == FrameKind::Brace)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Steps out of the block a closer closes. A `}` lands on the block its brace
// holds, so the alignments and label bodies above that brace come off with it;
// a `)`/`]` lands on the wrapped expression it opened.
void pop_closer(std::vector<IndentFrame> &frames, char closer)
{
  if (closer == '}')
  {
    while (frames.size() > 1 && frames.back().kind != FrameKind::Brace)
    {
      frames.pop_back();
    }
  }
  else
  {
    while (frames.size() > 1 && frames.back().kind == FrameKind::Label)
    {
      frames.pop_back();
    }
  }
  if (frames.size() > 1)
  {
    frames.pop_back();
  }
}

void pop_top(std::vector<IndentFrame> &frames)
{
  if (frames.size() > 1)
  {
    frames.pop_back();
  }
}

struct Opener
{
  char ch;
  size_t at;
};

// Walks the rest of a line's code: a closer with nothing of its own to shut
// steps out of the block a previous line opened, and the openers the line
// leaves open are returned, outermost first. `from` is how much of the line the
// leading closers already consumed.
std::vector<Opener>
balance_line(const std::string &code, size_t from, std::vector<IndentFrame> &frames)
{
  std::vector<Opener> open;
  for (size_t i = from; i < code.size(); i++)
  {
    const char c = code[i];
    if (c == '(' || c == '[' || c == '{')
    {
      open.push_back({c, i});
      continue;
    }
    if (c != ')' && c != ']' && c != '}')
    {
      continue;
    }
    const char match = c == ')' ? '(' : (c == ']' ? '[' : '{');
    bool closed = false;
    for (size_t j = open.size(); j-- > 0;)
    {
      if (open[j].ch != match)
      {
        continue;
      }
      open.erase(open.begin() + static_cast<long>(j));
      closed = true;
      break;
    }
    if (!closed)
    {
      pop_closer(frames, c);
    }
  }
  return open;
}

// The brace languages: the nesting is read back from the file's own braces,
// brackets and labels, which is what Enter and the external formatters agree
// on, and the column of every line is then rebuilt from the settings step.
std::vector<std::string>
reindent_braced(const std::vector<std::string> &lines, int step, ReindentStyle style)
{
  const bool c_family = style == ReindentStyle::CFamily;
  const bool lua = style == ReindentStyle::Lua;
  const bool rust = style == ReindentStyle::Rust;
  const char *marker = lua ? "--" : "//";

  std::vector<std::string> out;
  out.reserve(lines.size());
  std::vector<IndentFrame> frames;
  int base = -1;
  int where_indent = -1;
  std::string previous_code;
  bool in_block_comment = false;
  bool in_backtick = false;
  std::string in_triple;
  std::string lua_closer;

  for (const std::string &line : lines)
  {
    // The body of a comment or a multi-line string is its own text: a reindent
    // must not touch it, and it says nothing about the block structure.
    if (lua)
    {
      if (lua_long_span(line, lua_closer))
      {
        out.push_back(line);
        continue;
      }
    }
    else if (block_comment_span(line, in_block_comment) || backtick_span(line, in_backtick)
             || triple_quote_span(line, in_triple))
    {
      out.push_back(line);
      continue;
    }

    const std::string content = trim_left(line);
    if (content.empty())
    {
      out.push_back(std::string());
      continue;
    }
    if (base < 0)
    {
      // The file's own left margin is the baseline: a fragment that opens at
      // column four keeps its four, and only the nesting under it is rebased.
      base = EditorFeatures::get_indent_level(line);
      frames.push_back({base, FrameKind::Brace});
    }

    const int original = EditorFeatures::get_indent_level(line);
    const std::string code = code_only(content, marker);

    // A Rust `where` clause ends at the item's body, whose brace stands back
    // out at the header's own column.
    if (rust && where_indent >= 0 && rust_where_ends(code))
    {
      pop_top(frames);
      where_indent = -1;
    }

    // A line that closes a block steps out of it before its own column is read:
    // a `}` lands on the block it closes rather than inside it.
    size_t scanned = 0;
    while (scanned < content.size())
    {
      const char c = content[scanned];
      if (c == ' ' || c == '\t')
      {
        scanned++;
        continue;
      }
      if (!lua && (c == '}' || c == ')' || c == ']'))
      {
        pop_closer(frames, c);
        scanned++;
        continue;
      }
      break;
    }
    if (lua && EditorFeatures::should_lua_dedent(content))
    {
      pop_top(frames);
    }

    const bool label = is_label_line(style, content);
    if (label && !frames.empty() && frames.back().kind == FrameKind::Label)
    {
      // A label ends the body the previous label opened.
      frames.pop_back();
    }
    int target = frames.empty() ? base : frames.back().indent;
    if (c_family && !content.empty() && content[0] == '#')
    {
      // A preprocessor directive sits at the file's left margin, wherever it is
      // written.
      target = base;
    }
    else if (label && c_family)
    {
      // A `case`/`default` label and an access specifier stand at the column of
      // the switch or class itself, one step out from the body they introduce.
      const int brace = innermost_brace(frames);
      target =
          std::max(base, brace >= 0 ? frames[static_cast<size_t>(brace)].indent - step : target);
    }
    if (target < original && keeps_own_column(previous_code, code, content))
    {
      target = original;
    }

    out.push_back(EditorFeatures::get_indent_string(target, step) + content);

    // What the line leaves open shapes the lines under it. Only the innermost
    // opener decides their column - a body steps in, a wrapped expression lines
    // up under the first thing after the opener - and the openers under it are
    // carried so that the closers walking back out land on the right frame.
    const std::vector<Opener> open = balance_line(code, scanned, frames);
    for (size_t i = 0; i < open.size(); i++)
    {
      const bool innermost = i + 1 == open.size();
      int indent = target;
      if (innermost)
      {
        if (open[i].ch == '{')
        {
          indent = target + step;
        }
        else
        {
          const size_t after = code.find_first_not_of(" \t", open[i].at + 1);
          indent = after == std::string::npos ? target + step : target + static_cast<int>(after);
        }
      }
      frames.push_back({indent, open[i].ch == '{' ? FrameKind::Brace : FrameKind::Wrap});
    }
    if (open.empty())
    {
      if (label)
      {
        frames.push_back({target + step, FrameKind::Label});
      }
      else if (lua && EditorFeatures::should_lua_auto_indent(content))
      {
        frames.push_back({target + step, FrameKind::Wrap});
      }
    }
    if (rust && rust_where_header(code))
    {
      where_indent = target;
      frames.push_back({target + step, FrameKind::Wrap});
    }

    previous_code = code;
  }
  return out;
}

// The bracket depth after a line of code, so a Python continuation line can be
// told from a block line.
int bracket_depth_after(const std::string &code, int depth)
{
  for (char c : code)
  {
    if (c == '(' || c == '[' || c == '{')
    {
      depth++;
    }
    else if (c == ')' || c == ']' || c == '}')
    {
      depth = std::max(0, depth - 1);
    }
  }
  return depth;
}

// Python: the depth can only be read from the file's own indentation, since a
// block is nothing but its indentation, so each block's original column is
// pushed on a stack and the line's depth is how many are open over it. The
// column itself is then rebuilt from the settings step, which is what turns a
// file written at another width into one at the configured width.
std::vector<std::string> reindent_python(const std::vector<std::string> &lines, int step)
{
  std::vector<std::string> out;
  out.reserve(lines.size());
  std::vector<int> blocks; // the original column each open block started at
  std::string open_quote;
  int brackets = 0;
  int base = -1;

  for (const std::string &line : lines)
  {
    if (triple_quote_span(line, open_quote))
    {
      out.push_back(line);
      continue;
    }

    const std::string content = trim_left(line);
    if (content.empty())
    {
      out.push_back(std::string());
      continue;
    }
    const std::string code = code_only(content, "#");
    if (brackets > 0)
    {
      // Inside a bracket the line is a continuation: its column is the author's
      // alignment, not a block level, and the block stack is untouched.
      out.push_back(line);
      brackets = bracket_depth_after(code, brackets);
      continue;
    }

    const int original = EditorFeatures::get_indent_level(line);
    if (base < 0)
    {
      base = original;
    }

    // A block ends where the next line is no deeper than its header was, and
    // the original indentation is the only record of that.
    while (!blocks.empty() && original <= blocks.back())
    {
      blocks.pop_back();
    }

    const int depth = static_cast<int>(blocks.size());
    out.push_back(EditorFeatures::get_indent_string(base + depth * step, step) + content);

    brackets = bracket_depth_after(code, brackets);
    const std::string trimmed = EditorFeatures::trim_right(code);
    if (brackets == 0 && !trimmed.empty() && trimmed.back() == ':')
    {
      blocks.push_back(original);
    }
  }
  return out;
}
} // namespace

bool SaveHygiene::has_trailing_whitespace(const std::string &line)
{
  return !line.empty() && (line.back() == ' ' || line.back() == '\t');
}

std::string SaveHygiene::trim_trailing_whitespace(const std::string &line)
{
  size_t end = line.size();
  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
  {
    end--;
  }
  return line.substr(0, end);
}

bool SaveHygiene::preserves_trailing_whitespace(const std::string &path)
{
  return is_line_break_sensitive(extension_of(path));
}

bool SaveHygiene::supports_reindent(const std::string &path)
{
  return reindent_style(extension_of(path)) != ReindentStyle::None;
}

std::vector<std::string> SaveHygiene::reindent_lines(const std::vector<std::string> &lines,
                                                     const std::string &path,
                                                     int tab_size)
{
  const int step = std::max(1, tab_size);
  const ReindentStyle style = reindent_style(extension_of(path));
  if (style == ReindentStyle::Python)
  {
    return reindent_python(lines, step);
  }
  if (style == ReindentStyle::None)
  {
    return lines;
  }
  return reindent_braced(lines, step, style);
}
