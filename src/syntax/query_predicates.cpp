#include "syntax/query_predicates.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>
#include <utility>

namespace jot_query
{
  namespace
  {
#ifdef JOT_TREESITTER
    // One step of a predicate: either a capture name or a literal string.
    struct Arg
    {
      bool capture = false;
      std::string value;
    };

    struct Form
    {
      std::string name;
      std::vector<Arg> args;
      bool directive() const { return !name.empty() && name.back() == '!'; }
    };

    // The predicates and directives of one pattern, in the order the query
    // file wrote them. The steps arrive flat, with a Done step between forms,
    // and the first step of a form is the form's own name.
    std::vector<Form> forms_of(const TSQuery *query, uint32_t pattern_index)
    {
      std::vector<Form> forms;
      uint32_t step_count = 0;
      const TSQueryPredicateStep *steps =
          ts_query_predicates_for_pattern(query, pattern_index, &step_count);
      if (!steps)
      {
        return forms;
      }
      Form form;
      for (uint32_t i = 0; i < step_count; i++)
      {
        const TSQueryPredicateStep &step = steps[i];
        if (step.type == TSQueryPredicateStepTypeDone)
        {
          if (!form.name.empty())
          {
            forms.push_back(form);
          }
          form = Form{};
          continue;
        }
        if (form.name.empty())
        {
          // The form's name is always a string step; it arrives without the
          // leading '#', which the query grammar strips.
          uint32_t length = 0;
          const char *value = ts_query_string_value_for_id(query, step.value_id, &length);
          form.name.assign(value ? value : "", length);
          continue;
        }
        Arg arg;
        if (step.type == TSQueryPredicateStepTypeCapture)
        {
          uint32_t length = 0;
          const char *value = ts_query_capture_name_for_id(query, step.value_id, &length);
          arg.capture = true;
          arg.value.assign(value ? value : "", length);
        }
        else
        {
          uint32_t length = 0;
          const char *value = ts_query_string_value_for_id(query, step.value_id, &length);
          arg.value.assign(value ? value : "", length);
        }
        form.args.push_back(std::move(arg));
      }
      return forms;
    }

    // Every node a capture name bound in this match. A quantified or alternated
    // pattern binds one name more than once, and both `#any-eq?` and the plain
    // forms have to see all of them.
    std::vector<TSNode> nodes_for_capture(const TSQuery *query,
                                          const TSQueryMatch &match,
                                          const std::string &name)
    {
      std::vector<TSNode> nodes;
      for (uint16_t i = 0; i < match.capture_count; i++)
      {
        uint32_t length = 0;
        const char *capture_name =
            ts_query_capture_name_for_id(query, match.captures[i].index, &length);
        if (capture_name && name == std::string(capture_name, length))
        {
          nodes.push_back(match.captures[i].node);
        }
      }
      return nodes;
    }

    bool node_is_null(TSNode node)
    {
      return ts_node_is_null(node);
    }
#endif // JOT_TREESITTER

    // ---------------------------------------------------------------------
    // Lua patterns
    // ---------------------------------------------------------------------

    bool class_contains(char cls, char c)
    {
      switch (cls)
      {
      case 'a':
        return std::isalpha((unsigned char)c) != 0;
      case 'd':
        return std::isdigit((unsigned char)c) != 0;
      case 'l':
        return std::islower((unsigned char)c) != 0;
      case 'u':
        return std::isupper((unsigned char)c) != 0;
      case 's':
        return std::isspace((unsigned char)c) != 0;
      case 'w':
        return std::isalnum((unsigned char)c) != 0;
      case 'x':
        return std::isxdigit((unsigned char)c) != 0;
      case 'p':
        return std::ispunct((unsigned char)c) != 0;
      case 'c':
        return std::iscntrl((unsigned char)c) != 0;
      default:
        return false;
      }
    }

    // One pattern item: the thing a quantifier multiplies. `end` is where the
    // next item starts.
    struct Item
    {
      size_t end = 0;
      bool valid = false;
      std::function<bool(char)> matches;
    };

    bool is_class_letter(char c)
    {
      switch (std::tolower((unsigned char)c))
      {
      case 'a':
      case 'd':
      case 'l':
      case 'u':
      case 's':
      case 'w':
      case 'x':
      case 'p':
      case 'c':
        return true;
      default:
        return false;
      }
    }

