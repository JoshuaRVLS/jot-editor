#include "features/color_definitions.h"

#include <algorithm>
#include <cctype>

namespace
{
  bool is_name_char(char c)
  {
    return std::isalnum((unsigned char)c) || c == '_' || c == '-';
  }

  // Everything after a comment marker is not code: "// line" and "/* block */".
  std::string strip_comments(const std::string &line)
  {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size();)
    {
      if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')
      {
        break;
      }
      if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*')
      {
        const size_t close = line.find("*/", i + 2);
        if (close == std::string::npos)
        {
          break;
        }
        out += ' ';
        i = close + 2;
        continue;
      }
      out += line[i];
      i++;
    }
    return out;
  }

  // Trims a definition's value: trailing ";", whitespace, and a trailing
  // "!important" are all noise for the colour it names.
  std::string trim_value(const std::string &raw)
  {
    std::string v = raw;
    const size_t bang = v.find("!important");
    if (bang != std::string::npos)
    {
      v = v.substr(0, bang);
    }
    while (!v.empty() && (std::isspace((unsigned char)v.back()) || v.back() == ';'))
    {
      v.pop_back();
    }
    size_t start = 0;
    while (start < v.size() && std::isspace((unsigned char)v[start]))
    {
      start++;
    }
    return v.substr(start);
  }

  // If the value is a reference to another variable, reports which: a CSS
  // var(--x) or a Sass $x. The returned name excludes the sigil.
  bool reference_target(const std::string &value, bool sass, std::string &name)
  {
    if (sass)
    {
      if (value.empty() || value[0] != '$')
      {
        return false;
      }
      size_t n = 1;
      while (n < value.size() && is_name_char(value[n]))
      {
        n++;
      }
      if (n == 1)
      {
        return false;
      }
      name = value.substr(1, n - 1);
      return true;
    }
    if (value.compare(0, 4, "var(") != 0)
    {
      return false;
    }
    size_t i = 4;
    while (i < value.size() && std::isspace((unsigned char)value[i]))
    {
      i++;
    }
    if (i + 1 >= value.size() || value[i] != '-' || value[i + 1] != '-')
    {
      return false;
    }
    i += 2;
    size_t n = i;
    while (n < value.size() && is_name_char(value[n]))
    {
      n++;
    }
    if (n == i)
    {
      return false;
    }
    name = value.substr(i, n - i);
    return true;
  }

  // "R,G,B" -- the Catppuccin-style custom property that holds channels rather
  // than a colour literal.
  bool parse_rgb_triplet(const std::string &value, std::uint32_t &rgb)
  {
    int channels[3] = {0, 0, 0};
    size_t i = 0;
    for (int c = 0; c < 3; c++)
    {
      while (i < value.size() && std::isspace((unsigned char)value[i]))
      {
        i++;
      }
      const size_t start = i;
      while (i < value.size() && std::isdigit((unsigned char)value[i]))
      {
        i++;
      }
      if (i == start)
      {
        return false;
      }
      const int v = std::atoi(value.substr(start, i - start).c_str());
      channels[c] = std::clamp(v, 0, 255);
      while (i < value.size() && std::isspace((unsigned char)value[i]))
      {
        i++;
      }
      if (c < 2)
      {
        if (i >= value.size() || value[i] != ',')
        {
          return false;
        }
        i++;
      }
    }
    // Anything but whitespace after the third channel means this is not a triplet.
    while (i < value.size() && std::isspace((unsigned char)value[i]))
    {
      i++;
    }
    if (i != value.size())
    {
      return false;
    }
    rgb = ((std::uint32_t)channels[0] << 16) | ((std::uint32_t)channels[1] << 8)
          | (std::uint32_t)channels[2];
    return true;
  }

  struct RawDef
  {
    std::string name;
    std::string value;
    bool sass = false;
  };

  // Finds every "name: value" definition on a line, for both sigils. A line can
  // hold more than one (Sass allows "$a: #fff; $b: $a;").
  void find_definitions(const std::string &line, std::vector<RawDef> &out)
  {
    const std::string text = strip_comments(line);
    for (size_t i = 0; i + 1 < text.size(); i++)
    {
      const char c = text[i];
      const bool is_css =
          (c == '-' && text[i + 1] == '-') && (i == 0 || !is_name_char(text[i - 1]));
      const bool is_sass = (c == '$') && (i == 0 || !is_name_char(text[i - 1]));
      if (!is_css && !is_sass)
      {
        continue;
      }
      size_t p = i + (is_css ? 2 : 1);
      const size_t name_start = p;
      while (p < text.size() && is_name_char(text[p]))
      {
        p++;
      }
      if (p == name_start)
      {
        continue;
      }
      const std::string name = text.substr(name_start, p - name_start);
      while (p < text.size() && std::isspace((unsigned char)text[p]))
      {
        p++;
      }
      if (p >= text.size() || text[p] != ':')
      {
        continue;
      }
      p++;
      RawDef def;
      def.name = name;
      def.value = trim_value(text.substr(p));
      def.sass = is_sass;
      if (!def.value.empty())
      {
        out.push_back(def);
      }
      i = p; // do not re-scan the value for further definitions on this pass
    }
  }
} // namespace

namespace jot_color
{
  void Definitions::rebuild(const std::vector<std::string> &lines, const Options &options)
  {
    css_.clear();
    sass_.clear();

    std::vector<RawDef> raw;
    for (const auto &line : lines)
    {
      find_definitions(line, raw);
    }

    // Pass 1: direct values, and remember the references for the resolution pass.
    std::unordered_map<std::string, std::string> css_pending;
    std::unordered_map<std::string, std::string> sass_pending;
    for (const auto &def : raw)
    {
      std::string target;
      if (reference_target(def.value, def.sass, target))
      {
        (def.sass ? sass_pending : css_pending)[def.name] = target;
        continue;
      }
      std::uint32_t rgb = 0;
      // The value is scanned with the same scanners the line itself uses, so
      // "--x: #fff" and "--x: rgb(0,0,0)" both work, and a disabled parser stops
      // defining variables of that shape.
      const auto spans = scan_line(def.value, -1, options, nullptr);
      if (!spans.empty())
      {
        rgb = spans.front().rgb;
      }
      else if (parse_rgb_triplet(def.value, rgb))
      {
        // handled
      }
      else
      {
        continue;
      }
      (def.sass ? sass_ : css_)[def.name] = rgb;
    }

    // Pass 2: follow chains, with a depth cap that also breaks cycles.
    auto resolve = [](std::unordered_map<std::string, std::uint32_t> &resolved,
                      const std::unordered_map<std::string, std::string> &pending)
    {
      for (const auto &entry : pending)
      {
        std::string name = entry.first;
        for (int hop = 0; hop < 16; hop++)
        {
          const auto value = resolved.find(name);
          if (value != resolved.end())
          {
            resolved[entry.first] = value->second;
            break;
          }
          const auto next = pending.find(name);
          if (next == pending.end())
          {
            break;
          }
          name = next->second;
        }
      }
    };
    resolve(css_, css_pending);
    resolve(sass_, sass_pending);
  }

  bool Definitions::lookup_css(const std::string &name, std::uint32_t &rgb) const
  {
    const auto it = css_.find(name);
    if (it == css_.end())
    {
      return false;
    }
    rgb = it->second;
    return true;
  }

  bool Definitions::lookup_sass(const std::string &name, std::uint32_t &rgb) const
  {
    const auto it = sass_.find(name);
    if (it == sass_.end())
    {
      return false;
    }
    rgb = it->second;
    return true;
  }
} // namespace jot_color
