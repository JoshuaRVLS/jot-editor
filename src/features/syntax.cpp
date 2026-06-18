#include "syntax_highlighter.h"
#include "tree_sitter/manager.h"
#include <algorithm>
#include <cstring>
#include <regex>

namespace {

struct CaptureStyle {
  int token = TS_TOKEN_NONE;
  int priority = 0;
};

CaptureStyle capture_style_for_name(std::string_view sv) {
  auto has_prefix = [&](const char *pfx) {
    size_t n = strlen(pfx);
    return sv.size() >= n && sv.compare(0, n, pfx) == 0;
  };

  if (sv == "comment" || has_prefix("comment.")) return {TS_TOKEN_COMMENT, 120};
  if (sv == "string.escape" || sv == "escape") {
    return {TS_TOKEN_STRING_ESCAPE, 118};
  }
  if (sv == "string" || has_prefix("string.") || sv == "character" ||
      has_prefix("character.")) {
    return {TS_TOKEN_STRING, 115};
  }
  if (sv == "punctuation.bracket") {
    return {TS_TOKEN_PUNCTUATION_BRACKET, 108};
  }
  if (sv == "punctuation.delimiter") {
    return {TS_TOKEN_PUNCTUATION_DELIMITER, 107};
  }
  if (sv == "punctuation" || has_prefix("punctuation.")) {
    return {TS_TOKEN_PUNCTUATION, 105};
  }
  if (sv == "function" || has_prefix("function.") ||
      sv == "method" || has_prefix("method.") || sv == "constructor") {
    if (sv == "function.builtin" || sv == "method.builtin") {
      return {TS_TOKEN_BUILTIN, 98};
    }
    if (sv == "function.method" || sv == "method" ||
        has_prefix("method.")) {
      return {TS_TOKEN_FUNCTION_METHOD, 97};
    }
    if (sv == "function.constructor" || sv == "constructor") {
      return {TS_TOKEN_FUNCTION_CONSTRUCTOR, 96};
    }
    return {TS_TOKEN_FUNCTION, 95};
  }
  if (sv == "variable.parameter" || sv == "parameter" ||
      has_prefix("parameter.")) {
    return {TS_TOKEN_PARAMETER, 90};
  }
  if (sv == "variable.member" || sv == "property" ||
      has_prefix("property.") || sv == "field" || has_prefix("field.")) {
    return {TS_TOKEN_FIELD, 88};
  }
  if (sv == "attribute" || has_prefix("attribute.") ||
      sv == "tag.attribute") {
    return {TS_TOKEN_ATTRIBUTE, 91};
  }
  if (sv == "tag" || has_prefix("tag.")) {
    return {TS_TOKEN_TAG, 86};
  }
  if (sv == "type" || has_prefix("type.")) {
    if (sv == "type.builtin") {
      return {TS_TOKEN_TYPE_BUILTIN, 84};
    }
    return {TS_TOKEN_TYPE, 82};
  }
  if (sv == "namespace" || has_prefix("namespace.")) {
    return {TS_TOKEN_NAMESPACE, 80};
  }
  if (sv == "module" || has_prefix("module.")) {
    return {TS_TOKEN_MODULE, 78};
  }
  if (sv == "keyword.control" || has_prefix("keyword.control.")) {
    return {TS_TOKEN_KEYWORD_CONTROL, 79};
  }
  if (sv == "keyword.storage" || has_prefix("keyword.storage.") ||
      sv == "keyword.modifier" || has_prefix("keyword.modifier.")) {
    return {TS_TOKEN_KEYWORD_STORAGE, 78};
  }
  if (sv == "keyword.directive" || has_prefix("keyword.directive.") ||
      sv == "keyword.import" || sv == "preproc" || has_prefix("preproc.")) {
    return {TS_TOKEN_KEYWORD_PREPROC, 77};
  }
  if (sv == "keyword" || has_prefix("keyword.") || sv == "operator") {
    if (sv == "operator") {
      return {TS_TOKEN_OPERATOR, 76};
    }
    return {TS_TOKEN_KEYWORD, 74};
  }
  if (sv == "number" || has_prefix("number.") || sv == "float" ||
      sv == "constant.numeric") {
    return {TS_TOKEN_NUMBER, 72};
  }
  if (sv == "constant.builtin" || sv == "boolean" || sv == "none" ||
      sv == "null") {
    return {TS_TOKEN_BUILTIN, 70};
  }
  if (sv == "constant.macro" || sv == "constant.predefined" ||
      sv == "constant.define") {
    return {TS_TOKEN_CONSTANT_MACRO, 69};
  }
  if (sv == "constant" || has_prefix("constant.")) {
    return {TS_TOKEN_CONSTANT, 68};
  }
  if (sv == "variable" || has_prefix("variable.") || sv == "identifier") {
    return {TS_TOKEN_VARIABLE, 45};
  }
  return {};
}

} // namespace

