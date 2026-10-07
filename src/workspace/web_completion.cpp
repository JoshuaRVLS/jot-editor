// The workspace's CSS vocabulary (see web_completion.h for what it is for).
//
// Everything here is a scan of text: no filesystem access in the three
// extractors, no buffer, no server. The walk at the bottom reads the files and
// hands each one to the same extractors a case can call directly, so the only
// part a test cannot reach without a workspace is the walk itself.
#include "workspace/web_completion.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
  bool is_ident_char(char c)
  {
    unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) != 0 || c == '_';
  }

  bool is_ident_start(char c)
  {
    unsigned char uc = (unsigned char)c;
    return std::isalpha(uc) != 0 || c == '_';
  }

  // A name character as CSS and markup spell them: an identifier plus the `-`
  // every BEM-ish class and every custom property is built from.
  bool is_name_char(char c)
  {
    return is_ident_char(c) || c == '-';
  }

  // An attribute name in HTML/JSX can carry `-`, `:`, `.` and `@`
  // (`data-id`, `v-bind:foo`, `x-on.click`), so it is scanned with the wider
  // alphabet. Without this, `class` in `:class="x"` would be read as a class
  // attribute on its own.
  bool is_attr_name_char(char c)
  {
    return is_ident_char(c) || c == '-' || c == ':' || c == '.' || c == '@';
  }

  char lower_ascii(char c)
  {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }

  bool equals_ascii_ci(const std::string &text, const char *word)
  {
    std::size_t i = 0;
    for (; word[i] != '\0'; i++)
    {
      if (i >= text.size() || lower_ascii(text[i]) != word[i])
      {
        return false;
      }
    }
    return i == text.size();
  }

  void add_token(const std::string &token, std::vector<std::string> &out)
  {
    if (token.empty())
    {
      return;
    }
    // A token with a template marker in it (`{{ cls }}`, `${x}`, a backtick
    // expression) is a value the scan cannot resolve to a name, so it is not
    // offered as one. The `.` is not a marker but has the same effect: a `.` is
    // not a class-name character, and the words inside a template expression
    // (`loop.index` in `{{ loop.index }}`) are separated from their braces by
    // whitespace, so this is what stops them from arriving as names on their
    // own.
    for (char c : token)
    {
      if (c == '{' || c == '}' || c == '$' || c == '`' || c == '"' || c == '\'' || c == '.')
      {
        return;
      }
    }
    out.push_back(token);
  }

  // The whitespace-separated tokens of a class attribute's value.
  void add_class_list(const std::string &value, std::vector<std::string> &out)
  {
    std::size_t i = 0;
    while (i < value.size())
    {
      while (i < value.size() && std::isspace((unsigned char)value[i]))
      {
        i++;
      }
      std::size_t start = i;
      while (i < value.size() && !std::isspace((unsigned char)value[i]))
      {
        i++;
      }
      add_token(value.substr(start, i - start), out);
    }
  }

  std::string extension_of(const std::string &path)
  {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
      return "";
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), lower_ascii);
    return ext;
  }

  bool in_list(const std::string &ext, std::initializer_list<const char *> list)
  {
    for (const char *candidate : list)
    {
      if (ext == candidate)
      {
        return true;
      }
    }
    return false;
  }

  // Single-file components carry both languages in one file, and so does an
  // HTML file with a `<style>` block.
  bool is_hybrid_path(const std::string &path)
  {
    return in_list(extension_of(path), {".vue", ".svelte", ".astro", ".html", ".htm", ".xhtml"});
  }

  bool should_skip_dir(const std::string &name)
  {
    if (name.empty() || name[0] == '.')
    {
      return true;
    }
    static const std::set<std::string> skipped = {"node_modules",
                                                  "dist",
                                                  "build",
                                                  "out",
                                                  "bin",
                                                  "obj",
                                                  "target",
                                                  "venv",
                                                  "vendor",
                                                  "deps",
                                                  "CMakeFiles"};
    if (skipped.count(name) != 0)
    {
      return true;
    }
    return name.rfind("build-", 0) == 0 || name.rfind("build_", 0) == 0
           || name.rfind("cmake-build", 0) == 0;
  }
} // namespace

namespace WebCompletion
{
  // A file whose text is markup: the class names live in attributes.
  bool is_markup_path(const std::string &path)
  {
    return in_list(extension_of(path),
                   {".html",
                    ".htm",
                    ".xhtml",
                    ".jsx",
                    ".tsx",
                    ".vue",
                    ".svelte",
                    ".astro",
                    ".php",
                    ".erb",
                    ".ejs",
                    ".hbs",
                    ".handlebars",
                    ".twig",
                    ".njk",
                    ".mustache"});
  }

