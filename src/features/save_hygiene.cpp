#include "save_hygiene.h"

#include "text_features.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace
{
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

// The C-family extensions whose nesting the editor's own block rules describe.
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
  for (const char *candidate : kExtensions)
  {
    if (extension == candidate)
    {
      return true;
    }
  }
  return false;
}

std::string trim_left(const std::string &line)
{
  const size_t start = line.find_first_not_of(" \t");
  return start == std::string::npos ? std::string() : line.substr(start);
}

// `line` with its trailing line comment dropped and the contents of its string
// and character literals blanked to spaces, so a scan reads syntax rather than
// text. Lengths are kept, so a column into the result is a column into the
// line. `marker` is the comment lead: "//" for C, "--" for Lua, "#" for Python.
std::string code_only(const std::string &line, const char *marker)
{
  std::string out = line;
  const size_t marker_len = std::strlen(marker);
  char literal = '\0';
  for (size_t i = 0; i < out.size(); i++)
  {
    const char c = out[i];
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
    if (c == marker[0] && i + marker_len <= out.size() && out.compare(i, marker_len, marker) == 0)
    {
      out.erase(i);
      break;
    }
  }
  return out;
}

// Whether a line's column belongs to a wrapped expression rather than to a
// block: the line before ends on an operator, or this one starts on one.
// Recomputing those would pull a hanging indent back to the statement's column,
// so the reindent leaves them where the author put them.
bool looks_continued(const std::string &previous_code, const std::string &content_code)
{
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

// Updates `inside` for the line just read and reports whether the line begins
// inside a `/* ... */` comment. Those lines are the comment's own text, so the
// reindent has to leave them exactly as they are.
bool c_family_comment_span(const std::string &line, bool &inside)
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

// Updates `quote` for the line just read and reports whether it begins inside a
// Python triple-quoted string (a docstring is the usual one). The lines inside
// are content, so the reindent leaves them alone.
bool python_string_span(const std::string &line, std::string &quote)
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

void python_bracket_depth(const std::string &code, int &depth)
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
}

// C-family and Lua: recompute each line's depth from the previous line with
// indent_for_new_line, the rule Enter itself uses, so the same shape that would
// be typed is the shape that is written.
std::vector<std::string> reindent_braced(const std::vector<std::string> &lines,
                                         const std::string &path,
                                         int step,
                                         bool c_family)
{
  std::vector<std::string> out;
  out.reserve(lines.size());
  int hint = 0;
  int base = -1;
  std::string previous_code;
  bool in_block_comment = false;
  std::string lua_closer;

  for (const std::string &line : lines)
  {
    if (c_family)
    {
      if (c_family_comment_span(line, in_block_comment))
      {
        out.push_back(line);
        continue;
      }
    }
    else if (lua_long_span(line, lua_closer))
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
      hint = base;
    }

    const int original = EditorFeatures::get_indent_level(line);
    const std::string code = code_only(content, c_family ? "//" : "--");
    const char lead = content[0];

    int target = hint;
    if (lead == '}' || lead == ')' || lead == ']')
    {
      target = std::max(base, hint - step);
    }
    else if (c_family && lead == '#')
    {
      target = base;
    }
    else if (c_family && EditorFeatures::should_cpp_dedent(content))
    {
      target = std::max(base, hint - step);
    }
    else if (!c_family && EditorFeatures::should_lua_dedent(content))
    {
      target = std::max(base, hint - step);
    }

    bool continuation = false;
    if (target < original && looks_continued(previous_code, code))
    {
      target = original;
      continuation = true;
    }

    const std::string reindented = EditorFeatures::get_indent_string(target, step) + content;
    out.push_back(reindented);
    if (!continuation)
    {
      hint = EditorFeatures::indent_for_new_line(path, reindented, (int)reindented.size(), step);
    }
    previous_code = code;
  }
  return out;
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
    if (python_string_span(line, open_quote))
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
    if (brackets > 0)
    {
      // Inside a bracket the line is a continuation: its column is the author's
      // alignment, not a block level, and the block stack is untouched.
      out.push_back(line);
      python_bracket_depth(code_only(content, "#"), brackets);
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

    const int depth = (int)blocks.size();
    out.push_back(EditorFeatures::get_indent_string(base + depth * step, step) + content);

    const std::string code = code_only(content, "#");
    python_bracket_depth(code, brackets);
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
  const std::string extension = extension_of(path);
  return is_c_family_extension(extension) || extension == ".lua" || extension == ".py";
}

std::vector<std::string> SaveHygiene::reindent_lines(const std::vector<std::string> &lines,
                                                     const std::string &path,
                                                     int tab_size)
{
  const int step = std::max(1, tab_size);
  const std::string extension = extension_of(path);
  if (extension == ".py")
  {
    return reindent_python(lines, step);
  }
  if (is_c_family_extension(extension))
  {
    return reindent_braced(lines, path, step, true);
  }
  if (extension == ".lua")
  {
    return reindent_braced(lines, path, step, false);
  }
  return lines;
}