    Item read_item(const std::string &pattern, size_t pos)
    {
      Item item;
      if (pos >= pattern.size())
      {
        return item;
      }
      const char c = pattern[pos];
      if (c == '%')
      {
        if (pos + 1 >= pattern.size())
        {
          return item;
        }
        const char escaped = pattern[pos + 1];
        item.end = pos + 2;
        item.valid = true;
        if (std::isalpha((unsigned char)escaped) && is_class_letter(escaped))
        {
          const bool complement = std::isupper((unsigned char)escaped) != 0;
          const char cls = (char)std::tolower((unsigned char)escaped);
          item.matches = [cls, complement](char ch) {
            const bool in = class_contains(cls, ch);
            return complement ? !in : in;
          };
        }
        else
        {
          // An escaped magic character matches itself.
          item.matches = [escaped](char ch) { return ch == escaped; };
        }
        return item;
      }
      if (c == '.')
      {
        item.end = pos + 1;
        item.valid = true;
        item.matches = [](char) { return true; };
        return item;
      }
      if (c == '[')
      {
        size_t i = pos + 1;
        bool negate = false;
        if (i < pattern.size() && pattern[i] == '^')
        {
          negate = true;
          i++;
        }
        std::string literals;
        std::string classes;
        std::vector<std::pair<char, char>> ranges;
        while (i < pattern.size() && pattern[i] != ']')
        {
          if (pattern[i] == '%' && i + 1 < pattern.size())
          {
            classes.push_back(pattern[i + 1]);
            i += 2;
            continue;
          }
          if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']')
          {
            ranges.push_back({pattern[i], pattern[i + 2]});
            i += 3;
            continue;
          }
          literals.push_back(pattern[i]);
          i++;
        }
        if (i >= pattern.size())
        {
          return item; // unterminated set: no item, and the match fails
        }
        item.end = i + 1;
        item.valid = true;
        item.matches = [negate, literals, classes, ranges](char ch) {
          bool in = literals.find(ch) != std::string::npos;
          for (const auto &range : ranges)
          {
            if (ch >= range.first && ch <= range.second)
            {
              in = true;
            }
          }
          for (char cls : classes)
          {
            if (is_class_letter(cls))
            {
              const bool complement = std::isupper((unsigned char)cls) != 0;
              const bool hit = class_contains((char)std::tolower((unsigned char)cls), ch);
              if (complement ? !hit : hit)
              {
                in = true;
              }
            }
            else if (cls == ch)
            {
              in = true;
            }
          }
          return negate ? !in : in;
        };
        return item;
      }
      // Anything else is a literal, including the magic characters that only
      // mean something in their own position (^, $, (, ), *, +, -, ?).
      item.end = pos + 1;
      item.valid = true;
      item.matches = [c](char ch) { return ch == c; };
      return item;
    }

    bool match_at(const std::string &pattern, size_t pos, const std::string &text, size_t at)
    {
      if (pos == pattern.size())
      {
        return true;
      }
      if (pattern[pos] == '$' && pos + 1 == pattern.size())
      {
        return at == text.size();
      }
      const Item item = read_item(pattern, pos);
      if (!item.valid)
      {
        return false;
      }
      const char quantifier = item.end < pattern.size() ? pattern[item.end] : '\0';
      const bool quantified = quantifier == '*' || quantifier == '+' || quantifier == '-' ||
                              quantifier == '?';
      const size_t next = quantified ? item.end + 1 : item.end;
      size_t max = 0;
      while (at + max < text.size() && item.matches(text[at + max]))
      {
        max++;
      }
      if (quantifier == '?')
      {
        if (max >= 1 && match_at(pattern, next, text, at + 1))
        {
          return true;
        }
        return match_at(pattern, next, text, at);
      }
      if (quantifier == '*')
      {
        for (size_t take = max;; take--)
        {
          if (match_at(pattern, next, text, at + take))
          {
            return true;
          }
          if (take == 0)
          {
            break;
          }
        }
        return false;
      }
      if (quantifier == '+')
      {
        if (max < 1)
        {
          return false;
        }
        for (size_t take = max;; take--)
        {
          if (match_at(pattern, next, text, at + take))
          {
            return true;
          }
          if (take == 1)
          {
            break;
          }
        }
        return false;
      }
      if (quantifier == '-')
      {
        // Reluctant: shortest first ('.*?', the form query files reach for).
        for (size_t take = 0; take <= max; take++)
        {
          if (match_at(pattern, next, text, at + take))
          {
            return true;
          }
        }
        return false;
      }
      if (max < 1)
      {
        return false;
      }
      return match_at(pattern, next, text, at + 1);
    }

#ifdef JOT_TREESITTER
    // ---------------------------------------------------------------------
    // Predicates
    // ---------------------------------------------------------------------

