#include "features/color_codes.h"

#include "features/color_definitions.h"
#include "features/color_functions.h"
#include "features/color_names.h"
#include "features/color_tailwind.h"
#include "features/color_terminal_codes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
  inline bool is_hex_digit(char c)
  {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  inline int hex_value(char c)
  {
    if (c >= '0' && c <= '9')
    {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
      return c - 'a' + 10;
    }
    return c - 'A' + 10;
  }

  // Characters that bind a colour literal to its surroundings. A match may not
  // start or end inside one of these runs, which is what stops "red" matching in
  // "text-red-500" and "#fff" matching in "x#ffffff" (upstream reaches the same
  // place with its extra_word_chars option, defaulting to "-").
  inline bool is_word_char(char c)
  {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
           || c == '-';
  }

  // True when a token starting at `i` is a whole word (nothing word-ish before it).
inline bool at_word_start(const std::string &line, size_t i)
{
  return i == 0 || !is_word_char(line[i - 1]);
}

inline std::uint32_t pack(int r, int g, int b)
  {
    return ((std::uint32_t)(r & 0xFF) << 16) | ((std::uint32_t)(g & 0xFF) << 8)
           | (std::uint32_t)(b & 0xFF);
  }

  inline int clamp_channel(int v)
  {
    return std::clamp(v, 0, 255);
  }

  // hsl()/hsla() -> RGB. Standard CSS conversion; hue in turns, saturation and
  // lightness as fractions.
  std::uint32_t hsl_to_rgb(double hue_turns, double s, double l)
  {
    hue_turns = hue_turns - std::floor(hue_turns); // wrap into [0,1)
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);

    const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double hp = hue_turns * 6.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    double r = 0.0, g = 0.0, b = 0.0;
    if (hp < 1.0)
    {
      r = c;
      g = x;
    }
    else if (hp < 2.0)
    {
      r = x;
      g = c;
    }
    else if (hp < 3.0)
    {
      g = c;
      b = x;
    }
    else if (hp < 4.0)
    {
      g = x;
      b = c;
    }
    else if (hp < 5.0)
    {
      r = x;
      b = c;
    }
    else
    {
      r = c;
      b = x;
    }
    const double m = l - c * 0.5;
    return pack((int)std::lround((r + m) * 255.0),
                (int)std::lround((g + m) * 255.0),
                (int)std::lround((b + m) * 255.0));
  }

  struct NumberParser
  {
    const std::string &s;
    size_t i;
    size_t limit;

    void skip_spaces()
    {
      while (i < limit && (s[i] == ' ' || s[i] == '\t'))
      {
        i++;
      }
    }

    // Reads one CSS number: digits with an optional fractional part, or a leading
    // '.'. Returns false when the next token is not a number.
    bool number(double &out)
    {
      skip_spaces();
      if (i >= limit)
      {
        return false;
      }
      const size_t start = i;
      while (i < limit && (s[i] >= '0' && s[i] <= '9'))
      {
        i++;
      }
      if (i < limit && s[i] == '.')
      {
        i++;
        while (i < limit && (s[i] >= '0' && s[i] <= '9'))
        {
          i++;
        }
      }
      if (i == start || (i == start + 1 && s[start] == '.'))
      {
        i = start;
        return false;
      }
      out = std::strtod(s.substr(start, i - start).c_str(), nullptr);
      return true;
    }

    bool percentage(double &out)
    {
      skip_spaces();
      if (!number(out))
      {
        return false;
      }
      if (i < limit && s[i] == '%')
      {
        i++;
        return true;
      }
      return false;
    }

    // Consumes the separators CSS allows between arguments: commas and/or
    // whitespace, plus the '/' that introduces an alpha channel.
    void skip_separators()
    {
      skip_spaces();
      while (i < limit && (s[i] == ',' || s[i] == '/'))
      {
        i++;
        skip_spaces();
      }
    }
  };

  // rgb()/rgba(): three channels, decimal or percentage, then an ignored alpha.
  // Accepts both the comma and the space separated CSS syntax.
  bool parse_rgb_function(const std::string &line, size_t open, size_t limit, std::uint32_t &rgb)
  {
    NumberParser p{line, open + 1, limit};
    int channels[3] = {0, 0, 0};
    for (int c = 0; c < 3; c++)
    {
      if (c > 0)
      {
        p.skip_separators();
      }
      double v = 0.0;
      if (!p.number(v))
      {
        return false;
      }
      // An explicit percent sign on the channel scales 100% to full intensity.
      if (p.i < p.limit && p.s[p.i] == '%')
      {
        p.i++;
        channels[c] = clamp_channel((int)std::lround(v * 255.0 / 100.0));
      }
      else
      {
        channels[c] = clamp_channel((int)std::lround(v));
      }
    }
    // Optional alpha (ignored: the preview shows the opaque colour).
    p.skip_separators();
    if (p.i < p.limit && (p.s[p.i] == '.' || (p.s[p.i] >= '0' && p.s[p.i] <= '9')))
    {
      double alpha = 0.0;
      if (!p.number(alpha))
      {
        return false;
      }
      if (p.i < p.limit && p.s[p.i] == '%')
      {
        p.i++;
      }
    }
    p.skip_spaces();
    if (p.i >= p.limit || p.s[p.i] != ')')
    {
      return false;
    }
    rgb = pack(channels[0], channels[1], channels[2]);
    return true;
  }

  // hsl()/hsla(): hue with an optional angle unit, then saturation and lightness
  // percentages (required), then an ignored alpha.
  bool parse_hsl_function(const std::string &line, size_t open, size_t limit, std::uint32_t &rgb)
  {
    NumberParser p{line, open + 1, limit};
    double hue = 0.0;
    if (!p.number(hue))
    {
      return false;
    }
    double hue_turns = hue / 360.0;
    if (p.i < p.limit && (p.s[p.i] == 'd' || p.s[p.i] == 'D'))
    {
      // deg
      if (p.i + 2 < p.limit && p.s[p.i + 1] == 'e' && p.s[p.i + 2] == 'g')
      {
        p.i += 3;
      }
    }
    else if (p.i < p.limit && (p.s[p.i] == 't' || p.s[p.i] == 'T'))
    {
      // turn
      if (p.i + 3 < p.limit && p.s[p.i + 1] == 'u' && p.s[p.i + 2] == 'r' && p.s[p.i + 3] == 'n')
      {
        p.i += 4;
        hue_turns = hue;
      }
    }
    else if (p.i < p.limit && (p.s[p.i] == 'g' || p.s[p.i] == 'G'))
    {
      // grad (400 per turn)
      if (p.i + 3 < p.limit && p.s[p.i + 1] == 'r' && p.s[p.i + 2] == 'a' && p.s[p.i + 3] == 'd')
      {
        p.i += 4;
        hue_turns = hue / 400.0;
      }
    }
    else if (p.i < p.limit && (p.s[p.i] == 'r' || p.s[p.i] == 'R'))
    {
      // rad
      if (p.i + 2 < p.limit && p.s[p.i + 1] == 'a' && p.s[p.i + 2] == 'd')
      {
        p.i += 3;
        hue_turns = hue / (2.0 * M_PI);
      }
    }

    p.skip_separators();
    double sat = 0.0;
    if (!p.percentage(sat))
    {
      return false;
    }
    p.skip_separators();
    double light = 0.0;
    if (!p.percentage(light))
    {
      return false;
    }
    // Optional alpha (ignored).
    p.skip_separators();
    if (p.i < p.limit && (p.s[p.i] == '.' || (p.s[p.i] >= '0' && p.s[p.i] <= '9')))
    {
      double alpha = 0.0;
      if (!p.number(alpha))
      {
        return false;
      }
      if (p.i < p.limit && p.s[p.i] == '%')
      {
        p.i++;
      }
    }
    p.skip_spaces();
    if (p.i >= p.limit || p.s[p.i] != ')')
    {
      return false;
    }
    rgb = hsl_to_rgb(hue_turns, sat / 100.0, light / 100.0);
    return true;
  }
} // namespace

