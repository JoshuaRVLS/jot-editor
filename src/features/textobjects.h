#pragma once

// Which tree-sitter node types mean "function", "class", "argument" and
// "comment" in a given language.
//
// Helix gets this from a hand-written `textobjects.scm` per grammar, and the
// nvim plugins do the same with a per-language table. jot bundles highlight
// queries only (no textobjects.scm anywhere), so the table lives here: the same
// shape, in C++, kept deliberately free of any tree-sitter types so it can be
// tested without a parser loaded.
//
// Names come from the grammars themselves: C and C++ declare `function_definition`,
// JavaScript `function_declaration`, Rust `function_item`, Python
// `function_definition` again. Missing a name means the textobject silently does
// nothing for that language, so the fallback list at the bottom covers the
// C-like naming most grammars share.

#include <string>
#include <vector>

namespace jot_textobjects
{
  struct Names
  {
    std::vector<std::string> function;
    std::vector<std::string> klass;
    std::vector<std::string> argument;
    std::vector<std::string> comment;

    bool empty() const
    {
      return function.empty() && klass.empty() && argument.empty() && comment.empty();
    }

    bool is_function(const std::string &type) const
    {
      return contains(function, type);
    }

    bool is_class(const std::string &type) const
    {
      return contains(klass, type);
    }

    bool is_argument(const std::string &type) const
    {
      return contains(argument, type);
    }

    bool is_comment(const std::string &type) const
    {
      return contains(comment, type);
    }

    bool is_any(const std::string &type) const
    {
      return is_function(type) || is_class(type) || is_argument(type) || is_comment(type);
    }

  private:
    static bool contains(const std::vector<std::string> &list, const std::string &type)
    {
      for (const auto &name : list)
      {
        if (name == type)
        {
          return true;
        }
      }
      return false;
    }
  };

  // Extension (".cpp") or language id ("cpp"); both spellings are accepted since
  // the callers have one or the other.
  Names names_for_extension(const std::string &extension_or_language);

  // Field names to try, in order, when a textobject wants the *inside* of a node:
  // the body of a function or class, or the declared name of a parameter.
  // "around" is the node itself.
  const std::vector<std::string> &body_fields();
  const std::vector<std::string> &argument_inner_fields();
} // namespace jot_textobjects