    enum class Predicate
    {
      Eq,
      Match,
      LuaMatch,
      Contains,
      AnyOf,
      KindEq,
      HasParent,
      HasAncestor,
      Unknown,
    };

    struct PredicateName
    {
      Predicate predicate = Predicate::Unknown;
      bool any = false;    // any- prefix: any bound node may satisfy it
      bool negate = false; // not- prefix
    };

    PredicateName parse_predicate_name(const std::string &name)
    {
      PredicateName parsed;
      // `any-of?` is a predicate of its own -- "the node's text is one of
      // these" -- and not the `any-` prefix, which asks whether any one bound
      // node satisfies what follows. Stripping the prefix first left its name
      // reading as the unknown `of?`, and an unknown name is satisfied, so
      // every node the pattern matched took the capture.
      if (name == "any-of?" || name == "not-any-of?")
      {
        parsed.predicate = Predicate::AnyOf;
        parsed.any = true;
        parsed.negate = name[0] == 'n';
        return parsed;
      }
      std::string rest = name;
      if (rest.rfind("not-", 0) == 0)
      {
        parsed.negate = true;
        rest = rest.substr(4);
      }
      if (rest.rfind("any-", 0) == 0)
      {
        parsed.any = true;
        rest = rest.substr(4);
      }
      if (rest == "eq?")
      {
        parsed.predicate = Predicate::Eq;
      }
      else if (rest == "match?" || rest == "vim-match?")
      {
        parsed.predicate = Predicate::Match;
      }
      else if (rest == "lua-match?")
      {
        parsed.predicate = Predicate::LuaMatch;
      }
      else if (rest == "contains?")
      {
        parsed.predicate = Predicate::Contains;
      }
      else if (rest == "kind-eq?")
      {
        parsed.predicate = Predicate::KindEq;
      }
      else if (rest == "has-parent?")
      {
        parsed.predicate = Predicate::HasParent;
      }
      else if (rest == "has-ancestor?")
      {
        parsed.predicate = Predicate::HasAncestor;
      }
      return parsed;
    }

    bool regex_search_ok(const std::string &pattern, const std::string &text)
    {
      try
      {
        const std::regex re(pattern, std::regex::ECMAScript);
        return std::regex_search(text, re);
      }
      catch (const std::regex_error &)
      {
        // A pattern the C++ engine refuses: treat it as satisfied, the same as
        // a predicate this file does not know. Structural diagnostics already
        // report the query as loaded.
        return true;
      }
    }

    std::string node_text(const TextReader &text, TSNode node)
    {
      if (!text)
      {
        return "";
      }
      return text(ts_node_start_point(node), ts_node_end_point(node));
    }