namespace jot_color
{
  DisplayMode parse_display_mode(const std::string &value)
  {
    if (value == "foreground")
    {
      return DisplayMode::Foreground;
    }
    if (value == "virtualtext" || value == "virtual_text")
    {
      return DisplayMode::VirtualText;
    }
    return DisplayMode::Background;
  }

  std::uint32_t expand_short_hex(const char *digits, int count)
  {
    // #RGB -> #RRGGBB by doubling each nibble; #RGBA drops the alpha.
    int v[4] = {0, 0, 0, 0};
    for (int i = 0; i < count && i < 4; i++)
    {
      v[i] = hex_value(digits[i]);
    }
    const int r = v[0] * 16 + v[0];
    const int g = v[1] * 16 + v[1];
    const int b = v[2] * 16 + v[2];
    return pack(r, g, b);
  }

  std::uint32_t contrast_text_color(std::uint32_t rgb)
  {
    // WCAG relative luminance, then black or white -- the same rule the caret
    // uses, and the same intent as upstream's bright_fg/dark_fg pair.
    auto linearize = [](double channel)
    { return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
    const double r = linearize(((rgb >> 16) & 0xFF) / 255.0);
    const double g = linearize(((rgb >> 8) & 0xFF) / 255.0);
    const double b = linearize((rgb & 0xFF) / 255.0);
    const double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return lum > 0.35 ? 0x000000u : 0xFFFFFFu;
  }

  std::vector<ColorSpan> scan_line(const std::string &line,
                                   int byte_limit,
                                   const Options &options,
                                   const std::vector<std::uint8_t> *scope,
                                   const Definitions *definitions)
  {
    std::vector<ColorSpan> spans;
    const size_t n = line.size();
    size_t limit = (byte_limit < 0) ? n : std::min(n, (size_t)byte_limit);

    auto allowed = [&](size_t i)
    {
      if (!scope)
      {
        return true;
      }
      return i < scope->size() && (*scope)[i] != 0;
    };
    auto prev_is_word_char = [&](size_t i) { return i > 0 && is_word_char(line[i - 1]); };

    size_t i = 0;
    while (i < limit)
    {
      if (!allowed(i))
      {
        i++;
        continue;
      }
      const char c = line[i];

      // --- Hex literals -----------------------------------------------------
      if (c == '#')
      {
        if (i > 0 && (is_hex_digit(line[i - 1]) || line[i - 1] == '#' || is_word_char(line[i - 1])))
        {
          i++;
          continue;
        }
        size_t run = 0;
        while (i + 1 + run < limit && is_hex_digit(line[i + 1 + run]))
        {
          run++;
        }
        const bool enabled = (run == 3 && options.hex3) || (run == 4 && options.hex4)
                             || (run == 6 && options.hex6)
                             || (run == 8 && (options.hex8 || options.hex_aarrggbb));
        // The byte after the run must not continue the token: "#abcdefg" is an
        // identifier, not a colour, and a longer run is handled by `enabled`.
        const size_t after = i + 1 + run;
        const bool clean_end = after >= limit || !is_word_char(line[after]);
        if (enabled && clean_end)
        {
          ColorSpan span;
          span.start = (int)i;
          span.len = (int)run + 1;
          if (run <= 4)
          {
            span.rgb = expand_short_hex(line.data() + i + 1, (int)run);
          }
          else if (run == 8 && !options.hex8 && options.hex_aarrggbb)
          {
            // #AARRGGBB: the QML/Android order, alpha first.
            const char *d = line.data() + i + 1;
            span.rgb = pack(hex_value(d[2]) * 16 + hex_value(d[3]),
                            hex_value(d[4]) * 16 + hex_value(d[5]),
                            hex_value(d[6]) * 16 + hex_value(d[7]));
          }
          else
          {
            const char *d = line.data() + i + 1;
            span.rgb = pack(hex_value(d[0]) * 16 + hex_value(d[1]),
                            hex_value(d[2]) * 16 + hex_value(d[3]),
                            hex_value(d[4]) * 16 + hex_value(d[5]));
          }
          spans.push_back(span);
          i = after;
          continue;
        }
        // "#x208": the xterm palette index shorthand, which is also '#'-led.
        if (options.xterm)
        {
          std::uint32_t xterm_rgb = 0;
          size_t xterm_end = 0;
          if (parse_xterm_code(line, i, limit, xterm_rgb, xterm_end))
          {
            spans.push_back(ColorSpan{(int)i, (int)(xterm_end - i), xterm_rgb});
            i = xterm_end;
            continue;
          }
        }
        i++;
        continue;
      }

      // --- Non-letter-led forms --------------------------------------------
      // These start with a byte the identifier walk below would skip, so they
      // are tried first: 0x..., =38;5;..., $var, \e[..., var(--x).
      const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      const bool word_start = at_word_start(line, i);

      // 0x-prefixed hex (Android/Java and friends): 0xRGB, 0xRRGGBB, 0xAARRGGBB.
      if (options.hex_0x && c == '0' && i + 1 < limit && (line[i + 1] == 'x' || line[i + 1] == 'X'))
      {
        size_t run = 0;
        while (i + 2 + run < limit && is_hex_digit(line[i + 2 + run]))
        {
          run++;
        }
        const size_t after = i + 2 + run;
        const bool clean_end = after >= limit || !is_word_char(line[after]);
        if ((run == 3 || run == 6 || run == 8) && clean_end)
        {
          const char *d = line.data() + i + 2;
          auto nib = [&](size_t k) { return hex_value(d[k]); };
          std::uint32_t rgb = 0;
          if (run == 3)
          {
            rgb = pack(nib(0) * 17, nib(1) * 17, nib(2) * 17);
          }
          else if (run == 6)
          {
            rgb = pack(nib(0) * 16 + nib(1), nib(2) * 16 + nib(3), nib(4) * 16 + nib(5));
          }
          else
          {
            // 0xAARRGGBB: alpha first, as in Android resources.
            rgb = pack(nib(2) * 16 + nib(3), nib(4) * 16 + nib(5), nib(6) * 16 + nib(7));
          }
          spans.push_back(ColorSpan{(int)i, (int)(after - i), rgb});
          i = after;
          continue;
        }
      }

      // LS_COLORS / SGR snippets ('=38;5;196').
      if (options.ls_colors && c == '=')
      {
        std::uint32_t rgb = 0;
        size_t end = 0;
        if (parse_ls_colors(line, i, limit, rgb, end))
        {
          spans.push_back(ColorSpan{(int)i, (int)(end - i), rgb});
          i = end;
          continue;
        }
      }

      // Escape sequences: a literal backslash form or a real ESC byte.
      if (options.xterm && (c == '\\' || c == '\x1b'))
      {
        std::uint32_t rgb = 0;
        size_t end = 0;
        if (parse_xterm_code(line, i, limit, rgb, end))
        {
          spans.push_back(ColorSpan{(int)i, (int)(end - i), rgb});
          i = end;
          continue;
        }
      }

      // Sass variable reference: $name.
      if (definitions != nullptr && options.sass && c == '$' && word_start)
      {
        size_t n = i + 1;
        while (n < limit && is_word_char(line[n]))
        {
          n++;
        }
        std::uint32_t rgb = 0;
        if (n > i + 1 && definitions->lookup_sass(line.substr(i + 1, n - i - 1), rgb))
        {
          spans.push_back(ColorSpan{(int)i, (int)(n - i), rgb});
          i = n;
          continue;
        }
      }

      // CSS custom-property reference: var(--name), tolerating a fallback.
      if (definitions != nullptr && options.css_var && alpha && word_start
          && line.compare(i, 4, "var(") == 0)
      {
        size_t n = i + 4;
        while (n < limit && std::isspace((unsigned char)line[n]))
        {
          n++;
        }
        if (n + 1 < limit && line[n] == '-' && line[n + 1] == '-')
        {
          n += 2;
          const size_t name_start = n;
          while (n < limit && is_word_char(line[n]))
          {
            n++;
          }
          std::uint32_t rgb = 0;
          if (n > name_start
              && definitions->lookup_css(line.substr(name_start, n - name_start), rgb))
          {
            // The span covers the whole call, closing paren included.
            size_t close = n;
            int depth = 1;
            while (close < limit && depth > 0)
            {
              if (line[close] == '(')
              {
                depth++;
              }
              else if (line[close] == ')')
              {
                depth--;
              }
              close++;
            }
            const size_t span_end = depth == 0 ? close : n;
            spans.push_back(ColorSpan{(int)i, (int)(span_end - i), rgb});
            i = span_end;
            continue;
          }
        }
      }

      if (!alpha)
      {
        i++;
        continue;
      }

      // CSS colour functions: rgb(), hsl(), hwb(), lab(), lch(), oklch(),
      // hsluv() and color(), each validating its own argument grammar.
      if (word_start && options.functions)
      {
        std::uint32_t rgb = 0;
        size_t end = 0;
        if (parse_css_function(line, i, limit, rgb, end))
        {
          spans.push_back(ColorSpan{(int)i, (int)(end - i), rgb});
          i = end;
          continue;
        }
      }

      // #xNN, which is '#'-led and therefore handled with the hex forms.
      if (word_start && options.xterm)
      {
        std::uint32_t rgb = 0;
        size_t end = 0;
        if (parse_xterm_code(line, i, limit, rgb, end))
        {
          spans.push_back(ColorSpan{(int)i, (int)(end - i), rgb});
          i = end;
          continue;
        }
      }

      // A whole identifier: only its full spelling can be a colour name, so the
      // run is looked up once and then skipped. That rejects "red" in
      // "text-red-500" without any extra boundary bookkeeping.
      size_t ident_end = i;
      while (ident_end < limit && is_word_char(line[ident_end]))
      {
        ident_end++;
      }
      if (word_start && ident_end > i)
      {
        const std::string word = line.substr(i, ident_end - i);
        std::uint32_t rgb = 0;
        // Bare hex (RRGGBB / RRGGBBAA) with no "#": only a whole word of the
        // right length, so an identifier that happens to look hex-ish is not
        // mistaken for a colour. A CSS name is never all hex digits, so
        // trying this first cannot shadow one.
        if (options.hex_no_hash && (word.size() == 6 || word.size() == 8)
            && std::all_of(word.begin(), word.end(), is_hex_digit))
        {
          const char *d = word.data();
          spans.push_back(ColorSpan{(int)i,
                                    (int)word.size(),
                                    pack(hex_value(d[0]) * 16 + hex_value(d[1]),
                                         hex_value(d[2]) * 16 + hex_value(d[3]),
                                         hex_value(d[4]) * 16 + hex_value(d[5]))});
          i = ident_end;
          continue;
        }
        // xcolor is checked first when it is on: "red!30" denotes 30% red, so
        // the whole expression is the colour the reader means, not the word
        // "red" inside it. (Upstream's fallback ordering would paint just
        // "red" here, which loses the expression's actual value.)
        if (options.xcolor && ident_end < limit && line[ident_end] == '!')
        {
          // xcolor: NAME!NN mixes toward white, so red!30 is 30% red.
          size_t n = ident_end + 1;
          const size_t digits_start = n;
          while (n < limit && isdigit((unsigned char)line[n]))
          {
            n++;
          }
          const int pct = n > digits_start
                              ? std::atoi(line.substr(digits_start, n - digits_start).c_str())
                              : -1;
          std::uint32_t base = 0;
          if (pct >= 0 && pct <= 100
              && lookup_named_color(line.data() + i,
                                    ident_end - i,
                                    options.names_camelcase,
                                    options.names_uppercase,
                                    base))
          {
            const double t = pct / 100.0;
            const auto mix = [&](int shift)
            {
              return (int)std::lround(((base >> shift) & 0xFF) * t + 255.0 * (1.0 - t));
            };
            spans.push_back(ColorSpan{(int)i,
                                      (int)(n - i),
                                      pack(std::clamp(mix(16), 0, 255),
                                           std::clamp(mix(8), 0, 255),
                                           std::clamp(mix(0), 0, 255))});
            i = n;
            continue;
          }
        }
        if (options.names
            && lookup_named_color(line.data() + i,
                                  ident_end - i,
                                  options.names_camelcase,
                                  options.names_uppercase,
                                  rgb))
        {
          spans.push_back(ColorSpan{(int)i, (int)(ident_end - i), rgb});
        }
        else if (options.tailwind && lookup_tailwind(word, rgb))
        {
          spans.push_back(ColorSpan{(int)i, (int)(ident_end - i), rgb});
        }
      }
      i = ident_end > i ? ident_end : i + 1;
    }

    return spans;
  }

  namespace
  {
    std::uint32_t options_mask(const Options &o)
    {
      // Every switch must set a bit: a cached line is only rescanned when this
      // mask changes, so an option left out here would never take effect on a
      // line that is already cached.
      const bool flags[] = {o.hex3,
                            o.hex4,
                            o.hex6,
                            o.hex8,
                            o.hex_aarrggbb,
                            o.hex_no_hash,
                            o.hex_0x,
                            o.names,
                            o.names_camelcase,
                            o.names_uppercase,
                            o.tailwind,
                            o.xcolor,
                            o.functions,
                            o.xterm,
                            o.ls_colors,
                            o.css_var,
                            o.sass};
      std::uint32_t mask = 0;
      std::uint32_t bit = 1;
      for (bool flag : flags)
      {
        if (flag)
        {
          mask |= bit;
        }
        bit <<= 1;
      }
      return mask;
    }

    // FNV-1a over the bytes we actually scan: cheap (a few hundred bytes per
    // visible line) and enough to notice any edit to the window.
    std::uint64_t hash_bytes(const char *data, size_t len, std::uint64_t seed)
    {
      std::uint64_t h = seed;
      for (size_t i = 0; i < len; i++)
      {
        h ^= (unsigned char)data[i];
        h *= 1099511628211ull;
      }
      return h;
    }
  } // namespace

  const std::vector<ColorSpan> &SpanCache::spans_for(int line_index,
                                                     const std::string &line,
                                                     int byte_limit,
                                                     const Options &options,
                                                     const std::vector<std::uint8_t> *scope,
                                                     const Definitions *definitions,
                                                     std::uint64_t definitions_version)
  {
    static const std::vector<ColorSpan> kEmpty;

    const size_t scanned =
        (byte_limit < 0) ? line.size() : std::min(line.size(), (size_t)byte_limit);
    const std::uint64_t hash = hash_bytes(line.data(), scanned, 1469598103934665603ull);
    const std::uint32_t mask = options_mask(options);
    std::uint64_t scope_hash = 0;
    if (scope)
    {
      scope_hash = hash_bytes(
          (const char *)scope->data(), std::min(scope->size(), scanned), 1469598103934665603ull);
    }

    if (line_index < 0)
    {
      return kEmpty;
    }
    if ((size_t)line_index >= line_to_slot_.size())
    {
      line_to_slot_.resize((size_t)line_index + 1, -1);
    }
    const int slot = line_to_slot_[(size_t)line_index];
    if (slot >= 0)
    {
      Entry &e = entries_[(size_t)slot];
      if (e.hash == hash && e.limit == byte_limit && e.options_mask == mask
          && e.scope_hash == scope_hash && e.definitions_version == definitions_version)
      {
        return e.spans;
      }
    }

    // Bounded: a linear rescan of the whole file drops every stale entry at once.
    // The visible window is a few dozen lines, so this is rare.
    if (entries_.size() >= kMaxEntries)
    {
      entries_.clear();
      std::fill(line_to_slot_.begin(), line_to_slot_.end(), -1);
    }

    Entry entry;
    entry.hash = hash;
    entry.limit = byte_limit;
    entry.options_mask = mask;
    entry.scope_hash = scope_hash;
    entry.definitions_version = definitions_version;
    entry.spans = scan_line(line, byte_limit, options, scope, definitions);
    entries_.push_back(std::move(entry));
    line_to_slot_[(size_t)line_index] = (int)entries_.size() - 1;
    return entries_.back().spans;
  }
} // namespace jot_color
