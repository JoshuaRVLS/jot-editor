#ifndef JOT_FEATURES_LANGUAGE_H
#define JOT_FEATURES_LANGUAGE_H

#include <string>

// Per-language classification shared across editing, folding and rendering.
// Keep file-extension policies here so call sites never re-implement them.
namespace Language
{
  bool is_python_file(const std::string &path);
  bool is_lua_file(const std::string &path);
  // A C-family source file (C, C++, Objective-C), whose brace and label
  // conventions differ from the brace-on-the-same-line languages.
  bool is_c_family_file(const std::string &path);
  // A source file: one the breadcrumb winbar has something to say about
  // (symbols, a document outline). Data, prose and media files are not code,
  // and neither is a file without an extension -- `winbar = auto` keeps the
  // row for the panes that can use it.
  bool is_code_file(const std::string &path);
  // A known source extension, without the leading dot ("cpp").
  bool is_code_extension(const std::string &extension);
  // Indentation-significant languages (blocks are spelled with indentation
  // rather than braces): used by folding and auto-indent.
  bool is_indentation_language(const std::string &extension);
} // namespace Language

#endif // JOT_FEATURES_LANGUAGE_H