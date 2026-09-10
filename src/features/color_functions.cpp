#include "features/color_functions.h"

#include "features/color_space.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{
  using namespace jot_color;

  // How the arguments of a call were separated. CSS allows either the legacy
  // comma form (rgb(0, 0, 0) / rgba(0, 0, 0, .5)) or the modern space form with
  // "/" before alpha (rgb(0 0 0 / 50%)). Mixing them is invalid.
  enum class SepStyle
  {
    Unknown,
    Comma,
    Space,
  };

  struct Cursor
  {
    const std::string &s;
    size_t i;
    size_t limit;

    bool at_end() const
    {
      return i >= limit;
    }

    char peek() const
    {
      return i < limit ? s[i] : '\0';
    }

    void skip_spaces()
    {
      while (i < limit && (s[i] == ' ' || s[i] == '\t'))
      {
        i++;
      }
    }

    // Consumes the separator before a channel argument and updates `style`.
    // Rejects "/" here (that introduces alpha) and any mixing of styles. When
    // `allow_comma` is false -- the space-only functions -- a comma is rejected
    // outright, so "hwb(0, 0%, 0%)" does not read as valid CSS.
    bool take_channel_separator(SepStyle &style, bool allow_comma = true)
    {
      skip_spaces();
      bool saw_comma = false;
      while (i < limit && s[i] == ',')
      {
        saw_comma = true;
        i++;
        skip_spaces();
      }
      if (saw_comma && !allow_comma)
      {
        return false;
      }
      if (saw_comma)
      {
        if (style == SepStyle::Space)
        {
          return false;
        }
        style = SepStyle::Comma;
        return true;
      }
      if (i < limit && s[i] == '/')
      {
        return false; // alpha must not appear mid-channel
      }
      if (style == SepStyle::Comma)
      {
        return false; // comma form must keep its commas
      }
      style = SepStyle::Space;
      return true;
    }

    // Consumes the optional separator before alpha. In the comma form that is a
    // comma; in the space form it must be "/" (and "/" is also accepted after
    // commas, which the spec allows).
    bool take_alpha_separator(SepStyle style)
    {
      skip_spaces();
      bool saw_slash = false;
      bool saw_comma = false;
      while (i < limit && (s[i] == '/' || s[i] == ','))
      {
        if (s[i] == '/')
        {
          saw_slash = true;
        }
        else
        {
          saw_comma = true;
        }
        i++;
        skip_spaces();
      }
      if (saw_slash)
      {
        return true;
      }
      if (saw_comma)
      {
        // The legacy fourth argument is only legal in the comma form.
        return style != SepStyle::Space;
      }
      return true; // no separator: no alpha follows
    }

    // A CSS number: optional sign, digits with an optional fractional part, or a
    // leading ".". Rejects a bare "." or "-".
    bool number(double &out)
    {
      skip_spaces();
      const size_t start = i;
      if (i < limit && (s[i] == '+' || s[i] == '-'))
      {
        i++;
      }
      size_t digits = 0;
      while (i < limit && s[i] >= '0' && s[i] <= '9')
      {
        i++;
        digits++;
      }
      if (i < limit && s[i] == '.')
      {
        i++;
        while (i < limit && s[i] >= '0' && s[i] <= '9')
        {
          i++;
          digits++;
        }
      }
      if (digits == 0)
      {
        i = start;
        return false;
      }
      out = std::strtod(s.substr(start, i - start).c_str(), nullptr);
      return true;
    }

    // A number followed by an optional "%". `had_percent` reports which it was.
    // `allow_negative` is false for channels the CSS grammar defines as
    // non-negative (rgb()'s), so "rgb(-1, 0, 0)" is rejected rather than clamped.
    bool number_or_percent(double &out, bool &had_percent, bool allow_negative = true)
    {
      const size_t sign_at = i;
      if (!number(out))
      {
        return false;
      }
      if (!allow_negative && sign_at < i && s[sign_at] == '-')
      {
        i = sign_at;
        return false;
      }
      had_percent = false;
      if (i < limit && s[i] == '%')
      {
        i++;
        had_percent = true;
      }
      return true;
    }

    bool take(char c)
    {
      skip_spaces();
      if (i < limit && s[i] == c)
      {
        i++;
        return true;
      }
      return false;
    }
  };

  // Hue in degrees, accepting the spec's angle units.
  bool read_hue(Cursor &cur, double &hue_deg)
  {
    double v = 0.0;
    if (!cur.number(v))
    {
      return false;
    }
    // Longest unit first so "grad" is not read as "rad" with a stray "g".
    struct Unit
    {
      const char *name;
      double degrees_per_unit;
    };
    static const Unit kUnits[] = {
        {"grad", 0.9}, {"deg", 1.0}, {"rad", 180.0 / M_PI}, {"turn", 360.0}};
    for (const auto &unit : kUnits)
    {
      const size_t len = std::char_traits<char>::length(unit.name);
      if (cur.i + len <= cur.limit && cur.s.compare(cur.i, len, unit.name) == 0)
      {
        cur.i += len;
        hue_deg = v * unit.degrees_per_unit;
        return true;
      }
    }
    // No unit at all is degrees.
    hue_deg = v;
    return true;
  }

  double wrap_degrees(double d)
  {
    d = std::fmod(d, 360.0);
    if (d < 0)
    {
      d += 360.0;
    }
    return d;
  }

  // The alpha channel, which the preview accepts and then ignores: it only has to
  // be syntactically valid and the call must close.
  // Alpha is accepted and then discarded: the preview shows the opaque colour.
  bool skip_alpha(Cursor &cur, SepStyle style)
  {
    if (!cur.take_alpha_separator(style))
    {
      return false;
    }
    if (cur.i < cur.limit
        && (cur.s[cur.i] == '-' || cur.s[cur.i] == '+' || cur.s[cur.i] == '.'
            || (cur.s[cur.i] >= '0' && cur.s[cur.i] <= '9')))
    {
      double discarded = 0.0;
      bool percent = false;
      if (!cur.number_or_percent(discarded, percent))
      {
        return false;
      }
    }
    cur.skip_spaces();
    return cur.i < cur.limit && cur.s[cur.i] == ')';
  }

  // rgb()/rgba(): three channels, decimal (0-255) or percentage.
  bool parse_rgb(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    int channels[3] = {0, 0, 0};
    for (int c = 0; c < 3; c++)
    {
      if (c > 0 && !cur.take_channel_separator(style))
      {
        return false;
      }
      double v = 0.0;
      bool percent = false;
      if (!cur.number_or_percent(v, percent, /*allow_negative=*/false))
      {
        return false;
      }
      channels[c] = percent ? (int)std::lround(v * 255.0 / 100.0) : (int)std::lround(v);
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    rgb = pack_rgb(std::clamp(channels[0], 0, 255),
                   std::clamp(channels[1], 0, 255),
                   std::clamp(channels[2], 0, 255));
    return true;
  }

  // hsl()/hsla(): hue with optional unit, then saturation and lightness, which the
  // spec requires as percentages.
  bool parse_hsl(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double hue = 0.0;
    if (!read_hue(cur, hue))
    {
      return false;
    }
    if (!cur.take_channel_separator(style))
    {
      return false;
    }
    double sat = 0.0;
    bool sat_percent = false;
    if (!cur.number_or_percent(sat, sat_percent) || !sat_percent)
    {
      return false;
    }
    if (!cur.take_channel_separator(style))
    {
      return false;
    }
    double light = 0.0;
    bool light_percent = false;
    if (!cur.number_or_percent(light, light_percent) || !light_percent)
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }

    rgb = hsl_to_rgb(
        wrap_degrees(hue), std::clamp(sat / 100.0, 0.0, 1.0), std::clamp(light / 100.0, 0.0, 1.0));
    return true;
  }

  // hwb(): hue, whiteness and blackness. The spec lets both be percentages and
  // caps them at 0 on the low side; the high side overflows into "grey".
  bool parse_hwb(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double hue = 0.0;
    if (!read_hue(cur, hue))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double w = 0.0;
    bool w_percent = false;
    if (!cur.number_or_percent(w, w_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double b = 0.0;
    bool b_percent = false;
    if (!cur.number_or_percent(b, b_percent))
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    rgb = hwb_to_rgb(wrap_degrees(hue), std::max(0.0, w / 100.0), std::max(0.0, b / 100.0));
    return true;
  }

  // lab(): L 0-100 (or a percentage of the same), a/b unbounded but a percentage
  // maps onto the spec's +/-125 reference range.
  bool parse_lab(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double l = 0.0;
    bool l_percent = false;
    if (!cur.number_or_percent(l, l_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double a = 0.0;
    bool a_percent = false;
    if (!cur.number_or_percent(a, a_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double b = 0.0;
    bool b_percent = false;
    if (!cur.number_or_percent(b, b_percent))
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    if (a_percent)
    {
      a *= 1.25;
    }
    if (b_percent)
    {
      b *= 1.25;
    }
    rgb = lab_to_rgb(std::clamp(l, 0.0, 100.0), a, b);
    return true;
  }

  // lch(): like lab() but polar; chroma percentages map onto 0-150.
  bool parse_lch(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double l = 0.0;
    bool l_percent = false;
    if (!cur.number_or_percent(l, l_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double c = 0.0;
    bool c_percent = false;
    if (!cur.number_or_percent(c, c_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double hue = 0.0;
    if (!read_hue(cur, hue))
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    if (c_percent)
    {
      c *= 1.5;
    }
    rgb = lch_to_rgb(std::clamp(l, 0.0, 100.0), std::max(0.0, c), wrap_degrees(hue));
    return true;
  }

  // oklch(): L is 0-1 (or a percentage of it), chroma percentages map onto 0.4.
  bool parse_oklch(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double l = 0.0;
    bool l_percent = false;
    if (!cur.number_or_percent(l, l_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double c = 0.0;
    bool c_percent = false;
    if (!cur.number_or_percent(c, c_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style, false))
    {
      return false;
    }
    double hue = 0.0;
    if (!read_hue(cur, hue))
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    if (l_percent)
    {
      l /= 100.0;
    }
    if (c_percent)
    {
      c = c * 0.4 / 100.0;
    }
    rgb = oklch_to_rgb(std::clamp(l, 0.0, 1.0), std::max(0.0, c), wrap_degrees(hue));
    return true;
  }

  // hsluv() / hsluvu(): hue, saturation and lightness all in 0-100 terms.
  bool parse_hsluv(Cursor &cur, std::uint32_t &rgb)
  {
    SepStyle style = SepStyle::Unknown;
    double hue = 0.0;
    bool hue_percent = false;
    if (!cur.number_or_percent(hue, hue_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style))
    {
      return false;
    }
    double s = 0.0;
    bool s_percent = false;
    if (!cur.number_or_percent(s, s_percent))
    {
      return false;
    }
    if (!cur.take_channel_separator(style))
    {
      return false;
    }
    double l = 0.0;
    bool l_percent = false;
    if (!cur.number_or_percent(l, l_percent))
    {
      return false;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    rgb = hsluv_to_rgb(wrap_degrees(hue), std::clamp(s, 0.0, 100.0), std::clamp(l, 0.0, 100.0));
    return true;
  }

  // color(): a colour space name followed by three channels (0-1 or percentages).
  bool parse_color_fn(Cursor &cur, std::uint32_t &rgb)
  {
    cur.skip_spaces();
    const size_t name_start = cur.i;
    while (cur.i < cur.limit && (std::isalnum((unsigned char)cur.s[cur.i]) || cur.s[cur.i] == '-'))
    {
      cur.i++;
    }
    if (cur.i == name_start)
    {
      return false;
    }
    const std::string space = cur.s.substr(name_start, cur.i - name_start);
    ColorFnSpace fn_space;
    if (space == "srgb")
    {
      fn_space = ColorFnSpace::Srgb;
    }
    else if (space == "srgb-linear")
    {
      fn_space = ColorFnSpace::SrgbLinear;
    }
    else if (space == "display-p3")
    {
      fn_space = ColorFnSpace::DisplayP3;
    }
    else if (space == "a98-rgb")
    {
      fn_space = ColorFnSpace::A98Rgb;
    }
    else if (space == "prophoto-rgb")
    {
      fn_space = ColorFnSpace::ProPhotoRgb;
    }
    else if (space == "rec2020")
    {
      fn_space = ColorFnSpace::Rec2020;
    }
    else
    {
      return false;
    }

    SepStyle style = SepStyle::Unknown;
    double channels[3] = {0.0, 0.0, 0.0};
    for (int c = 0; c < 3; c++)
    {
      if (!cur.take_channel_separator(style))
      {
        return false;
      }
      double v = 0.0;
      bool percent = false;
      if (!cur.number_or_percent(v, percent))
      {
        return false;
      }
      channels[c] = percent ? v / 100.0 : v;
    }
    if (!skip_alpha(cur, style))
    {
      return false;
    }
    rgb = css_color_to_rgb(fn_space, channels[0], channels[1], channels[2]);
    return true;
  }
} // namespace

namespace jot_color
{
  bool parse_css_function(
      const std::string &line, size_t name_start, size_t limit, std::uint32_t &rgb, size_t &end)
  {
    struct Fn
    {
      const char *name;
      bool (*parse)(Cursor &, std::uint32_t &);
    };
    // Longest name first where one is a prefix of another (hsluv before hsl,
    // oklch before lch); the "(" check below rejects a false prefix anyway.
    static const Fn kFunctions[] = {
        {"color", parse_color_fn},
        {"hsluvu", parse_hsluv},
        {"hsluv", parse_hsluv},
        {"oklch", parse_oklch},
        {"rgba", parse_rgb},
        {"hsla", parse_hsl},
        {"hwb", parse_hwb},
        {"lab", parse_lab},
        {"lch", parse_lch},
        {"rgb", parse_rgb},
        {"hsl", parse_hsl},
    };

    for (const auto &fn : kFunctions)
    {
      const size_t name_len = std::char_traits<char>::length(fn.name);
      if (name_start + name_len >= limit)
      {
        continue;
      }
      if (line.compare(name_start, name_len, fn.name) != 0)
      {
        continue;
      }
      // The name must be followed directly by the call's opening paren; without
      // this, "hsl" would match inside "hsluv(" and "color" inside "colors(".
      if (line[name_start + name_len] != '(')
      {
        continue;
      }
      Cursor cur{line, name_start + name_len + 1, limit};
      std::uint32_t parsed = 0;
      if (fn.parse(cur, parsed))
      {
        rgb = parsed;
        end = cur.i + 1; // just past the closing paren
        return true;
      }
    }
    return false;
  }
} // namespace jot_color
