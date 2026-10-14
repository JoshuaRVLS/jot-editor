#ifndef JOT_CORE_OPTIONS_H
#define JOT_CORE_OPTIONS_H

#include <cctype>
#include <string>

// Which options a buffer or a window can hold a value of its own for.
//
// Most settings are global: one value for the session, shared by every buffer
// and written to settings.conf. A few are properties of one buffer (the width
// it indents by) or of one window (which gutter decorations it shows), and for
// those a local value can be set with jot.buffer.set_option /
// jot.window.set_option. The code that uses such a key reads the local value
// through the option_int / option_bool / option_str helpers below, and the two
// namespaces and `jot.config.get(key, {scope = ...})` answer it too; the global
// value stays where it was until it is changed itself.
//
// This list and the scope field each entry carries in the settings catalog
// (runtime/lua/features/settings/catalog.lua) are the same facts, kept in step
// by tools/settings_catalog_import.py. The native list is the fallback for a
// build with no Lua runtime, which is also the build where the refusal of a
// scope a key does not take has to work, so it cannot live only in the catalog.
namespace jot_options
{
  enum class Scope
  {
    Global,
    Buffer,
    Window,
  };

  // Keys a buffer may carry its own value for. Everything here is read with the
  // buffer in hand (indentation, the render walk), so a value travels with the
  // buffer it was set on.
  inline const char *const *buffer_keys(std::size_t &count)
  {
    static const char *const kKeys[] = {
        "tab_size",
        "auto_indent",
    };
    count = sizeof(kKeys) / sizeof(kKeys[0]);
    return kKeys;
  }

  // Keys a window (a pane here) may carry its own value for: the decorations
  // that are a property of the view rather than of the text in it.
  inline const char *const *window_keys(std::size_t &count)
  {
    static const char *const kKeys[] = {
        "relative_line_numbers",
        "show_indent_guides",
        "winbar",
    };
    count = sizeof(kKeys) / sizeof(kKeys[0]);
    return kKeys;
  }

  inline bool in_list(const char *const *keys, std::size_t count, const std::string &key)
  {
    for (std::size_t i = 0; i < count; i++)
    {
      if (key == keys[i])
      {
        return true;
      }
    }
    return false;
  }

  inline Scope scope_of(const std::string &key)
  {
    std::size_t count = 0;
    const char *const *buffer = buffer_keys(count);
    if (in_list(buffer, count, key))
    {
      return Scope::Buffer;
    }
    const char *const *window = window_keys(count);
    if (in_list(window, count, key))
    {
      return Scope::Window;
    }
    return Scope::Global;
  }

  inline const char *scope_name(Scope scope)
  {
    switch (scope)
    {
    case Scope::Buffer:
      return "buffer";
    case Scope::Window:
      return "window";
    default:
      return "global";
    }
  }

  // The type a stored value reads as, for a key with no schema entry: what the
  // settings menu shows for a plugin's own key, and what jot.config.info
  // answers when the catalog has nothing to say. The schema wins when it does.
  inline std::string inferred_type_name(const std::string &value)
  {
    if (value == "true" || value == "false")
    {
      return "bool";
    }
    if (!value.empty())
    {
      bool numeric = true;
      for (char c : value)
      {
        if (!std::isdigit((unsigned char)c) && c != '-' && c != '.')
        {
          numeric = false;
          break;
        }
      }
      if (numeric)
      {
        return "int";
      }
    }
    return "string";
  }

  // The scope a config file or a Lua call named, or false when it is not one of
  // the three. `effective` is what a reader means by "the value in force".
  inline bool scope_from_name(const std::string &name, Scope &out)
  {
    if (name == "buffer" || name == "local")
    {
      out = Scope::Buffer;
      return true;
    }
    if (name == "window")
    {
      out = Scope::Window;
      return true;
    }
    if (name == "global" || name == "effective")
    {
      out = Scope::Global;
      return true;
    }
    return false;
  }
} // namespace jot_options

#endif