  // A file whose text is CSS: custom properties are declared here, and the
  // selectors name classes.
  bool is_style_path(const std::string &path)
  {
    return in_list(extension_of(path),
                   {".css", ".scss", ".sass", ".less", ".pcss", ".styl", ".stylus"});
  }

  void Index::sort_unique()
  {
    for (std::vector<std::string> *list : {&classes, &css_vars})
    {
      std::sort(list->begin(), list->end());
      list->erase(std::unique(list->begin(), list->end()), list->end());
    }
  }

  void extract_from_css(const std::string &text, Index &out)
  {
    std::size_t i = 0;
    while (i < text.size())
    {
      const char c = text[i];
      // A comment is not a declaration: content in one must not become a name.
      if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
      {
        const std::size_t end = text.find("*/", i + 2);
        i = end == std::string::npos ? text.size() : end + 2;
        continue;
      }
      // A string is not a declaration either (`content: "--x"`).
      if (c == '"' || c == '\'')
      {
        std::size_t j = i + 1;
        while (j < text.size() && text[j] != c)
        {
          j += (text[j] == '\\' && j + 1 < text.size()) ? 2 : 1;
        }
        i = j < text.size() ? j + 1 : text.size();
        continue;
      }
      if (c == '-' && i + 1 < text.size() && text[i + 1] == '-' && i + 2 < text.size()
          && is_ident_start(text[i + 2]))
      {
        std::size_t j = i + 2;
        while (j < text.size() && is_name_char(text[j]))
        {
          j++;
        }
        // Only a declaration counts, so the name has to be followed by a colon.
        std::size_t k = j;
        while (k < text.size() && std::isspace((unsigned char)text[k]))
        {
          k++;
        }
        if (k < text.size() && text[k] == ':')
        {
          out.css_vars.push_back(text.substr(i + 2, j - (i + 2)));
        }
        i = j;
        continue;
      }
      i++;
    }
  }

  void extract_from_markup(const std::string &text, Index &out)
  {
    std::size_t i = 0;
    while (i < text.size())
    {
      const char c = text[i];
      if (c == '<' && i + 3 < text.size() && text[i + 1] == '!' && text[i + 2] == '-'
          && text[i + 3] == '-')
      {
        const std::size_t end = text.find("-->", i + 4);
        i = end == std::string::npos ? text.size() : end + 3;
        continue;
      }
      if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
      {
        const std::size_t end = text.find("*/", i + 2);
        i = end == std::string::npos ? text.size() : end + 2;
        continue;
      }
      if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
      {
        const std::size_t end = text.find('\n', i + 2);
        i = end == std::string::npos ? text.size() : end + 1;
        continue;
      }
      if (!is_ident_start(c) || (i > 0 && is_attr_name_char(text[i - 1])))
      {
        i++;
        continue;
      }

      std::size_t j = i;
      while (j < text.size() && is_attr_name_char(text[j]))
      {
        j++;
      }
      const std::string name = text.substr(i, j - i);
      const bool class_attribute =
          equals_ascii_ci(name, "class") || equals_ascii_ci(name, "classname");
      if (!class_attribute)
      {
        i = j;
        continue;
      }

      std::size_t k = j;
      while (k < text.size() && std::isspace((unsigned char)text[k]))
      {
        k++;
      }
      // `class=` with something other than a quoted literal after it is a
      // template or a JSX expression; the names are not in the file text.
      if (k >= text.size() || text[k] != '=')
      {
        i = j;
        continue;
      }
      k++;
      while (k < text.size() && std::isspace((unsigned char)text[k]))
      {
        k++;
      }
      if (k >= text.size() || (text[k] != '"' && text[k] != '\''))
      {
        i = j;
        continue;
      }
      const char quote = text[k];
      k++;
      const std::size_t value_start = k;
      while (k < text.size() && text[k] != quote && text[k] != '\n')
      {
        k++;
      }
      add_class_list(text.substr(value_start, k - value_start), out.classes);
      i = k < text.size() ? k + 1 : text.size();
    }
  }

