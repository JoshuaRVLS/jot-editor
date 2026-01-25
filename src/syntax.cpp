#include "editor.h"
#include <new>
#include <regex>

void SyntaxHighlighter::set_language(const std::string &ext) {
  file_extension = ext;
  rules.clear();

  if (ext == ".cpp" || ext == ".h" || ext == ".c" || ext == ".hpp") {
    // Keywords
    rules.push_back(
        {std::regex("\\b(int|char|void|float|double|bool|long|short|unsigned|"
                    "signed|const|static|struct|class|namespace|public|private|"
                    "protected|virtual|override|return|if|else|for|while|do|"
                    "switch|case|break|continue|sizeof|typedef|using|template|"
                    "typename|auto|nullptr|new|delete|try|catch|throw|enum|"
                    "union|friend|explicit|operator|this)\\b"),
         1}); // Keyword color

    // Preprocessor and Includes
    rules.push_back({std::regex("#include\\s*[<\"][^>\"]+[>\"]"),
                     6}); // Cyan for entire include
    rules.push_back(
        {std::regex("#[a-z]+"), 5}); // Magenta for #define, #ifdef, etc.

    rules.push_back({std::regex("\"[^\"]*\""), 2});
    rules.push_back({std::regex("'[^']*'"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back(
        {std::regex("/\\*([^*]|[\\r\\n]|(\\*+([^*/]|[\\r\\n])))*\\*+/"),
         3}); // Block comment (simple regex)
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});
    rules.push_back({std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\s*\\("),
                     6}); // Function calls -> Cyan

  } else if (ext == ".py") {
    // Distinct colors for import/from/as
    rules.push_back({std::regex("\\b(import|from|as)\\b"), 5}); // Magenta

    rules.push_back(
        {std::regex(
             "\\b(def|class|if|elif|else|for|while|return|try|"
             "except|finally|with|lambda|yield|assert|break|continue|pass|"
             "raise|global|nonlocal|True|False|None|and|or|not|in|is|self)\\b"),
         1});
    rules.push_back(
        {std::regex("\"\"\"[^\"]*\"\"\"|'''[^']*'''|\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});
    rules.push_back({std::regex("@[a-zA-Z0-9_]+"), 6}); // Decorators

  } else if (ext == ".js" || ext == ".ts") {
    rules.push_back(
        {std::regex("\\b(import|from|export|require)\\b"), 5}); // Imports

    rules.push_back(
        {std::regex(
             "\\b(var|let|const|function|return|if|else|for|while|do|switch|"
             "case|break|continue|class|extends|async|await|"
             "try|catch|finally|throw|new|typeof|instanceof|this|super|static|"
             "get|set|yield|void|null|undefined|true|false)\\b"),
         1});
    rules.push_back({std::regex("`[^`]*`|\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});

  } else if (ext == ".html" || ext == ".xml") {
    rules.push_back({std::regex("<[^>]*>"), 1});
    rules.push_back({std::regex("\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("<!--.*?-->"), 3});

  } else if (ext == ".rs") {
    rules.push_back({std::regex("\\b(use|mod|crate|extern)\\b"), 5});

    rules.push_back(
        {std::regex(
             "\\b(fn|let|mut|const|static|struct|enum|impl|trait|type|pub|"
             "self|super|if|else|match|for|while|loop|return|break|"
             "continue|async|await|move|ref|where|unsafe|as|dyn)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\""), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});

  } else if (ext == ".css") {
    rules.push_back(
        {std::regex(
             "\\b(body|div|span|h[1-6]|p|a|ul|ol|li|table|tr|td|th|form|input|"
             "button|img|header|footer|nav|section|article|aside)\\b"),
         1});
    rules.push_back({std::regex("[a-zA-Z0-9-]+\\s*:"), 5}); // Properties
    rules.push_back({std::regex("\\.[a-zA-Z0-9_-]+"), 5});  // Classes
    rules.push_back({std::regex("#[a-zA-Z0-9_-]+"), 4});    // IDs
    rules.push_back({std::regex("/\\*.*?\\*/"), 3});        // Comments
    rules.push_back({std::regex("\\b[0-9]+(px|em|rem|%|vh|vw|s|ms)?\\b"), 4});

  } else if (ext == ".java") {
    rules.push_back({std::regex("\\b(import|package)\\b"), 5});

    rules.push_back(
        {std::regex(
             "\\b(public|private|protected|class|interface|enum|extends|"
             "implements|static|final|void|int|double|float|boolean|char|byte|"
             "short|long|if|else|for|while|do|switch|case|break|continue|"
             "return|"
             "try|catch|finally|throw|throws|new|this|super|"
             "synchronized|volatile|transient|native|abstract|default)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\""), 2});
    rules.push_back({std::regex("'[^']*'"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});
    rules.push_back({std::regex("@\\w+"), 6}); // Annotations

  } else if (ext == ".go") {
    rules.push_back({std::regex("\\b(package|import)\\b"), 5});

    rules.push_back(
        {std::regex("\\b(func|type|struct|interface|map|chan|go|"
                    "defer|if|else|for|range|return|break|continue|switch|case|"
                    "default|select|var|const|fallthrough|goto)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\"|`[^`]*`"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("\\b[0-9]+\\b"), 4});
    rules.push_back({std::regex("\\b(true|false|nil|iota)\\b"), 6});

  } else if (ext == ".md") {
    rules.push_back({std::regex("^#+ .*"), 5});                  // Headers
    rules.push_back({std::regex("\\*\\*.*?\\*\\*|__.*?__"), 1}); // Bold
    rules.push_back({std::regex("\\*.*?\\*|_.*?_"), 6});         // Italic
    rules.push_back({std::regex("`[^`]*`"), 2});                 // Code
    rules.push_back({std::regex("\\[.*?\\]\\(.*?\\)"), 4});      // Links
    rules.push_back({std::regex("^\\s*[-*+] "), 1});             // Lists
    rules.push_back({std::regex("^\\s*\\d+\\. "), 1}); // Ordered lists
    rules.push_back({std::regex("<!--.*?-->"), 3});    // Comments

  } else if (ext == ".json") {
    rules.push_back({std::regex("\"[^\"]*\":"), 5}); // Keys
    rules.push_back({std::regex("\"[^\"]*\""), 2});  // Strings
    rules.push_back({std::regex("\\b(true|false|null)\\b"), 1});
    rules.push_back({std::regex("\\b-?[0-9]+(\\.[0-9]+)?\\b"), 4});

  } else if (ext == ".sh" || ext == ".bash" || ext == ".zsh") {
    rules.push_back(
        {std::regex(
             "\\b(if|then|else|elif|fi|case|esac|for|while|until|do|done|"
             "in|function|return|exit|export|local|echo|read|source)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\$\\{?[a-zA-Z0-9_]+\\}?"), 5}); // Variables

  } else if (ext == ".rb") {
    rules.push_back({std::regex("\\b(require|include|extend)\\b"), 5});

    rules.push_back(
        {std::regex(
             "\\b(def|end|class|module|if|else|elsif|unless|while|until|for|in|"
             "do|yield|return|break|next|redo|retry|ensure|rescue|case|when|"
             "then|"
             "begin|super|alias|defined\\?|self|true|false|nil)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex(":[a-zA-Z0-9_]+"), 5}); // Symbols
    rules.push_back({std::regex("@[a-zA-Z0-9_]+"), 6}); // Instance vars

  } else if (ext == ".php") {
    rules.push_back({std::regex("\\b(use|namespace|require|include)\\b"), 5});

    rules.push_back(
        {std::regex(
             "\\b(php|echo|function|class|public|private|protected|static|if|"
             "else|elseif|for|foreach|while|do|switch|case|break|continue|"
             "return|"
             "try|catch|finally|throw|new|extends|implements|interface|trait|"
             "null|true|false)\\b"),
         1});
    rules.push_back({std::regex("\"[^\"]*\"|'[^']*'"), 2});
    rules.push_back({std::regex("//.*|/\\*.*?\\*/|#.*"), 3});
    rules.push_back({std::regex("\\$[a-zA-Z0-9_]+"), 5}); // Variables
  }
}

std::vector<std::pair<int, int>>
SyntaxHighlighter::get_colors(const std::string &line) {
  std::vector<std::pair<int, int>> colors(line.length(), {0, 0});

  for (const auto &rule : rules) {
    auto words_begin =
        std::sregex_iterator(line.begin(), line.end(), rule.pattern);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
      std::smatch match = *i;
      for (size_t pos = match.position();
           pos < match.position() + match.length() && pos < line.length();
           pos++) {
        colors[pos] = {1, rule.color};
      }
    }
  }

  return colors;
}
