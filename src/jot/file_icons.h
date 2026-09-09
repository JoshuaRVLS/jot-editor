#pragma once

// Shared file-type glyph + brand-color mapping used by the file explorer,
// home screen and status line so every surface shows the same icon for a
// file. Glyphs are Nerd Fonts codepoints (devicons / seti / material design
// families); colors are the closest ANSI-256 index to the language's brand
// color, chosen to stay readable on both dark and light status bars.
//
// The explorer previously owned its own extension table; keeping the palette
// here means the status line (which had only a generic file glyph before)
// now shows the same per-language icon as the sidebar, colored.

#include "tools/string_util.h"
#include <string>

namespace jot_icons
{
  struct FileTypeIcon
  {
    const char *glyph; // Nerd Fonts codepoint (UTF-8), no trailing space
    int color;         // ANSI-256 foreground suggestion, -1 = unset
  };

  namespace detail
  {
    inline std::string lower_copy(std::string s)
    {
      return string_util::lower_copy(std::move(s));
    }
  } // namespace detail

  // Glyph + color for a path. Unknown types fall back to a generic file
  // glyph (gray). Names are matched case-insensitively.
  inline FileTypeIcon file_type_icon(const std::string &path)
  {
    static const FileTypeIcon kDefault = {"\U000F0214", 244}; // 󰈔 file outline

    // Extension table: most-specific first is not needed (extensions are
    // unique), but well-known basenames (Dockerfile, Makefile, ...) are
    // matched before the extension lookup below.
    static const struct
    {
      const char *name;
      FileTypeIcon icon;
    } kBasenames[] = {
        {"dockerfile", {"\uE7B0", 39}},  // nf-dev-docker
        {"makefile", {"\uF013", 179}},
        {"gnumakefile", {"\uF013", 179}},
        {"cmakelists.txt", {"\uF013", 179}},
        {"cmakecache.txt", {"\uF013", 179}},
        {"justfile", {"\uF013", 179}},
    };
    static const struct
    {
      const char *ext;
      FileTypeIcon icon;
    } kExts[] = {
        // C family
        {"c", {"\uE61E", 75}},     // nf-dev-c
        {"cpp", {"\uE61D", 67}},   // nf-dev-cpp
        {"cc", {"\uE61D", 67}},
        {"cxx", {"\uE61D", 67}},
        {"h", {"\uF0FD", 140}},    // nf-fa-file-code-o
        {"hpp", {"\uF0FD", 140}},
        {"hh", {"\uF0FD", 140}},
        {"cs", {"\uE648", 75}},    // nf-dev-csharp
        // Scripting
        {"py", {"\uE606", 61}},    // nf-seti-python
        {"js", {"\uE74E", 221}},   // nf-dev-javascript
        {"mjs", {"\uE74E", 221}},
        {"cjs", {"\uE74E", 221}},
        {"jsx", {"\uE7BA", 81}},   // nf-dev-react
        {"ts", {"\uE628", 68}},    // nf-seti-typescript
        {"tsx", {"\uE7BA", 81}},
        {"lua", {"\uE620", 74}},   // nf-seti-lua
        {"rb", {"\uE791", 166}},   // nf-dev-ruby
        {"php", {"\uE608", 103}},  // nf-dev-php
        {"sh", {"\uE795", 113}},   // nf-dev-terminal
        {"bash", {"\uE795", 113}},
        {"zsh", {"\uE795", 113}},
        {"fish", {"\uE795", 113}},
        {"ps1", {"\uE795", 113}},
        {"pl", {"\uE769", 75}},
        {"pm", {"\uE769", 75}},
        {"r", {"\uE78F", 68}},
        {"go", {"\uE627", 38}},    // nf-seti-go
        {"rs", {"\uE7A8", 180}},   // nf-dev-rust
        {"java", {"\uE738", 130}}, // nf-dev-java
        {"kt", {"\uE634", 128}},   // nf-dev-kotlin
        {"kts", {"\uE634", 128}},
        {"swift", {"\uE755", 203}}, // nf-dev-swift
        {"dart", {"\uE798", 37}},   // nf-dev-dart
        {"ex", {"\uE62D", 60}},     // nf-dev-elixir
        {"exs", {"\uE62D", 60}},
        {"erl", {"\uE7B1", 167}},
        {"hrl", {"\uE7B1", 167}},
        {"hs", {"\uE777", 130}},
        {"lhs", {"\uE777", 130}},
        {"zig", {"\uE6A9", 221}},
        {"scala", {"\uE737", 160}},
        {"vue", {"\uE6A0", 72}},    // nf-dev-vue
        {"sql", {"\uE706", 172}},   // nf-dev-database
        // Web / markup
        {"html", {"\uF13B", 166}},  // nf-fa-html5
        {"htm", {"\uF13B", 166}},
        {"css", {"\uE749", 75}},    // nf-dev-css3
        {"scss", {"\uE603", 168}},  // nf-dev-sass
        {"sass", {"\uE603", 168}},
        {"less", {"\uE758", 75}},
        {"xml", {"\U000F05C0", 173}}, // nf-md-file-xml-box? material file-xml
        {"svg", {"\U000F021A", 215}}, // nf-md-file-image-outline
        {"json", {"\uE60B", 185}},    // nf-seti-json
        {"jsonc", {"\uE60B", 185}},
        {"md", {"\uE609", 67}},       // nf-seti-markdown
        {"markdown", {"\uE609", 67}},
        {"rst", {"\uE609", 67}},
        {"toml", {"\uF013", 130}},    // nf-fa-gear
        {"yaml", {"\uE615", 160}},    // nf-seti-yaml
        {"yml", {"\uE615", 160}},
        {"ini", {"\uF013", 179}},
        {"conf", {"\uF013", 179}},
        {"cfg", {"\uF013", 179}},
        {"env", {"\uF013", 179}},
        {"txt", {"\U000F0219", 245}}, // 󰈙 file document outline
        {"log", {"\U000F0219", 245}},
        {"csv", {"\uE615", 113}},
        {"tsv", {"\uE615", 113}},
        // Locked / archives / media
        {"lock", {"\U000F033E", 140}}, // 󰌾 lock
        {"zip", {"\U000F0216", 140}},
        {"gz", {"\U000F0216", 140}},
        {"tar", {"\U000F0216", 140}},
        {"pdf", {"\U000F021A", 203}},
        {"png", {"\U000F021A", 215}},
        {"jpg", {"\U000F021A", 215}},
        {"jpeg", {"\U000F021A", 215}},
        {"gif", {"\U000F021A", 215}},
        {"ico", {"\U000F021A", 215}},
    };

    std::string name = detail::lower_copy(path);
    // Match against the file's own name (Dockerfile, Makefile, ...).
    std::string::size_type slash = name.find_last_of("/\\");
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    for (const auto &entry : kBasenames)
    {
      if (base == entry.name)
      {
        return entry.icon;
      }
    }
    std::string::size_type dot = base.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= base.size())
    {
      return kDefault;
    }
    const std::string ext = base.substr(dot + 1);
    for (const auto &entry : kExts)
    {
      if (ext == entry.ext)
      {
        return entry.icon;
      }
    }
    return kDefault;
  }
} // namespace jot_icons
