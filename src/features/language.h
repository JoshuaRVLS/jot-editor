#ifndef JOT_FEATURES_LANGUAGE_H
#define JOT_FEATURES_LANGUAGE_H

#include <string>

// Per-language classification shared across editing, folding and rendering.
// Keep file-extension policies here so call sites never re-implement them.
namespace Language
{
  bool is_python_file(const std::string &path);
  bool is_lua_file(const std::string &path);
  // Indentation-significant languages (blocks are spelled with indentation
  // rather than braces): used by folding and auto-indent.
  bool is_indentation_language(const std::string &extension);
}

#endif // JOT_FEATURES_LANGUAGE_H