    // Whether one node satisfies the predicate, ignoring the any-/not- wrappers.
    bool node_satisfies(Predicate predicate,
                        const std::vector<Arg> &args,
                        const TSQuery *query,
                        const TSQueryMatch &match,
                        const TextReader &text,
                        TSNode node)
    {
      const std::string value = node_text(text, node);
      switch (predicate)
      {
      case Predicate::Eq:
      case Predicate::Match:
      case Predicate::LuaMatch:
      case Predicate::Contains:
      case Predicate::AnyOf:
        break;
      case Predicate::KindEq:
      {
        const std::string kind = ts_node_type(node);
        for (size_t i = 1; i < args.size(); i++)
        {
          if (args[i].value == kind)
          {
            return true;
          }
        }
        return false;
      }
      case Predicate::HasParent:
      {
        const TSNode parent = ts_node_parent(node);
        if (node_is_null(parent))
        {
          return false;
        }
        const std::string kind = ts_node_type(parent);
        for (size_t i = 1; i < args.size(); i++)
        {
          if (args[i].value == kind)
          {
            return true;
          }
        }
        return false;
      }
      case Predicate::HasAncestor:
      {
        TSNode parent = ts_node_parent(node);
        while (!node_is_null(parent))
        {
          const std::string kind = ts_node_type(parent);
          for (size_t i = 1; i < args.size(); i++)
          {
            if (args[i].value == kind)
            {
              return true;
            }
          }
          parent = ts_node_parent(parent);
        }
        return false;
      }
      default:
        return true;
      }

      if (!text)
      {
        return true; // nothing to compare the text against
      }
      if (args.size() < 2)
      {
        return true;
      }
      // The second argument is either another capture (compared as text) or a
      // literal.
      if (args[1].capture)
      {
        const std::vector<TSNode> others = nodes_for_capture(query, match, args[1].value);
        if (others.empty())
        {
          return false;
        }
        const std::string other = node_text(text, others.front());
        switch (predicate)
        {
        case Predicate::Eq:
          return value == other;
        case Predicate::Match:
          return regex_search_ok(other, value);
        case Predicate::LuaMatch:
          return lua_pattern_match(other, value);
        default:
          return false;
        }
      }
      switch (predicate)
      {
      case Predicate::Eq:
        return value == args[1].value;
      case Predicate::AnyOf:
      {
        for (size_t i = 1; i < args.size(); i++)
        {
          if (value == args[i].value)
          {
            return true;
          }
        }
        return false;
      }
      case Predicate::Contains:
      {
        for (size_t i = 1; i < args.size(); i++)
        {
          if (value.find(args[i].value) == std::string::npos)
          {
            return false;
          }
        }
        return true;
      }
      case Predicate::Match:
        return regex_search_ok(args[1].value, value);
      case Predicate::LuaMatch:
        return lua_pattern_match(args[1].value, value);
      default:
        return true;
      }
    }

    bool evaluate(PredicateName parsed,
                  const std::vector<Arg> &args,
                  const TSQuery *query,
                  const TSQueryMatch &match,
                  const TextReader &text)
    {
      if (parsed.predicate == Predicate::Unknown)
      {
        return true;
      }
      if (args.empty() || !args[0].capture)
      {
        // A predicate whose first argument is not a capture is malformed, and
        // the capture-less predicates the query grammar cannot express are the
        // ones this file ignores anyway.
        return true;
      }
      const std::vector<TSNode> nodes = nodes_for_capture(query, match, args[0].value);
      if (nodes.empty())
      {
        // The capture did not bind in this match: a quantifier with a minimum
        // of zero is the usual reason, and there is nothing to test.
        return true;
      }
      // Seeded false either way: the any- branch sets it only when a node
      // satisfies the test, and the plain branch flips it to false when one
      // does not. Seeding it from `parsed.any` made every any- predicate
      // satisfied without a single node being tested.
      bool satisfied = false;
      if (parsed.any)
      {
        for (const TSNode &node : nodes)
        {
          if (node_satisfies(parsed.predicate, args, query, match, text, node))
          {
            satisfied = true;
            break;
          }
        }
      }
      else
      {
        satisfied = true;
        for (const TSNode &node : nodes)
        {
          if (!node_satisfies(parsed.predicate, args, query, match, text, node))
          {
            satisfied = false;
            break;
          }
        }
      }
      return parsed.negate ? !satisfied : satisfied;
    }
#endif // JOT_TREESITTER
  } // namespace

  bool lua_pattern_match(const std::string &pattern, const std::string &text)
  {
    if (pattern.empty())
    {
      return true;
    }
    // `^` anchors to the start of the subject; without it, `#lua-match?` scans,
    // the way Lua's `string.find` does (and the way Neovim's predicate is
    // documented: "the pattern must match somewhere in the node text").
    const bool anchored = pattern[0] == '^';
    const size_t start = anchored ? 1 : 0;
    if (anchored)
    {
      return match_at(pattern, start, text, 0);
    }
    for (size_t i = 0; i <= text.size(); i++)
    {
      if (match_at(pattern, start, text, i))
      {
        return true;
      }
    }
    return false;
  }

