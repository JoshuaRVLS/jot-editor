// The C++ definition checker's parser: one file's text in, its function
// signatures out (see cpp_definitions.h for what the checker does with them).
//
// The scan is single-pass and declaration-scoped. It maintains
//
//   * a brace counter and a stack of scopes (namespace / class) so a signature
//     knows its qualified name,
//   * a pending statement buffer that collects the tokens between `;` and `{`,
//     and
//   * a preprocessor conditional stack, so a signature under a `#if` can say so
//     (the include guard is recognized and not counted).
//
// Every `{` at declaration scope is either a function body -- parsed, then
// *skipped whole*, which is why statements inside functions are never mistaken
// for declarations -- or a scope worth reading into (a namespace, a class), or
// something opaque (an `enum`, a brace initializer, a lambda) that is skipped
// the same way. Preprocessor lines are blanked before tokenizing, so a macro
// body never leaks into the declarations around it.
#include "cpp_definitions.h"
#include "base/string_util.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CppDefinitions
{
  namespace
  {
    // --- masking ------------------------------------------------------------

    // Comments and literal bodies are replaced by spaces (newlines kept) so the
    // tokenizer only ever sees code. The `"` / `'` delimiters survive as one
    // token each, which is what makes `Foo x("a");` readable as an initializer
    // and `void f(const char *s);` as a declaration.
    std::string mask_non_code(const std::string &text)
    {
      std::string out = text;
      const size_t n = text.size();
      size_t i = 0;
      while (i < n)
      {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/')
        {
          while (i < n && text[i] != '\n')
          {
            out[i++] = ' ';
          }
          continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*')
        {
          out[i] = ' ';
          out[i + 1] = ' ';
          i += 2;
          while (i < n)
          {
            if (text[i] == '*' && i + 1 < n && text[i + 1] == '/')
            {
              out[i] = ' ';
              out[i + 1] = ' ';
              i += 2;
              break;
            }
            if (text[i] != '\n')
            {
              out[i] = ' ';
            }
            i++;
          }
          continue;
        }
        // A raw string literal -- R"delim(...)delim" -- has no escapes: every
        // `"` inside it is content. The ordinary branch below would close it at
        // the first one and then read the rest of the literal -- embedded JSON,
        // GLSL, SQL -- as code, so it is handled here. The prefix (R, uR, UR,
        // LR, u8R) ends in the `R` directly before the quote.
        if (c == '"' && i > 0 && text[i - 1] == 'R'
            && (i < 2
                || (std::isalnum((unsigned char)text[i - 2]) == 0 && text[i - 2] != '_')))
        {
          const size_t open = text.find('(', i + 1);
          // The delimiter is at most 16 characters and may be empty.
          if (open != std::string::npos && open - i - 1 <= 16)
          {
            const std::string terminator = ")" + text.substr(i + 1, open - i - 1) + "\"";
            const size_t close = text.find(terminator, open + 1);
            const size_t stop = close == std::string::npos ? n : close + terminator.size();
            for (size_t k = i; k < stop; k++)
            {
              if (text[k] != '\n')
              {
                out[k] = ' ';
              }
            }
            i = stop;
            continue;
          }
        }
        if (c == '"' || c == '\'')
        {
          const char quote = c;
          i++;
          while (i < n)
          {
            if (text[i] == '\\' && i + 1 < n)
            {
              out[i] = ' ';
              out[i + 1] = ' ';
              i += 2;
              continue;
            }
            if (text[i] == quote)
            {
              out[i] = ' ';
              i++;
              break;
            }
            if (text[i] != '\n')
            {
              out[i] = ' ';
            }
            i++;
          }
          continue;
        }
        i++;
      }
      return out;
    }

    // Blanks every preprocessor line, continuations included, so the parser
    // never sees a macro's tokens. Runs on already-masked text, so a `#` inside
    // a string or a comment has already become a space.
    std::string blank_directives(const std::string &text)
    {
      std::string out = text;
      size_t pos = 0;
      while (pos < text.size())
      {
        size_t line_end = text.find('\n', pos);
        if (line_end == std::string::npos)
        {
          line_end = text.size();
        }
        size_t first = pos;
        while (first < line_end && std::isspace((unsigned char)text[first]) != 0)
        {
          first++;
        }
        if (first < line_end && text[first] == '#')
        {
          size_t end = line_end;
          for (;;)
          {
            size_t back = end;
            while (back > pos && text[back - 1] != '\n'
                   && std::isspace((unsigned char)text[back - 1]) != 0)
            {
              back--;
            }
            if (back > pos && text[back - 1] == '\\')
            {
              const size_t next_nl = text.find('\n', end + 1);
              if (next_nl == std::string::npos)
              {
                end = text.size();
                break;
              }
              end = next_nl;
              continue;
            }
            break;
          }
          for (size_t i = pos; i < end; i++)
          {
            if (out[i] != '\n')
            {
              out[i] = ' ';
            }
          }
          pos = end < text.size() ? end + 1 : text.size();
          continue;
        }
        pos = line_end < text.size() ? line_end + 1 : text.size();
      }
      return out;
    }

    // --- tokens -------------------------------------------------------------

    struct Token
    {
      enum Kind
      {
        Word,
        Number,
        Literal,
        Punct
      };
      Kind kind = Punct;
      std::string text;
      int line = 0;
      int col = 0;
    };

    bool word_start(char c)
    {
      return std::isalpha((unsigned char)c) != 0 || c == '_';
    }

    bool word_char(char c)
    {
      return std::isalnum((unsigned char)c) != 0 || c == '_';
    }

    std::vector<Token> tokenize(const std::string &masked)
    {
      // Multi-character tokens the parser cares about. `<` and `>` are emitted
      // singly, so the `>>` that closes two template lists reads as two closers.
      static const char *kMulti[] = {"::", "->", "...", "[[", "]]", "&&", "||", ".*", "->*"};

      std::vector<Token> out;
      int line = 0;
      int col = 0;
      for (size_t i = 0; i < masked.size();)
      {
        const char c = masked[i];
        if (c == '\n')
        {
          line++;
          col = 0;
          i++;
          continue;
        }
        if (std::isspace((unsigned char)c) != 0)
        {
          col++;
          i++;
          continue;
        }

        Token t;
        t.line = line;
        t.col = col;
        if (word_start(c))
        {
          const size_t start = i;
          while (i < masked.size() && word_char(masked[i]))
          {
            i++;
            col++;
          }
          t.kind = Token::Word;
          t.text = masked.substr(start, i - start);
        }
        else if (std::isdigit((unsigned char)c) != 0
                 || (c == '.' && i + 1 < masked.size()
                     && std::isdigit((unsigned char)masked[i + 1]) != 0))
        {
          const size_t start = i;
          while (i < masked.size()
                 && (word_char(masked[i]) || masked[i] == '.' || masked[i] == '\''))
          {
            i++;
            col++;
          }
          t.kind = Token::Number;
          t.text = masked.substr(start, i - start);
        }
        else if (c == '"' || c == '\'')
        {
          t.kind = Token::Literal;
          t.text = std::string(1, c);
          i++;
          col++;
        }
        else
        {
          bool matched = false;
          for (const char *multi : kMulti)
          {
            const size_t len = std::string(multi).size();
            if (masked.compare(i, len, multi) == 0)
            {
              t.text = multi;
              i += len;
              col += (int)len;
              matched = true;
              break;
            }
          }
          if (!matched)
          {
            t.text = std::string(1, c);
            i++;
            col++;
          }
        }
        out.push_back(std::move(t));
      }
      return out;
    }

    // --- vocabulary ---------------------------------------------------------

    bool is_keyword(const std::string &w)
    {
      static const std::unordered_set<std::string> words = {
          // statements, and words whose paren group is not a parameter list
          "if", "else", "for", "while", "do", "switch", "case", "default", "break",
          "continue", "return", "goto", "sizeof", "alignof", "alignas", "decltype",
          "typeid", "static_assert", "noexcept", "throw", "new", "delete", "this",
          "co_await", "co_return", "co_yield", "requires", "concept", "asm",
          // types and declaration specifiers
          "void", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short",
          "int", "long", "float", "double", "signed", "unsigned", "auto", "typename",
          "class", "struct", "union", "enum", "namespace", "template", "using",
          "typedef", "const", "volatile", "mutable", "static", "register",
          "thread_local", "extern", "inline", "virtual", "explicit", "friend",
          "public", "private", "protected", "true", "false", "nullptr", "override",
          "final"};
      // The context-sensitive keywords (`module`, `import`, `export`, `concept`)
      // deliberately stay out of the list: they are legal parameter names, and a
      // parameter is what this set is mostly asked about.
      return words.count(w) != 0;
    }

    // Words that can precede a paren group of their own inside a declaration;
    // that group is skipped rather than read as a parameter list.
    bool is_paren_head_word(const std::string &w)
    {
      static const std::unordered_set<std::string> words = {
          "static_assert", "sizeof", "alignof", "decltype", "typeid", "__typeof__",
          "__attribute__", "__declspec", "requires", "assert", "alignas"};
      return words.count(w) != 0;
    }

    bool is_operator_name(const std::string &w)
    {
      return w.rfind("operator", 0) == 0;
    }

    bool is_leading_specifier(const std::string &w)
    {
      static const std::unordered_set<std::string> words = {
          "inline", "constexpr", "consteval", "constinit", "static", "virtual", "explicit",
          "friend", "extern", "mutable", "thread_local", "register"};
      return words.count(w) != 0;
    }

    // The spellings an overloaded operator can be written with.
    bool is_operator_symbol(const std::string &t)
    {
      static const std::unordered_set<std::string> symbols = {
          "+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">", "+=", "-=",
          "*=", "/=", "%=", "^=", "&=", "|=", "==", "!=", "<=", ">=", "&&", "||", "++",
          "--", ",", "->", "->*"};
      return symbols.count(t) != 0;
    }

    // --- small token walks --------------------------------------------------

    // One past the token closing the opener at `open`.
    size_t skip_balanced(const std::vector<Token> &s, size_t open)
    {
      const std::string &o = s[open].text;
      const std::string c = o == "(" ? ")" : (o == "[" ? "]" : "}");
      int depth = 0;
      for (size_t i = open; i < s.size(); i++)
      {
        if (s[i].text == o)
        {
          depth++;
        }
        else if (s[i].text == c)
        {
          depth--;
          if (depth == 0)
          {
            return i + 1;
          }
        }
      }
      return s.size();
    }

    // One past the `>` closing the `<` at `open`.
    size_t skip_angles(const std::vector<Token> &s, size_t open)
    {
      int depth = 0;
      for (size_t i = open; i < s.size(); i++)
      {
        if (s[i].text == "<")
        {
          depth++;
        }
        else if (s[i].text == ">")
        {
          depth--;
          if (depth == 0)
          {
            return i + 1;
          }
        }
      }
      return s.size();
    }

    // One past the `]]` closing the `[[` at `open`.
    size_t skip_attributes(const std::vector<Token> &s, size_t open)
    {
      int depth = 0;
      for (size_t i = open; i < s.size(); i++)
      {
        if (s[i].text == "[[")
        {
          depth++;
        }
        else if (s[i].text == "]]")
        {
          depth--;
          if (depth == 0)
          {
            return i + 1;
          }
        }
      }
      return s.size();
    }

    // A `<` after a name (or `>`, or `::`) opens a template argument list. A
    // less-than cannot appear in a declaration at this level, so this one rule
    // is enough to keep `std::map<int, int>` together and `std::function<void(int)>`
    // from looking like a call.
    bool opens_angle(const std::vector<Token> &s, size_t i)
    {
      if (s[i].text != "<" || i == 0)
      {
        return false;
      }
      const Token &prev = s[i - 1];
      if (prev.text == "operator")
      {
        return false;
      }
      return prev.kind == Token::Word || prev.text == ">" || prev.text == "::";
    }

    // Tokens joined back into text, spaced the one way a signature needs: two
    // adjacent words (or a word and a number) keep a space, everything else is
    // tight, so `const char *` and `const char*` come out the same.
    std::string join_tokens(const std::vector<Token> &s, size_t begin, size_t end)
    {
      std::string out;
      const Token *prev = nullptr;
      for (size_t i = begin; i < end && i < s.size(); i++)
      {
        const Token &t = s[i];
        if (!out.empty() && prev != nullptr && prev->kind == Token::Word
            && (t.kind == Token::Word || t.kind == Token::Number))
        {
          out += ' ';
        }
        out += t.text;
        prev = &t;
      }
      return out;
    }

    // Collapse `operator` and its spelling into a single word token
    // ("operator=", "operator[]", "operator bool") so the name walk only ever
    // has to look at words. The merged token keeps `operator`'s position.
    std::vector<Token> merge_operator_names(const std::vector<Token> &in)
    {
      std::vector<Token> out;
      for (size_t i = 0; i < in.size();)
      {
        if (in[i].kind != Token::Word || in[i].text != "operator" || i + 1 >= in.size())
        {
          out.push_back(in[i]);
          i++;
          continue;
        }

        Token merged = in[i];
        size_t j = i + 1;
        if (j + 1 < in.size() && in[j].text == "(" && in[j + 1].text == ")")
        {
          merged.text = "operator()";
          j += 2;
        }
        else if (j + 1 < in.size() && in[j].text == "[" && in[j + 1].text == "]")
        {
          merged.text = "operator[]";
          j += 2;
        }
        else if (in[j].kind == Token::Word)
        {
          merged.text = "operator " + in[j].text;
          j++;
          while (j + 1 < in.size() && in[j].text == "::" && in[j + 1].kind == Token::Word)
          {
            merged.text += "::" + in[j + 1].text;
            j += 2;
          }
          if (j + 1 < in.size() && in[j].text == "[" && in[j + 1].text == "]")
          {
            merged.text += "[]";
            j += 2;
          }
        }
        else if (is_operator_symbol(in[j].text))
        {
          merged.text = "operator";
          while (j < in.size() && is_operator_symbol(in[j].text))
          {
            merged.text += in[j].text;
            j++;
          }
        }
        else
        {
          // A spelling we do not model (a literal-suffix operator): leave it, the
          // name walk rejects the statement on its own.
          out.push_back(in[i]);
          i++;
          continue;
        }
        out.push_back(std::move(merged));
        i = j;
      }
      return out;
    }

    // --- the preprocessor ---------------------------------------------------

    struct Directive
    {
      enum Kind
      {
        If,
        Ifdef,
        Ifndef,
        Elif,
        Else,
        Endif,
        Define,
        Other
      };
      Kind kind = Other;
      std::string arg;
      int line = 0;
    };

    // Directives with their line number, continuations folded in: a `#define`
    // split over five lines is one directive on the line it starts.
    std::vector<Directive> scan_directives(const std::string &text)
    {
      std::vector<Directive> out;
      std::string line_text;
      int line_no = 0;
      size_t pos = 0;
      while (pos <= text.size())
      {
        const size_t nl = text.find('\n', pos);
        const bool last = nl == std::string::npos;
        const std::string piece = last ? text.substr(pos) : text.substr(pos, nl - pos);
        pos = last ? text.size() + 1 : nl + 1;

        line_text += piece;
        if (!line_text.empty() && line_text.back() == '\\')
        {
          line_text.pop_back();
          continue;
        }

        size_t first = 0;
        while (first < line_text.size() && std::isspace((unsigned char)line_text[first]) != 0)
        {
          first++;
        }
        if (first < line_text.size() && line_text[first] == '#')
        {
          size_t p = first + 1;
          while (p < line_text.size() && std::isspace((unsigned char)line_text[p]) != 0)
          {
            p++;
          }
          const size_t name_at = p;
          while (p < line_text.size() && word_char(line_text[p]))
          {
            p++;
          }
          const std::string name = line_text.substr(name_at, p - name_at);
          while (p < line_text.size() && std::isspace((unsigned char)line_text[p]) != 0)
          {
            p++;
          }
          const size_t arg_at = p;
          while (p < line_text.size() && word_char(line_text[p]))
          {
            p++;
          }
          Directive d;
          d.line = line_no;
          d.arg = line_text.substr(arg_at, p - arg_at);
          if (name == "if")
            d.kind = Directive::If;
          else if (name == "ifdef")
            d.kind = Directive::Ifdef;
          else if (name == "ifndef")
            d.kind = Directive::Ifndef;
          else if (name == "elif")
            d.kind = Directive::Elif;
          else if (name == "else")
            d.kind = Directive::Else;
          else if (name == "endif")
            d.kind = Directive::Endif;
          else if (name == "define")
            d.kind = Directive::Define;
          out.push_back(std::move(d));
        }
        line_text.clear();
        line_no++;
      }
      return out;
    }

    // A guard name is all caps by convention, which is enough to keep an
    // ordinary `#ifndef NDEBUG` + `#define NDEBUG` pair from looking like one.
    bool looks_like_guard_name(const std::string &name)
    {
      if (name.empty())
      {
        return false;
      }
      for (char c : name)
      {
        if (std::isalpha((unsigned char)c) != 0 && std::islower((unsigned char)c) != 0)
        {
          return false;
        }
      }
      return true;
    }

    // --- scopes -------------------------------------------------------------

    // What a `{` at declaration scope opens. Kind File means "opaque": an enum
    // body, a brace initializer, a lambda, anything whose contents are not
    // declarations -- the body is skipped whole.
    struct ScopeFrame
    {
      enum Kind
      {
        File,
        Namespace,
        Class
      };
      Kind kind = File;
      std::string name;
      bool anonymous = false;
      int open_depth = -1;
    };

    // The words before the `{` decide what it opens. A `class`/`struct`/`union`
    // body is read into, so its members get a qualified name; everything else is
    // opaque.
    ScopeFrame describe_scope(const std::vector<Token> &s)
    {
      ScopeFrame frame;
      size_t i = 0;
      while (i < s.size())
      {
        const std::string &t = s[i].text;
        if (t == "template")
        {
          i++;
          if (i < s.size() && s[i].text == "<")
          {
            i = skip_angles(s, i);
          }
          continue;
        }
        if (t == "[[")
        {
          i = skip_attributes(s, i);
          continue;
        }
        if (t == "extern")
        {
          // A linkage block: `extern "C" { ... }` holds ordinary declarations.
          frame.kind = ScopeFrame::Namespace;
          return frame;
        }
        if (is_leading_specifier(t))
        {
          i++;
          continue;
        }
        break;
      }
      if (i >= s.size())
      {
        return frame; // a bare `{`
      }

      const std::string &head = s[i].text;
      if (head == "namespace")
      {
        // `namespace A::B {`, `namespace {`, `namespace app {`.
        std::string name;
        for (size_t k = i + 1; k < s.size(); k++)
        {
          if (s[k].text == "{")
          {
            break;
          }
          name += s[k].text;
        }
        frame.kind = ScopeFrame::Namespace;
        frame.name = name;
        frame.anonymous = name.empty();
        return frame;
      }
      if (head == "class" || head == "struct" || head == "union")
      {
        // The name is the last word before a `:` / `<` / the end that is not a
        // keyword: `struct JOT_API Foo final : public Bar` is Foo.
        int angle = 0;
        std::string name;
        for (size_t k = i + 1; k < s.size(); k++)
        {
          const std::string &t = s[k].text;
          if (opens_angle(s, k))
          {
            angle++;
            continue;
          }
          if (t == ">" && angle > 0)
          {
            angle--;
            continue;
          }
          if (t == ":" && angle == 0)
          {
            break; // the base-class list: its names are not this class's
          }
          if (angle > 0)
          {
            continue;
          }
          if (s[k].kind == Token::Word && !is_keyword(t))
          {
            name = t;
          }
        }
        frame.kind = ScopeFrame::Class;
        frame.name = name;
        frame.anonymous = name.empty();
        return frame;
      }
      return frame;
    }

    // --- signatures ---------------------------------------------------------

    struct NameSpan
    {
      size_t begin = 0;
      size_t end = 0;
      size_t component_begin = 0; // the name alone, without any scope qualification
      size_t component_end = 0;
      std::string scope; // "ns::Box" (template arguments stripped)
      std::string name;  // "put", "~Widget", "Widget", "operator bool", "f<int>"
    };

    // The tokens naming the function whose parameter list opens at `open`, or
    // nothing when what precedes the paren is not a name at all (a control
    // statement, a pointer declarator, a cast, `sizeof`, ...).
    std::optional<NameSpan> name_before(const std::vector<Token> &s, size_t open)
    {
      if (open == 0)
      {
        return std::nullopt;
      }

      // Walk back over `::`-separated components: an optional `~`, an optional
      // `<...>` suffix, then a word.
      size_t begin = open;
      for (;;)
      {
        size_t comp_begin = begin;
        if (s[comp_begin - 1].text == ">")
        {
          int depth = 0;
          bool found = false;
          for (size_t k = comp_begin; k > 0;)
          {
            k--;
            if (s[k].text == ">")
            {
              depth++;
            }
            else if (s[k].text == "<")
            {
              depth--;
              if (depth == 0)
              {
                comp_begin = k;
                found = true;
                break;
              }
            }
          }
          if (!found || comp_begin == 0)
          {
            return std::nullopt;
          }
        }
        if (s[comp_begin - 1].kind != Token::Word)
        {
          return std::nullopt;
        }
        comp_begin--;
        if (comp_begin > 0 && s[comp_begin - 1].text == "~")
        {
          comp_begin--;
        }
        begin = comp_begin;
        if (begin >= 1 && s[begin - 1].text == "::")
        {
          begin--;
          if (begin >= 1 && s[begin - 1].text == "::")
          {
            begin--; // a leading `::`: the global scope, not a chain
            break;
          }
          continue;
        }
        break;
      }
      if (begin >= open)
      {
        return std::nullopt;
      }

      // Split the span at its last top-level `::`: what precedes is the scope.
      size_t last_colon = std::string::npos;
      int angle = 0;
      for (size_t i = begin; i < open; i++)
      {
        if (opens_angle(s, i))
        {
          angle++;
        }
        else if (s[i].text == ">" && angle > 0)
        {
          angle--;
        }
        else if (s[i].text == "::" && angle == 0)
        {
          last_colon = i;
        }
      }

      NameSpan span;
      span.begin = begin;
      span.end = open;
      const size_t name_begin = last_colon == std::string::npos ? begin : last_colon + 1;
      span.component_begin = name_begin;
      span.component_end = open;
      span.name = join_tokens(s, name_begin, open);
      if (last_colon != std::string::npos)
      {
        // The scope keeps its words and `::`s, with template arguments dropped so
        // that `void Box<T>::put(T)` matches the in-class `void put(T);` whose
        // scope is the class frame's plain name.
        std::string scope;
        int depth = 0;
        for (size_t i = begin; i < last_colon; i++)
        {
          const std::string &t = s[i].text;
          if (t == "<")
          {
            depth++;
            continue;
          }
          if (t == ">")
          {
            if (depth > 0)
            {
              depth--;
            }
            continue;
          }
          if (depth > 0)
          {
            continue;
          }
          scope += t;
        }
        span.scope = scope;
      }

      const std::string bare = span.name.rfind("~", 0) == 0 ? span.name.substr(1) : span.name;
      if (!is_operator_name(span.name) && is_keyword(bare))
      {
        return std::nullopt;
      }
      return span;
    }

    struct Signature
    {
      enum Terminator
      {
        Declaration,
        Body,
        Deleted,
        Defaulted,
        Pure,
        Unknown
      };

      size_t name_begin = 0;
      size_t name_end = 0;
      // The name's own token range, where name_begin/name_end still hold the
      // whole span including any `ns::` in front of it.
      size_t name_component_begin = 0;
      size_t name_component_end = 0;
      std::string scope;
      std::string name;
      size_t params_open = 0;
      size_t params_close = 0;
      Terminator terminator = Unknown;
      std::string params;
      std::string params_display;
      std::string suffix;
      bool params_look_like_declaration = true;
      bool templated = false;
      bool inline_hint = false;
      bool is_static = false;
      bool explicit_hint = false;
    };

    // The parameter list written the one canonical way: default arguments gone,
    // parameter names gone (a definition may name them differently), array
    // parameters spelled as pointers, and top-level `const` moved to the front
    // so `T const &` and `const T &` are the same type.
    struct ParamText
    {
      std::string canonical;
      std::string display;
      bool looks_like_declaration = true;
    };

    // Splits the list at top-level commas, dropping each default value.
    std::vector<std::pair<size_t, size_t>> split_params(const std::vector<Token> &s,
                                                        size_t open,
                                                        size_t close)
    {
      std::vector<std::pair<size_t, size_t>> out;
      size_t begin = open + 1;
      int paren = 0;
      int bracket = 0;
      int brace = 0;
      int angle = 0;
      bool in_default = false;
      for (size_t i = open + 1; i < close; i++)
      {
        const std::string &t = s[i].text;
        if (t == "(")
        {
          paren++;
        }
        else if (t == ")")
        {
          paren--;
        }
        else if (t == "[")
        {
          bracket++;
        }
        else if (t == "]")
        {
          bracket--;
        }
        else if (t == "{")
        {
          brace++;
        }
        else if (t == "}")
        {
          brace--;
        }

        if (paren == 0 && bracket == 0 && brace == 0)
        {
          if (!in_default && t == "=")
          {
            in_default = true;
            continue;
          }
          if (!in_default)
          {
            if (opens_angle(s, i))
            {
              angle++;
              continue;
            }
            if (t == ">" && angle > 0)
            {
              angle--;
              continue;
            }
          }
          if (!in_default && angle == 0 && t == ",")
          {
            if (i > begin)
            {
              out.push_back({begin, i});
            }
            begin = i + 1;
            continue;
          }
        }
      }
      if (close > begin)
      {
        out.push_back({begin, close});
      }
      return out;
    }

    // Spellings one type is written with across platforms and projects, so that
    // `unsigned long` and `DWORD`, and `std::uint8_t` and `unsigned char`, are
    // the same type to the matcher. Longest first: `std::uint8_t` before
    // `uint8_t`. Only the match key is rewritten -- nothing touches the file.
    std::string normalize_type_aliases(const std::string &text)
    {
      static const std::vector<std::pair<std::string, std::string>> aliases = {
          {"std::uint8_t", "unsigned char"},   {"std::int8_t", "signed char"},
          {"std::uint16_t", "unsigned short"},  {"std::int16_t", "short"},
          {"std::uint32_t", "unsigned int"},    {"std::int32_t", "int"},
          {"std::uint64_t", "unsigned long long"}, {"std::int64_t", "long long"},
          {"std::uintptr_t", "size_t"},         {"std::intptr_t", "size_t"},
          {"std::size_t", "size_t"},            {"uintptr_t", "size_t"},
          {"intptr_t", "size_t"},               {"uint8_t", "unsigned char"},
          {"int8_t", "signed char"},            {"uint16_t", "unsigned short"},
          {"int16_t", "short"},                {"uint32_t", "unsigned int"},
          {"int32_t", "int"},                   {"uint64_t", "unsigned long long"},
          {"int64_t", "long long"},             {"DWORD", "unsigned long"},
          {"ULONG", "unsigned long"},           {"LONG", "long"},
          {"UINT", "unsigned int"},             {"INT", "int"},
          {"BOOL", "int"},                      {"BYTE", "unsigned char"},
          {"WORD", "unsigned short"},           {"WCHAR", "wchar_t"},
          {"CHAR", "char"}};
      std::string out = text;
      for (const auto &alias : aliases)
      {
        const std::string &from = alias.first;
        const std::string &to = alias.second;
        for (size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + to.size()))
        {
          const bool left_ok = at == 0 || !word_char(out[at - 1]);
          const size_t after = at + from.size();
          const bool right_ok = after >= out.size() || !word_char(out[after]);
          if (!left_ok || !right_ok)
          {
            continue;
          }
          out.replace(at, from.size(), to);
        }
      }
      return out;
    }

    // Joins a transformed token-text list using the same spacing rule as
    // join_tokens. `spaced_pointers` is for the display form, where a name kept
    // after a `*`/`&` should not run into it (`const Vec& other`).
    std::string join_texts(const std::vector<std::string> &tokens, bool spaced_pointers)
    {
      std::string out;
      for (size_t i = 0; i < tokens.size(); i++)
      {
        if (!out.empty() && !tokens[i].empty() && !tokens[i - 1].empty()
            && ((word_start(tokens[i - 1][0]) && word_start(tokens[i][0]))
                || (spaced_pointers && word_start(tokens[i][0])
                    && (tokens[i - 1] == "*" || tokens[i - 1] == "&"
                        || tokens[i - 1] == "&&"))))
        {
          out += ' ';
        }
        out += tokens[i];
      }
      return out;
    }

    std::string canonical_param(const std::vector<Token> &s,
                                size_t begin,
                                size_t end,
                                bool keep_name)
    {
      std::vector<std::string> tokens;
      for (size_t i = begin; i < end; i++)
      {
        tokens.push_back(s[i].text);
      }

      // An array parameter (`int a[8]`) is a pointer: the name and its brackets
      // both go, and a `*` takes their place.
      size_t array_name = std::string::npos;
      for (size_t i = begin; i < end; i++)
      {
        if (s[i].text == "[" && i > begin && s[i - 1].kind == Token::Word)
        {
          array_name = i - 1;
          break;
        }
      }
      if (!keep_name && array_name != std::string::npos)
      {
        std::vector<std::string> rebuilt;
        for (size_t i = begin; i < array_name; i++)
        {
          rebuilt.push_back(s[i].text);
        }
        rebuilt.push_back("*");
        tokens = std::move(rebuilt);
      }
      else if (!keep_name)
      {
        // The declarator's name: the outermost word that is not a keyword, not
        // part of a `::` chain, and not inside `<...>`.
        size_t candidate = std::string::npos;
        size_t candidate_depth = 0;
        int angle = 0;
        int paren = 0;
        for (size_t i = begin; i < end; i++)
        {
          const std::string &t = s[i].text;
          if (t == "(")
          {
            paren++;
            continue;
          }
          if (t == ")")
          {
            paren--;
            continue;
          }
          if (opens_angle(s, i))
          {
            angle++;
            continue;
          }
          if (t == ">" && angle > 0)
          {
            angle--;
            continue;
          }
          if (angle > 0 || s[i].kind != Token::Word || is_keyword(t) || t == "operator")
          {
            continue;
          }
          const bool after_scope =
              i > begin && (s[i - 1].text == "::" || s[i - 1].text == ".");
          const bool before_scope = i + 1 < end && s[i + 1].text == "::";
          if (after_scope || before_scope)
          {
            continue;
          }
          if (candidate == std::string::npos || paren < (int)candidate_depth)
          {
            candidate = i;
            candidate_depth = (size_t)paren;
          }
          else if ((size_t)paren == candidate_depth)
          {
            candidate = i;
          }
        }
        if (candidate != std::string::npos)
        {
          bool has_type = false;
          for (size_t i = begin; i < end; i++)
          {
            if (i == candidate)
            {
              continue;
            }
            if (s[i].kind == Token::Word || s[i].text == "*" || s[i].text == "&"
                || s[i].text == "::")
            {
              has_type = true;
              break;
            }
          }
          if (has_type)
          {
            tokens.erase(tokens.begin() + (long)(candidate - begin));
          }
        }
      }

      // Top-level `const` / `volatile` move to the front.
      std::vector<std::string> qualifiers;
      std::vector<std::string> rest;
      for (const std::string &t : tokens)
      {
        if (t == "const" || t == "volatile")
        {
          if (std::find(qualifiers.begin(), qualifiers.end(), t) == qualifiers.end())
          {
            qualifiers.push_back(t);
          }
          continue;
        }
        rest.push_back(t);
      }
      std::vector<std::string> final_tokens;
      final_tokens.insert(final_tokens.end(), qualifiers.begin(), qualifiers.end());
      final_tokens.insert(final_tokens.end(), rest.begin(), rest.end());
      const std::string joined = join_texts(final_tokens, keep_name);
      return keep_name ? joined : normalize_type_aliases(joined);
    }

    ParamText parse_params(const std::vector<Token> &s, size_t open, size_t close)
    {
      ParamText out;
      const std::vector<std::pair<size_t, size_t>> slices = split_params(s, open, close);
      for (size_t k = 0; k < slices.size(); k++)
      {
        const size_t begin = slices[k].first;
        const size_t end = slices[k].second;
        if (k > 0)
        {
          out.canonical += ", ";
          out.display += ", ";
        }
        out.canonical += canonical_param(s, begin, end, false);
        out.display += canonical_param(s, begin, end, true);

        // The value test: `Foo x(1);` and `static Config c(kName);` are
        // initializers -- a parameter list is not written with values in it.
        // Operators count at angle depth zero only, since a non-type template
        // argument is an expression (`std::array<int, 3>`, `enable_if_t<!B, T>`).
        int angle = 0;
        for (size_t i = begin; i < end; i++)
        {
          const std::string &t = s[i].text;
          if (opens_angle(s, i))
          {
            angle++;
            continue;
          }
          if (t == ">" && angle > 0)
          {
            angle--;
            continue;
          }
          if (angle > 0)
          {
            continue;
          }
          const bool value = s[i].kind == Token::Number || s[i].kind == Token::Literal
                             || (s[i].kind == Token::Word
                                 && (t == "true" || t == "false" || t == "nullptr"));
          const bool operator_at_top = t == "+" || t == "-" || t == "/" || t == "%"
                                       || t == "|" || t == "^" || t == "!" || t == "?"
                                       || t == "." || t == ".*" || t == "->*" || t == "++"
                                       || t == "--" || t == "=";
          if (value || operator_at_top)
          {
            out.looks_like_declaration = false;
          }
        }
      }
      return out;
    }

    // Whether the part of the statement before the name holds a type: `void f()`
    // does, `foo(1);` and `explicit Widget();` do not.
    bool has_type_prefix(const std::vector<Token> &s, size_t name_begin)
    {
      size_t i = 0;
      while (i < name_begin)
      {
        const std::string &t = s[i].text;
        if (t == "template")
        {
          i++;
          if (i < name_begin && s[i].text == "<")
          {
            i = skip_angles(s, i);
          }
          continue;
        }
        if (t == "[[")
        {
          i = skip_attributes(s, i);
          continue;
        }
        if (is_leading_specifier(t))
        {
          i++;
          continue;
        }
        if (s[i].kind == Token::Literal && i > 0 && s[i - 1].text == "extern")
        {
          i++; // the `"C"` of `extern "C"`
          continue;
        }
        return true;
      }
      return false;
    }

    // From the `:` of a constructor's member-initializer list, the position of
    // the first `{` that is the body rather than a brace initializer (`m_{1}`),
    // or one past the end when the statement holds no body.
    size_t skip_initializer_list(const std::vector<Token> &s, size_t i)
    {
      while (i < s.size())
      {
        const std::string &t = s[i].text;
        if (t == "(" || t == "[" || t == "{")
        {
          const bool brace_init = t == "{" && i > 0
                                  && (s[i - 1].kind == Token::Word || s[i - 1].text == ")"
                                      || s[i - 1].text == "]" || s[i - 1].text == ">");
          if (t == "{" && !brace_init)
          {
            return i;
          }
          i = skip_balanced(s, i);
          continue;
        }
        if (t == ";")
        {
          return i;
        }
        i++;
      }
      return i;
    }

    // Reads the signature out of one statement's tokens. `at_brace` says the
    // statement ended at a `{` (a definition body) rather than at a `;`.
    std::optional<Signature> parse_signature(const std::vector<Token> &s, bool at_brace)
    {
      if (s.empty())
      {
        return std::nullopt;
      }

      Signature sig;
      size_t i = 0;
      while (i < s.size())
      {
        const std::string &t = s[i].text;
        if (t == "template")
        {
          sig.templated = true;
          i++;
          if (i < s.size() && s[i].text == "<")
          {
            i = skip_angles(s, i);
          }
          continue;
        }
        if (t == "[[")
        {
          i = skip_attributes(s, i);
          continue;
        }
        if (t == "inline" || t == "constexpr" || t == "consteval" || t == "constinit")
        {
          sig.inline_hint = true;
          i++;
          continue;
        }
        if (t == "static")
        {
          sig.is_static = true;
          i++;
          continue;
        }
        if (t == "explicit")
        {
          sig.explicit_hint = true;
          i++;
          continue;
        }
        if (t == "extern")
        {
          i++;
          if (i < s.size() && s[i].kind == Token::Literal)
          {
            i++; // extern "C"
          }
          continue;
        }
        if (t == "mutable" || t == "thread_local" || t == "register")
        {
          i++;
          continue;
        }
        break;
      }

      size_t params_open = std::string::npos;
      int angle = 0;
      for (size_t p = i; p < s.size(); p++)
      {
        const std::string &t = s[p].text;
        if (t == "[[")
        {
          p = skip_attributes(s, p) - 1;
          continue;
        }
        if (t == "(" && angle == 0)
        {
          if (p > 0 && s[p - 1].kind == Token::Word && is_paren_head_word(s[p - 1].text))
          {
            p = skip_balanced(s, p) - 1;
            continue;
          }
          params_open = p;
          break;
        }
        if (angle == 0 && (t == "{" || t == "=" || t == ";"))
        {
          return std::nullopt; // an initializer, a brace block, or nothing at all
        }
        if (opens_angle(s, p))
        {
          angle++;
        }
        else if (t == ">" && angle > 0)
        {
          angle--;
        }
      }
      if (params_open == std::string::npos)
      {
        return std::nullopt;
      }
      sig.params_open = params_open;
      const size_t close = skip_balanced(s, params_open);
      if (close <= params_open)
      {
        return std::nullopt;
      }
      sig.params_close = close - 1;

      const std::optional<NameSpan> span = name_before(s, params_open);
      if (!span)
      {
        return std::nullopt;
      }
      sig.name_begin = span->begin;
      sig.name_end = span->end;
      sig.name_component_begin = span->component_begin;
      sig.name_component_end = span->component_end;
      sig.scope = span->scope;
      sig.name = span->name;

      const ParamText params = parse_params(s, sig.params_open, sig.params_close);
      sig.params = params.canonical;
      sig.params_display = params.display;
      sig.params_look_like_declaration = params.looks_like_declaration;

      // Whatever follows the `)` decides what the statement is -- and a
      // parameter list that is followed by something other than one of these is
      // not a parameter list at all.
      std::vector<std::string> qualifiers;
      std::string refs;
      size_t j = sig.params_close + 1;
      while (j < s.size())
      {
        const Token &t = s[j];
        if (t.kind == Token::Word)
        {
          if (t.text == "const" || t.text == "volatile")
          {
            qualifiers.push_back(t.text);
            j++;
            continue;
          }
          if (t.text == "override" || t.text == "final" || t.text == "mutable"
              || t.text == "constexpr" || t.text == "consteval")
          {
            j++;
            continue;
          }
          if (t.text == "noexcept" || t.text == "throw")
          {
            j++;
            if (j < s.size() && s[j].text == "(")
            {
              j = skip_balanced(s, j);
            }
            continue;
          }
          return std::nullopt; // an unexpected word: not a signature we understand
        }
        if (t.text == "[[")
        {
          j = skip_attributes(s, j);
          continue;
        }
        if (t.text == "]]")
        {
          j++;
          continue;
        }
        if (t.text == "&" || t.text == "&&")
        {
          refs += t.text;
          j++;
          continue;
        }
        if (t.text == "->")
        {
          // A trailing return type: its tokens run to the body or the `;`.
          j++;
          int depth = 0;
          while (j < s.size())
          {
            const std::string &u = s[j].text;
            if (u == "(" || u == "[" || u == "{")
            {
              depth++;
            }
            else if (u == ")" || u == "]" || u == "}")
            {
              depth--;
            }
            else if (depth == 0 && (u == ";" || u == "="))
            {
              break;
            }
            j++;
          }
          continue;
        }
        if (t.text == ":")
        {
          // A constructor's member-initializer list.
          j = skip_initializer_list(s, j + 1);
          if (j >= s.size())
          {
            sig.terminator = at_brace ? Signature::Body : Signature::Declaration;
            break;
          }
          continue;
        }
        if (t.text == "=")
        {
          j++;
          if (j < s.size() && s[j].kind == Token::Number && s[j].text == "0")
          {
            sig.terminator = Signature::Pure;
            j++;
            continue;
          }
          if (j < s.size() && s[j].text == "delete")
          {
            sig.terminator = Signature::Deleted;
            j++;
            continue;
          }
          if (j < s.size() && s[j].text == "default")
          {
            sig.terminator = Signature::Defaulted;
            j++;
            continue;
          }
          return std::nullopt;
        }
        if (t.text == "{")
        {
          sig.terminator = Signature::Body;
          break;
        }
        if (t.text == ";")
        {
          sig.terminator = Signature::Declaration;
          break;
        }
        return std::nullopt;
      }
      if (sig.terminator == Signature::Unknown)
      {
        sig.terminator = at_brace ? Signature::Body : Signature::Declaration;
      }

      for (const std::string &q : qualifiers)
      {
        sig.suffix += " " + q;
      }
      sig.suffix += refs;
      return sig;
    }
  } // namespace

  bool is_header_file(const std::string &path)
  {
    static const std::unordered_set<std::string> extensions = {
        "h", "hh", "hpp", "hxx", "h++", "inl", "ipp", "tpp", "tcc", "cuh"};
    const std::string lower = string_util::lower_copy(path);
    const size_t dot = lower.rfind('.');
    if (dot == std::string::npos || dot + 1 >= lower.size())
    {
      return false;
    }
    return extensions.count(lower.substr(dot + 1)) != 0;
  }

  bool is_source_file(const std::string &path)
  {
    static const std::unordered_set<std::string> extensions = {
        "c", "cc", "cpp", "cxx", "c++", "cu", "m", "mm"};
    const std::string lower = string_util::lower_copy(path);
    const size_t dot = lower.rfind('.');
    if (dot == std::string::npos || dot + 1 >= lower.size())
    {
      return false;
    }
    return extensions.count(lower.substr(dot + 1)) != 0;
  }

  bool is_parseable_file(const std::string &path)
  {
    return is_header_file(path) || is_source_file(path);
  }

  std::vector<FunctionRecord> parse_file(const std::string &path, const std::string &text)
  {
    const bool header = is_header_file(path);
    const std::string masked = mask_non_code(text);
    const std::vector<Token> tokens = tokenize(blank_directives(masked));
    const std::vector<Directive> directives = scan_directives(masked);

    struct CondFrame
    {
      bool guard_candidate = false;
      bool is_guard = false;
      std::string guard_name;
    };
    std::vector<CondFrame> conditions;
    bool seen_conditional = false;
    size_t next_directive = 0;

    std::vector<ScopeFrame> scopes;
    scopes.push_back(ScopeFrame{});

    std::vector<FunctionRecord> out;
    std::vector<size_t> statement;
    int brace_depth = 0;
    bool skipping = false;
    int skip_depth = 0;

    auto conditional_now = [&]()
    {
      for (const CondFrame &c : conditions)
      {
        if (!c.is_guard)
        {
          return true;
        }
      }
      return false;
    };

    auto apply_directives_until = [&](int line)
    {
      while (next_directive < directives.size() && directives[next_directive].line <= line)
      {
        const Directive &d = directives[next_directive++];
        switch (d.kind)
        {
        case Directive::Ifndef:
        case Directive::Ifdef:
        case Directive::If:
        {
          CondFrame frame;
          if (d.kind == Directive::Ifndef)
          {
            frame.guard_candidate = !seen_conditional && looks_like_guard_name(d.arg);
            frame.guard_name = d.arg;
          }
          seen_conditional = true;
          conditions.push_back(frame);
          break;
        }
        case Directive::Define:
          if (!conditions.empty() && conditions.back().guard_candidate
              && conditions.back().guard_name == d.arg)
          {
            conditions.back().is_guard = true;
          }
          break;
        case Directive::Endif:
          if (!conditions.empty())
          {
            conditions.pop_back();
          }
          break;
        default:
          break;
        }
      }
    };

    auto scope_prefix = [&]() -> std::string
    {
      std::string out;
      for (const ScopeFrame &frame : scopes)
      {
        if (frame.kind == ScopeFrame::File || frame.name.empty())
        {
          continue;
        }
        if (!out.empty())
        {
          out += "::";
        }
        out += frame.name;
      }
      return out;
    };

    auto anonymous_namespace_now = [&]()
    {
      for (const ScopeFrame &frame : scopes)
      {
        if (frame.kind == ScopeFrame::Namespace && frame.anonymous)
        {
          return true;
        }
      }
      return false;
    };

    auto in_class_scope = [&]()
    {
      for (size_t i = scopes.size(); i-- > 0;)
      {
        if (scopes[i].kind == ScopeFrame::Class)
        {
          return true;
        }
        if (scopes[i].kind == ScopeFrame::Namespace)
        {
          return false;
        }
      }
      return false;
    };

    auto enclosing_class = [&]() -> std::string
    {
      for (size_t i = scopes.size(); i-- > 0;)
      {
        if (scopes[i].kind == ScopeFrame::Class)
        {
          return scopes[i].name;
        }
      }
      return std::string();
    };

    // Turns one statement that parsed as a function signature into a record,
    // after the ambiguity tests that decide whether it really is one.
    auto record_signature = [&](const Signature &sig,
                                const std::vector<Token> &statement_tokens,
                                bool body)
    {
      // A parameter list written with values in it belongs to an initializer.
      if (!sig.params_look_like_declaration)
      {
        return;
      }

      const std::string prefix = scope_prefix();
      // A qualified name is relative to the enclosing scope -- `void
      // Definitions::rebuild()` written inside `namespace jot_color` is
      // jot_color::Definitions::rebuild -- unless its first component already
      // names one of those scopes, which is how a fully qualified definition
      // written inside its own namespace reads.
      std::string scope = sig.scope;
      if (scope.empty())
      {
        scope = prefix;
      }
      else if (!prefix.empty())
      {
        const std::string first = scope.substr(0, scope.find("::"));
        bool absolute = false;
        for (size_t at = 0; at <= prefix.size();)
        {
          const size_t end = prefix.find("::", at);
          const std::string component =
              prefix.substr(at, end == std::string::npos ? std::string::npos : end - at);
          if (component == first)
          {
            absolute = true;
            break;
          }
          if (end == std::string::npos)
          {
            break;
          }
          at = end + 2;
        }
        if (!absolute)
        {
          scope = prefix + "::" + scope;
        }
      }
      const std::string klass = enclosing_class();
      const bool scoped = !sig.scope.empty();
      const bool conversion = is_operator_name(sig.name);
      const bool constructor = !klass.empty() && sig.scope.empty() && sig.name == klass;
      const bool destructor = !klass.empty() && sig.scope.empty()
                              && sig.name == "~" + klass;
      const bool in_class = in_class_scope();

      // Nothing that looks like a type before the name means this is not a
      // declaration unless it is a constructor, a destructor, a conversion
      // operator, or qualified: everything else of that shape is a call. Inside
      // a class body it is always a declaration -- C++ reads a member's bare
      // `Foo();` (or a fragment's, whose class name belongs to its includer) as
      // a constructor, not as a call.
      if (!has_type_prefix(statement_tokens, sig.name_begin) && !scoped && !conversion
          && !constructor && !destructor && !in_class)
      {
        return;
      }

      FunctionRecord record;
      record.file = path;
      // Positions come from the statement's own token view: `name_begin` and
      // `params_close` index into it, not into the file's tokens.
      record.line = statement_tokens[sig.name_begin].line;
      record.col = statement_tokens[sig.name_begin].col;
      record.end_line = statement_tokens[sig.params_close].line;
      record.end_col = statement_tokens[sig.params_close].col + 1;
      record.name_line = statement_tokens[sig.name_component_begin].line;
      record.name_col = statement_tokens[sig.name_component_begin].col;
      record.name_end_line = statement_tokens[sig.name_component_end - 1].line;
      const Token &name_end = statement_tokens[sig.name_component_end - 1];
      record.name_end_col = name_end.col + (int)name_end.text.size();
      record.scope = scope;
      record.name = sig.name;
      record.params = sig.params;
      record.params_display = sig.params_display;
      record.suffix = sig.suffix;
      record.key = scope + (scope.empty() ? "" : "::") + sig.name + "(" + sig.params + ")"
                   + sig.suffix;
      record.display = (scope.empty() ? "" : scope + "::") + sig.name + "("
                       + sig.params_display + ")" + sig.suffix;
      record.definition = body;
      record.in_class_definition = body && in_class && sig.scope.empty();
      record.deleted_or_default = sig.terminator == Signature::Deleted
                                  || sig.terminator == Signature::Defaulted;
      record.pure_virtual = sig.terminator == Signature::Pure;
      record.template_function = sig.templated;
      record.inline_function = sig.inline_hint || record.in_class_definition;
      record.internal_linkage = anonymous_namespace_now() || (sig.is_static && !in_class);
      record.conditional = conditional_now();
      record.is_header = header;
      out.push_back(std::move(record));
    };

    auto statement_tokens = [&](std::vector<Token> &storage) -> std::vector<Token> &
    {
      storage.clear();
      for (size_t index : statement)
      {
        storage.push_back(tokens[index]);
      }
      storage = merge_operator_names(storage);
      return storage;
    };

    std::vector<Token> scratch;
    for (size_t i = 0; i < tokens.size();)
    {
      const Token &t = tokens[i];
      apply_directives_until(t.line);

      if (skipping)
      {
        if (t.text == "{")
        {
          brace_depth++;
        }
        else if (t.text == "}")
        {
          brace_depth--;
          if (brace_depth <= skip_depth)
          {
            skipping = false;
          }
        }
        i++;
        continue;
      }

      if (t.text == "{")
      {
        const std::vector<Token> &statement_view = statement_tokens(scratch);
        const std::optional<Signature> sig = parse_signature(statement_view, true);
        if (sig && sig->terminator == Signature::Body)
        {
          record_signature(*sig, statement_view, true);
          skipping = true;
          skip_depth = brace_depth;
        }
        else
        {
          ScopeFrame frame = describe_scope(statement_view);
          if (frame.kind == ScopeFrame::File)
          {
            // Not a scope we can read into (an enum body, a brace initializer, a
            // lambda): skip it whole rather than guess at its contents.
            skipping = true;
            skip_depth = brace_depth;
          }
          else
          {
            frame.open_depth = brace_depth;
            scopes.push_back(frame);
          }
        }
        brace_depth++;
        statement.clear();
        i++;
        continue;
      }

      if (t.text == "}")
      {
        brace_depth--;
        while (scopes.size() > 1 && scopes.back().open_depth == brace_depth)
        {
          scopes.pop_back();
        }
        statement.clear();
        i++;
        continue;
      }

      if ((t.text == "public" || t.text == "private" || t.text == "protected")
          && i + 1 < tokens.size() && tokens[i + 1].text == ":" && brace_depth == 0
          && !in_class_scope())
      {
        // An access label at file scope: this header is a *fragment* meant to be
        // included inside a class body (the editor's api/ fragments are), so
        // what follows is class scope. The class name belongs to the includer
        // and stays unknown, which is enough: members defined here are inline,
        // and a declaration whose body lives in the real class is matched by
        // signature rather than by scope.
        statement.clear();
        ScopeFrame fragment;
        fragment.kind = ScopeFrame::Class;
        fragment.anonymous = true;
        fragment.open_depth = -1; // closed by the includer, never by this file
        scopes.push_back(fragment);
        i += 2;
        continue;
      }

      if (t.text == ";")
      {
        const std::vector<Token> &statement_view = statement_tokens(scratch);
        const std::optional<Signature> sig = parse_signature(statement_view, false);
        if (sig && (sig->terminator == Signature::Declaration
                    || sig->terminator == Signature::Deleted
                    || sig->terminator == Signature::Defaulted
                    || sig->terminator == Signature::Pure))
        {
          record_signature(*sig, statement_view, false);
        }
        statement.clear();
        i++;
        continue;
      }

      statement.push_back(i);
      i++;
    }

    return out;
  }
} // namespace CppDefinitions
