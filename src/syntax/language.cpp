#include "syntax/language.h"

#include <algorithm>
#include <cctype>

namespace
{
  std::string lower_extension(const std::string &path)
  {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
      return "";
    std::string ext = path.substr(dot);
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
  }
} // namespace

namespace Language
{
  bool is_python_file(const std::string &path)
  {
    return lower_extension(path) == ".py";
  }

  bool is_lua_file(const std::string &path)
  {
    return lower_extension(path) == ".lua";
  }

  bool is_c_family_file(const std::string &path)
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
    const std::string ext = lower_extension(path);
    if (ext.empty())
    {
      return false;
    }
    for (const char *candidate : kExtensions)
    {
      if (ext == candidate)
      {
        return true;
      }
    }
    return false;
  }

  bool is_code_extension(const std::string &extension)
  {
    std::string ext = extension;
    if (!ext.empty() && ext[0] == '.')
    {
      ext.erase(0, 1);
    }
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    static const char *const kCodeExtensions[] = {
        // C family and friends
        "c",
        "cc",
        "cxx",
        "cpp",
        "c++",
        "h",
        "hh",
        "hpp",
        "hxx",
        "m",
        "mm",
        "cs",
        "java",
        "kt",
        "kts",
        "scala",
        "swift",
        "dart",
        "groovy",
        // Scripting
        "py",
        "pyi",
        "lua",
        "rb",
        "php",
        "pl",
        "pm",
        "r",
        "jl",
        "tcl",
        "vim",
        "sh",
        "bash",
        "zsh",
        "fish",
        "ps1",
        "psm1",
        "bat",
        "cmd",
        "awk",
        "el",
        // Web and JS
        "js",
        "mjs",
        "cjs",
        "jsx",
        "ts",
        "tsx",
        "vue",
        "svelte",
        "astro",
        "html",
        "htm",
        "css",
        "scss",
        "sass",
        "less",
        "styl",
        "graphql",
        "gql",
        // Systems and functional
        "go",
        "rs",
        "zig",
        "nim",
        "cr",
        "hs",
        "lhs",
        "ml",
        "mli",
        "fs",
        "fsx",
        "fsi",
        "ex",
        "exs",
        "erl",
        "hrl",
        "clj",
        "cljs",
        "cljc",
        "edn",
        "elm",
        "lisp",
        "scm",
        "rkt",
        "sql",
        "proto",
        "thrift",
        // Build / config that parses as code in the index
        "cmake",
        "mk",
        "make",
        "gradle",
        "bazel",
        "toml",
        "yaml",
        "yml",
        "json",
        "jsonc",
    };
    for (const char *candidate : kCodeExtensions)
    {
      if (ext == candidate)
      {
        return true;
      }
    }
    return false;
  }

  bool is_code_file(const std::string &path)
  {
    const std::string ext = lower_extension(path);
    if (ext.empty())
    {
      return false;
    }
    return is_code_extension(ext);
  }

  bool is_indentation_language(const std::string &extension)
  {
    std::string ext = extension;
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    // Lua code is conventionally indented, so keyword blocks fold like
    // Python's indentation blocks.
    return ext == ".py" || ext == ".yaml" || ext == ".yml" || ext == ".md" || ext == ".markdown"
           || ext == ".lua";
  }
} // namespace Language
