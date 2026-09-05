#include "features/language.h"

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