#ifdef JOT_TREESITTER
  bool match_satisfies(const TSQuery *query, const TSQueryMatch &match, const TextReader &text)
  {
    if (!query)
    {
      return true;
    }
    const std::vector<Form> forms = forms_of(query, match.pattern_index);
    for (const Form &form : forms)
    {
      if (form.directive())
      {
        continue;
      }
      if (!evaluate(parse_predicate_name(form.name), form.args, query, match, text))
      {
        return false;
      }
    }
    return true;
  }

  CaptureDirectives capture_directives(const TSQuery *query,
                                       const TSQueryMatch &match,
                                       const std::string &capture_name)
  {
    CaptureDirectives out;
    if (!query)
    {
      return out;
    }
    const std::vector<Form> forms = forms_of(query, match.pattern_index);
    for (const Form &form : forms)
    {
      if (!form.directive())
      {
        continue;
      }
      if (form.name == "set!" && form.args.size() >= 2)
      {
        if (form.args[0].capture)
        {
          // `(#set! @capture key value)`
          if (form.args[0].value != capture_name || form.args.size() < 3)
          {
            continue;
          }
          if (form.args[1].value == "priority")
          {
            out.has_priority = true;
            out.priority = (int)std::strtol(form.args[2].value.c_str(), nullptr, 10);
          }
          continue;
        }
        // `(#set! priority 110)`: every capture in the pattern.
        if (form.args[0].value == "priority")
        {
          out.has_priority = true;
          out.priority = (int)std::strtol(form.args[1].value.c_str(), nullptr, 10);
        }
        continue;
      }
      if (form.name == "offset!" && form.args.size() >= 5 && form.args[0].capture &&
          form.args[0].value == capture_name)
      {
        out.has_offset = true;
        out.start_row = (int)std::strtol(form.args[1].value.c_str(), nullptr, 10);
        out.start_column = (int)std::strtol(form.args[2].value.c_str(), nullptr, 10);
        out.end_row = (int)std::strtol(form.args[3].value.c_str(), nullptr, 10);
        out.end_column = (int)std::strtol(form.args[4].value.c_str(), nullptr, 10);
      }
    }
    return out;
  }

  bool pattern_has_predicates(const TSQuery *query, uint32_t pattern_index)
  {
    if (!query)
    {
      return false;
    }
    const std::vector<Form> forms = forms_of(query, pattern_index);
    for (const Form &form : forms)
    {
      if (!form.directive())
      {
        return true;
      }
    }
    return false;
  }

  bool query_has_conditions(const TSQuery *query)
  {
    if (!query)
    {
      return false;
    }
    const uint32_t patterns = ts_query_pattern_count(query);
    for (uint32_t i = 0; i < patterns; i++)
    {
      if (!forms_of(query, i).empty())
      {
        return true;
      }
    }
    return false;
  }

#endif // JOT_TREESITTER

  ModelineResult parse_modeline(const std::string &source)
  {
    ModelineResult out;
    size_t pos = 0;
    while (pos < source.size())
    {
      size_t end = source.find('\n', pos);
      std::string line = source.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      pos = (end == std::string::npos) ? source.size() : end + 1;
      // Trim leading whitespace and the trailing CR of a CRLF file.
      const size_t first = line.find_first_not_of(" \t");
      if (first == std::string::npos)
      {
        continue; // blank line inside the header
      }
      line = line.substr(first);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      {
        line.pop_back();
      }
      // A modeline is a comment line. The query grammar's own comments are
      // single-semicolon; the doubled form is the convention these files use.
      if (line.rfind(";;", 0) != 0)
      {
        break;
      }
      std::string body = line.substr(2);
      const size_t body_first = body.find_first_not_of(" \t");
      body = (body_first == std::string::npos) ? std::string() : body.substr(body_first);
      while (!body.empty() && (body.back() == ' ' || body.back() == '\t'))
      {
        body.pop_back();
      }
      if (body == "extends")
      {
        out.kind = Modeline::Extends;
      }
      else if (body.rfind("inherits:", 0) == 0)
      {
        out.kind = Modeline::Inherits;
        std::string rest = body.substr(std::strlen("inherits:"));
        size_t item_start = 0;
        while (item_start <= rest.size())
        {
          const size_t comma = rest.find(',', item_start);
          std::string item = rest.substr(item_start, comma == std::string::npos
                                                          ? std::string::npos
                                                          : comma - item_start);
          const size_t item_first = item.find_first_not_of(" \t");
          item = (item_first == std::string::npos) ? std::string() : item.substr(item_first);
          while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
          {
            item.pop_back();
          }
          if (!item.empty())
          {
            bool optional = item.size() >= 2 && item.front() == '(' && item.back() == ')';
            if (optional)
            {
              item = item.substr(1, item.size() - 2);
            }
            out.languages.push_back(item);
            out.optional.push_back(optional);
          }
          if (comma == std::string::npos)
          {
            break;
          }
          item_start = comma + 1;
        }
      }
      // Any other `;;` line is a plain comment.
    }
    return out;
  }
} // namespace jot_query