int tree_sitter_capture_color_for_name(const std::string &name) {
  return capture_style_for_name(name).token;
}

int tree_sitter_capture_token_for_name(const std::string &name) {
  return capture_style_for_name(name).token;
}

int tree_sitter_capture_priority_for_name(const std::string &name) {
  return capture_style_for_name(name).priority;
}

void SyntaxHighlighter::set_language(const std::string &ext) {
  if (file_extension == ext)
    return;
  file_extension = ext;
  rules.clear();

  if (ext == ".cpp" || ext == ".h" || ext == ".c" || ext == ".hpp" ||
      ext == ".cc" || ext == ".cxx" || ext == ".hh" || ext == ".hxx") {
    // Keywords
    rules.push_back(
        {std::regex("\\b(int|char|void|float|double|bool|long|short|unsigned|"
                    "signed|const|static|struct|class|namespace|public|private|"
                    "protected|virtual|override|final|return|if|else|for|while|"
                    "do|switch|case|break|continue|sizeof|typedef|using|template|"
                    "typename|auto|nullptr|new|delete|try|catch|throw|enum|union|"
                    "friend|explicit|operator|this|constexpr|consteval|constinit|"
                    "noexcept|concept|requires|co_await|co_return|co_yield)\\b"),
         1}); // Keyword color

    // Preprocessor and Includes
    rules.push_back({std::regex("#\\s*include\\s*[<\"][^>\"]+[>\"]"),
                     6}); // Cyan for entire include
    rules.push_back({std::regex("#\\s*[a-zA-Z_]+"),
                     5}); // Magenta for #define, #ifdef, etc.

    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("/\\*.*\\*/"), 3}); // single-line block chunk
    rules.push_back(
        {std::regex("\\b(0x[0-9a-fA-F]+|0b[01]+|\\d+\\.\\d+([eE][+-]?\\d+)?|"
                    "\\d+[eE][+-]?\\d+|\\d+)\\b"),
         4});
    rules.push_back(
        {std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"),
         6}); // Function calls: color only the identifier, not the bracket

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
        {std::regex("\"\"\".*\"\"\"|'''.*'''|\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"),
         2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back(
        {std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+([eE][+-]?\\d+)?|\\d+)\\b"),
         4});
    rules.push_back({std::regex("@[a-zA-Z0-9_]+"), 6}); // Decorators
    rules.push_back(
        {std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"),
         6}); // Calls

  } else if (ext == ".js" || ext == ".ts" || ext == ".jsx" ||
             ext == ".tsx" || ext == ".mjs" || ext == ".cjs") {
    rules.push_back(
        {std::regex("\\b(import|from|export|require)\\b"), 5}); // Imports

    rules.push_back(
        {std::regex(
             "\\b(var|let|const|function|return|if|else|for|while|do|switch|"
              "case|break|continue|class|extends|async|await|"
              "try|catch|finally|throw|new|typeof|instanceof|this|super|static|"
              "get|set|yield|void|null|undefined|true|false|interface|type|"
              "implements|enum|readonly|keyof|infer|satisfies)\\b"),
         1});
    rules.push_back(
        {std::regex("`([^`\\\\]|\\\\.)*`|\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"),
         2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("/\\*.*\\*/"), 3});
    rules.push_back(
        {std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+([eE][+-]?\\d+)?|\\d+)\\b"),
         4});
    rules.push_back(
        {std::regex("\\b[A-Za-z_$][A-Za-z0-9_$]*\\b(?=\\s*\\()"),
         6}); // Calls

  } else if (ext == ".html" || ext == ".xml") {
    rules.push_back({std::regex("<[^>]*>"), 1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("<!--.*?-->"), 3});

  } else if (ext == ".rs") {
    rules.push_back({std::regex("\\b(use|mod|crate|extern)\\b"), 5});

    rules.push_back(
        {std::regex(
             "\\b(fn|let|mut|const|static|struct|enum|impl|trait|type|pub|"
             "self|super|if|else|match|for|while|loop|return|break|"
             "continue|async|await|move|ref|where|unsafe|as|dyn)\\b"),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("/\\*.*\\*/"), 3});
    rules.push_back(
        {std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+([eE][+-]?\\d+)?|\\d+)\\b"),
         4});
    rules.push_back(
        {std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"),
         6}); // Calls

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

  } else if (ext == ".java" || ext == ".kt") {
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
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("//.*"), 3});
    rules.push_back({std::regex("/\\*.*\\*/"), 3});
    rules.push_back(
        {std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+([eE][+-]?\\d+)?|\\d+)\\b"),
         4});
    rules.push_back({std::regex("@\\w+"), 6}); // Annotations
    rules.push_back({std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"), 6});

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

  } else if (ext == ".json" || ext == ".jsonc") {
    rules.push_back({std::regex("\"[^\"]*\":"), 5}); // Keys
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});  // Strings
    rules.push_back({std::regex("\\b(true|false|null)\\b"), 1});
    rules.push_back({std::regex("\\b-?[0-9]+(\\.[0-9]+)?\\b"), 4});
    rules.push_back({std::regex("//.*"), 3});

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
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
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
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("//.*|/\\*.*?\\*/|#.*"), 3});
    rules.push_back({std::regex("\\$[a-zA-Z0-9_]+"), 5}); // Variables
  } else if (ext == ".lua") {
    rules.push_back(
        {std::regex(
             "\\b(local|function|end|if|then|elseif|else|for|while|repeat|"
             "until|do|return|break|goto|and|or|not|nil|true|false|in)\\b"),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("--\\[\\[.*\\]\\]|--.*"), 3});
    rules.push_back({std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+|\\d+)\\b"), 4});
    rules.push_back({std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"), 6});
  } else if (ext == ".swift") {
    rules.push_back(
        {std::regex(
             "\\b(import|class|struct|enum|protocol|extension|func|let|var|"
             "if|else|guard|for|while|repeat|switch|case|default|break|"
             "continue|return|throw|throws|try|catch|defer|where|in|as|is|"
             "nil|true|false|self|super|init|deinit)\\b"),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("//.*|/\\*.*\\*/"), 3});
    rules.push_back({std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+|\\d+)\\b"), 4});
    rules.push_back({std::regex("@[A-Za-z_][A-Za-z0-9_]*"), 5});
    rules.push_back({std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"), 6});
  } else if (ext == ".cs") {
    rules.push_back({std::regex("\\b(using|namespace)\\b"), 5});
    rules.push_back(
        {std::regex(
             "\\b(public|private|protected|internal|class|struct|interface|"
             "enum|record|static|readonly|const|void|int|float|double|decimal|"
             "bool|string|char|byte|short|long|if|else|for|foreach|while|do|"
             "switch|case|break|continue|return|new|this|base|try|catch|"
             "finally|throw|async|await|var|null|true|false)\\b"),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("//.*|/\\*.*\\*/"), 3});
    rules.push_back({std::regex("\\b(0x[0-9a-fA-F]+|\\d+\\.\\d+|\\d+)\\b"), 4});
    rules.push_back({std::regex("@\\w+"), 6});
    rules.push_back({std::regex("\\b[A-Za-z_][A-Za-z0-9_]*\\b(?=\\s*\\()"), 6});
  } else if (ext == ".sql") {
    rules.push_back(
        {std::regex(
             "\\b(select|insert|update|delete|from|where|join|left|right|inner|"
             "outer|on|group|by|order|having|limit|offset|into|values|create|"
             "alter|drop|table|view|index|primary|key|foreign|constraint|"
             "distinct|union|all|as|and|or|not|null|is|in|exists|between|like)\\b",
             std::regex::icase),
         1});
    rules.push_back({std::regex("'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("--.*|/\\*.*\\*/"), 3});
    rules.push_back({std::regex("\\b-?\\d+(\\.\\d+)?\\b"), 4});
  } else if (ext == ".cmake") {
    rules.push_back(
        {std::regex(
             "\\b(if|else|elseif|endif|foreach|endforeach|while|endwhile|"
             "function|endfunction|macro|endmacro|set|unset|option|include|"
             "project|add_executable|add_library|target_link_libraries|"
             "target_include_directories|message|find_package|install)\\b",
             std::regex::icase),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\$\\{[A-Za-z0-9_]+\\}"), 5});
  } else if (ext == ".dockerfile") {
    rules.push_back(
        {std::regex(
             "^(from|run|cmd|entrypoint|env|arg|workdir|copy|add|expose|user|"
             "volume|label|shell|stopsignal|healthcheck|onbuild)\\b",
             std::regex::icase),
         1});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\$\\{?[A-Za-z0-9_]+\\}?"), 5});
  } else if (ext == ".make" || ext == ".mk") {
    rules.push_back({std::regex("^[A-Za-z0-9_./-]+\\s*:"), 5}); // Targets
    rules.push_back(
        {std::regex(
             "\\b(ifdef|ifndef|ifeq|ifneq|else|endif|include|define|endef|"
             "override|export|unexport|vpath)\\b"),
         1});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\$\\([A-Za-z0-9_]+\\)|\\$\\{[A-Za-z0-9_]+\\}"), 6});
  } else if (ext == ".ini" || ext == ".cfg" || ext == ".conf" ||
             ext == ".properties") {
    rules.push_back({std::regex("^\\s*\\[[^\\]]+\\]"), 1}); // Section
    rules.push_back({std::regex("^[^=:#\\s][^=:#]*[=:]"), 5}); // Keys
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\""), 2});
    rules.push_back({std::regex("#.*|;.*"), 3});
  } else if (ext == ".yml" || ext == ".yaml" || ext == ".toml") {
    rules.push_back({std::regex("^[\\t ]*[a-zA-Z0-9_.-]+\\s*:"), 5});
    rules.push_back({std::regex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'"), 2});
    rules.push_back({std::regex("#.*"), 3});
    rules.push_back({std::regex("\\b(true|false|null|on|off|yes|no)\\b"), 1});
    rules.push_back({std::regex("\\b-?[0-9]+(\\.[0-9]+)?\\b"), 4});
  }
}

std::vector<std::pair<int, int>>
SyntaxHighlighter::get_colors(const std::string &line) {
  std::vector<std::pair<int, int>> colors(line.length(), {0, 0});
  std::vector<bool> protected_region(line.length(), false);

  auto apply_rule = [&](const SyntaxRule &rule, bool protect_only,
                        bool skip_protected) {
    auto words_begin = std::sregex_iterator(line.begin(), line.end(), rule.pattern);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
      std::smatch match = *i;
      size_t start = static_cast<size_t>(match.position());
      size_t end = static_cast<size_t>(match.position() + match.length());
      for (size_t pos = start; pos < end && pos < line.length(); pos++) {
        if (skip_protected && protected_region[pos]) {
          continue;
        }
        colors[pos] = {1, rule.color};
        if (protect_only) {
          protected_region[pos] = true;
        }
      }
    }
  };

  // Pass 1: strings/comments first; protect regions from later token rules.
  for (const auto &rule : rules) {
    if (rule.color == 2 || rule.color == 3) {
      apply_rule(rule, true, false);
    }
  }

  // Pass 2: other syntax rules, but don't paint inside protected regions.
  for (const auto &rule : rules) {
    if (rule.color == 2 || rule.color == 3) {
      continue;
    }
    apply_rule(rule, false, true);
  }

  return colors;
}