  void extract_selectors(const std::string &text, Index &out)
  {
    std::size_t i = 0;
    while (i < text.size())
    {
      const char c = text[i];
      if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
      {
        const std::size_t end = text.find("*/", i + 2);
        i = end == std::string::npos ? text.size() : end + 2;
        continue;
      }
      if (c != '.' || i + 1 >= text.size() || !is_ident_start(text[i + 1]))
      {
        i++;
        continue;
      }
      // A `.` is a class selector only where a selector can start. After a
      // digit it is a decimal point, after an identifier it is a member access
      // (`a.b`), after another `.` it is a range operator (`1..2`) -- none of
      // which names a class.
      if (i > 0)
      {
        const char prev = text[i - 1];
        if (is_ident_char(prev) || prev == '.' || prev == '%' || prev == '#')
        {
          i++;
          continue;
        }
      }
      std::size_t j = i + 1;
      while (j < text.size() && is_name_char(text[j]))
      {
        j++;
      }
      out.classes.push_back(text.substr(i + 1, j - i - 1));
      i = j;
    }
  }

  void extract_from_file_text(const std::string &path, const std::string &text, Index &out)
  {
    if (is_markup_path(path))
    {
      extract_from_markup(text, out);
    }
    if (is_style_path(path) || is_hybrid_path(path))
    {
      extract_from_css(text, out);
      extract_selectors(text, out);
    }
  }

  bool is_scanned_path(const std::string &path)
  {
    return is_markup_path(path) || is_style_path(path);
  }

  Context context_at(const std::string &line, int col, bool css)
  {
    const int end = std::clamp(col, 0, (int)line.size());

    if (css)
    {
      // The token the caret ends, or a `var(` with nothing typed after it yet.
      int start = end;
      while (start > 0 && is_name_char(line[start - 1]))
      {
        start--;
      }
      if (end - start >= 2 && line[start] == '-' && line[start + 1] == '-')
      {
        return Context::CssVar;
      }
      int open = -1;
      for (int i = end - 1; i >= 0; i--)
      {
        if (line[i] == ')')
        {
          return Context::None; // this call is already closed
        }
        if (line[i] == '(')
        {
          open = i;
          break;
        }
      }
      if (open >= 3 && equals_ascii_ci(line.substr(open - 3, 3), "var")
          && (open - 3 == 0 || !is_name_char(line[open - 4])))
      {
        return Context::CssVar;
      }
      return Context::None;
    }

    // Markup: walk the line forward so a quote that has already closed cannot
    // be mistaken for the one the caret sits inside, and remember the attribute
    // name each quote belongs to. The caret is in a class value only when the
    // attribute whose value it is, is the class one.
    bool in_quote = false;
    char quote = 0;
    std::string attribute;
    int i = 0;
    while (i < end)
    {
      const char c = line[i];
      if (in_quote)
      {
        if (c == quote)
        {
          in_quote = false;
          attribute.clear();
        }
        else if (c == '\\' && i + 1 < end)
        {
          i += 2;
          continue;
        }
        i++;
        continue;
      }
      if (c == '"' || c == '\'')
      {
        in_quote = true;
        quote = c;
        i++;
        continue;
      }
      if (is_ident_start(c) || c == '@' || c == ':' || c == '-')
      {
        const int name_start = i;
        while (i < end && is_attr_name_char(line[i]))
        {
          i++;
        }
        attribute = line.substr(name_start, i - name_start);
        continue;
      }
      i++;
    }

    if (in_quote
        && (equals_ascii_ci(attribute, "class") || equals_ascii_ci(attribute, "classname")))
    {
      return Context::ClassName;
    }
    return Context::None;
  }

  Index scan_workspace(const std::string &root, const ScanLimits &limits)
  {
    Index index;
    if (root.empty())
    {
      return index;
    }

    std::error_code ec;
    fs::path root_path = fs::absolute(fs::path(root), ec).lexically_normal();
    if (ec || !fs::is_directory(root_path, ec))
    {
      return index;
    }

    std::vector<std::string> paths;
    fs::recursive_directory_iterator it(
        root_path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end && paths.size() < limits.max_files; it.increment(ec))
    {
      const fs::directory_entry &entry = *it;
      if (entry.is_directory(ec))
      {
        if (should_skip_dir(entry.path().filename().string()))
        {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (!entry.is_regular_file(ec) || !is_scanned_path(entry.path().string()))
      {
        continue;
      }
      paths.push_back(entry.path().string());
    }
    std::sort(paths.begin(), paths.end()); // a stable index, whatever the walk order

    for (const std::string &path : paths)
    {
      const auto size = fs::file_size(path, ec);
      if (ec || size > limits.max_file_bytes)
      {
        continue;
      }
      std::ifstream in(path, std::ios::binary);
      if (!in.good())
      {
        continue;
      }
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      extract_from_file_text(path, text, index);
    }

    index.sort_unique();
    return index;
  }
} // namespace WebCompletion
