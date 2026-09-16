// The per-language textobject node table. See textobjects.h for why it exists.
#include "features/textobjects.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace
{
  using jot_textobjects::Names;

  std::string lower(std::string s)
  {
    std::transform(
        s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
  }

  // Strip a leading dot and a trailing anything-after-a-dot, so ".cpp", "cpp"
  // and "C++" style spellings all land on the same key.
  std::string key_for(const std::string &raw)
  {
    std::string key = lower(raw);
    if (!key.empty() && key[0] == '.')
    {
      key.erase(key.begin());
    }
    return key;
  }
} // namespace

namespace jot_textobjects
{
  Names names_for_extension(const std::string &extension_or_language)
  {
    const std::string key = key_for(extension_or_language);

    // C family. The declarator wraps the identifier in a function_definition, so
    // both are listed: which one the cursor is inside depends on the position.
    if (key == "c" || key == "h")
    {
      return {{"function_definition", "function_declarator"},
              {"struct_specifier", "union_specifier", "enum_specifier"},
              {"parameter_declaration", "parameter_list", "variadic_parameter"},
              {"comment"}};
    }
    if (key == "cpp" || key == "cc" || key == "cxx" || key == "hpp" || key == "hh" || key == "hxx"
        || key == "c++" || key == "cppm")
    {
      return {{"function_definition", "function_declarator", "template_declaration"},
              {"class_specifier", "struct_specifier", "union_specifier", "enum_specifier"},
              {"parameter_declaration", "parameter_list", "variadic_parameter_declaration"},
              {"comment"}};
    }

    // JavaScript family (tsx shares the names).
    if (key == "js" || key == "jsx" || key == "javascript" || key == "mjs" || key == "cjs")
    {
      return {{"function_declaration",
               "function_expression",
               "arrow_function",
               "generator_function_declaration",
               "method_definition"},
              {"class_declaration", "class"},
              {"formal_parameters", "identifier", "assignment_pattern"},
              {"comment"}};
    }
    if (key == "ts" || key == "tsx" || key == "typescript")
    {
      return {{"function_declaration",
               "function_expression",
               "arrow_function",
               "generator_function_declaration",
               "method_definition",
               "method_signature"},
              {"class_declaration",
               "interface_declaration",
               "type_alias_declaration",
               "enum_declaration",
               "abstract_class_declaration"},
              {"required_parameter", "optional_parameter", "formal_parameters"},
              {"comment"}};
    }

    if (key == "py" || key == "python" || key == "pyi")
    {
      return {{"function_definition"},
              {"class_definition"},
              {"parameters",
               "default_parameter",
               "typed_parameter",
               "typed_default_parameter",
               "identifier"},
              {"comment"}};
    }

    if (key == "rs" || key == "rust")
    {
      return {{"function_item", "closure_expression"},
              {"struct_item", "enum_item", "impl_item", "trait_item", "union_item"},
              {"parameters", "parameter", "self_parameter", "variadic_parameter"},
              {"line_comment", "block_comment", "doc_comment"}};
    }

    if (key == "go" || key == "golang")
    {
      return {{"function_declaration", "method_declaration", "func_literal"},
              {"type_declaration", "type_spec", "struct_type", "interface_type"},
              {"parameter_list", "parameter_declaration", "variadic_parameter_declaration"},
              {"comment"}};
    }

    if (key == "lua")
    {
      return {{"function_declaration", "function_definition"},
              {},
              {"parameters", "identifier"},
              {"comment"}};
    }

    if (key == "java")
    {
      return {{"method_declaration", "constructor_declaration", "lambda_expression"},
              {"class_declaration",
               "interface_declaration",
               "enum_declaration",
               "record_declaration",
               "annotation_type_declaration"},
              {"formal_parameters", "formal_parameter", "spread_parameter"},
              {"line_comment", "block_comment"}};
    }

    if (key == "cs" || key == "c_sharp" || key == "csharp")
    {
      return {{"method_declaration", "constructor_declaration", "local_function_statement"},
              {"class_declaration",
               "struct_declaration",
               "interface_declaration",
               "enum_declaration",
               "record_declaration"},
              {"parameter_list", "parameter"},
              {"comment"}};
    }

    if (key == "rb" || key == "ruby")
    {
      return {{"method", "singleton_method", "lambda"},
              {"class", "module", "singleton_class"},
              {"method_parameters", "identifier", "optional_parameter"},
              {"comment"}};
    }

    if (key == "php")
    {
      return {
          {"function_definition", "method_declaration", "anonymous_function_creation_expression"},
          {"class_declaration", "interface_declaration", "trait_declaration", "enum_declaration"},
          {"formal_parameters", "simple_parameter", "variadic_parameter"},
          {"comment"}};
    }

    if (key == "kt" || key == "kotlin")
    {
      return {{"function_declaration", "anonymous_function"},
              {"class_declaration", "object_declaration", "interface_declaration"},
              {"parameter", "function_value_parameters", "class_parameter"},
              {"line_comment", "multiline_comment"}};
    }

    if (key == "sh" || key == "bash" || key == "zsh" || key == "shell")
    {
      return {{"function_definition"}, {}, {}, {"comment"}};
    }

    // Unknown language: the C-like names cover most grammars, and a wrong name
    // costs only a no-op (nothing matches, the selection does not move).
    return {
        {"function_definition", "function_declaration", "function_item"},
        {"class_declaration",
         "class_definition",
         "struct_item",
         "struct_specifier",
         "class_specifier"},
        {"parameters", "parameter_list", "formal_parameters", "parameter", "parameter_declaration"},
        {"comment", "line_comment", "block_comment"}};
  }

  const std::vector<std::string> &body_fields()
  {
    // Every grammar that has a body node names the field "body".
    static const std::vector<std::string> fields = {"body"};
    return fields;
  }

  const std::vector<std::string> &argument_inner_fields()
  {
    // A parameter's own name, per grammar: C/C++ "declarator", TypeScript/Rust
    // "pattern", Go/Python "name", Ruby "left" (in its assignment-like form).
    static const std::vector<std::string> fields = {"declarator", "pattern", "name", "left"};
    return fields;
  }
} // namespace jot_textobjects
