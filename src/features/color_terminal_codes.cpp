#include "features/color_terminal_codes.h"

#include "ui/xterm_palette.h"

#include <algorithm>
#include <cstdlib>

namespace
{
  bool is_digit(char c)
  {
    return c >= '0' && c <= '9';
  }

  // Parses up to `max_digits` decimal digits at `i`, reporting how many were used.
  bool
  read_uint(const std::string &line, size_t i, size_t limit, int max_digits, int &out, size_t &used)
  {
    int value = 0;
    size_t n = 0;
    while (n < (size_t)max_digits && i + n < limit && is_digit(line[i + n]))
    {
      value = value * 10 + (line[i + n] - '0');
      n++;
    }
    if (n == 0 || (max_digits == 3 && value > 255))
    {
      return false;
    }
    out = value;
    used = n;
    return true;
  }

  // The spellings of ESC that turn up in source text: a real escape byte, and the
  // three ways people write one inside a string literal. Returns the number of
  // bytes consumed, or 0 when there is no escape here.
  size_t escape_length(const std::string &line, size_t i, size_t limit)
  {
    if (i >= limit)
    {
      return 0;
    }
    if (line[i] == '\x1b')
    {
      return 1;
    }
    if (line[i] != '\\')
    {
      return 0;
    }
    if (i + 1 < limit && line[i + 1] == 'e')
    {
      return 2;
    }
    if (i + 3 < limit && line.compare(i + 1, 3, "033") == 0)
    {
      return 4;
    }
    if (i + 3 < limit && (line[i + 1] == 'x' || line[i + 1] == 'X') && line[i + 2] == '1'
        && (line[i + 3] == 'b' || line[i + 3] == 'B'))
    {
      return 4;
    }
    return 0;
  }
} // namespace

namespace jot_color
{
  std::uint32_t xterm256_rgb(int index)
  {
    unsigned char r = 0, g = 0, b = 0;
    jot_ui::palette_rgb(index, r, g, b);
    return ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
  }

  bool
  parse_xterm_code(const std::string &line, size_t i, size_t limit, std::uint32_t &rgb, size_t &end)
  {
    // --- #xNN -----------------------------------------------------------------
    // An xterm palette index written as a CSS-ish colour literal.
    if (i + 2 < limit && line[i] == '#' && (line[i + 1] == 'x' || line[i + 1] == 'X'))
    {
      int value = 0;
      size_t digits = 0;
      if (read_uint(line, i + 2, limit, 3, value, digits) && value <= 255)
      {
        // The index must be a whole token, not the start of a longer one.
        const size_t after = i + 2 + digits;
        const bool clean =
            after >= limit
            || !(is_digit(line[after]) || (line[after] >= 'A' && line[after] <= 'Z')
                 || (line[after] >= 'a' && line[after] <= 'z') || line[after] == '_');
        if (clean)
        {
          rgb = xterm256_rgb(value);
          end = after;
          return true;
        }
      }
      return false;
    }

    // --- Escape sequences -----------------------------------------------------
    const size_t esc = escape_length(line, i, limit);
    if (esc == 0)
    {
      return false;
    }
    size_t p = i + esc;
    if (p >= limit || line[p] != '[')
    {
      return false;
    }
    p++;

    // 38;5;N / 48;5;N (256-colour) and 38;2;R;G;B / 48;2;R;G;B (truecolour).
    int lead = 0;
    size_t lead_digits = 0;
    if (!read_uint(line, p, limit, 3, lead, lead_digits))
    {
      return false;
    }
    if (lead != 38 && lead != 48)
    {
      return false;
    }
    size_t q = p + lead_digits;
    if (q >= limit || line[q] != ';')
    {
      return false;
    }
    q++;
    int mode = 0;
    size_t mode_digits = 0;
    if (!read_uint(line, q, limit, 1, mode, mode_digits))
    {
      return false;
    }
    q += mode_digits;
    if (q >= limit || line[q] != ';')
    {
      return false;
    }
    q++;

    if (mode == 5)
    {
      int index = 0;
      size_t index_digits = 0;
      if (!read_uint(line, q, limit, 3, index, index_digits) || index > 255)
      {
        return false;
      }
      q += index_digits;
      if (q < limit && line[q] == 'm')
      {
        rgb = xterm256_rgb(index);
        end = q + 1;
        return true;
      }
      return false;
    }
    if (mode == 2)
    {
      int channels[3] = {0, 0, 0};
      for (int c = 0; c < 3; c++)
      {
        size_t digits = 0;
        if (!read_uint(line, q, limit, 3, channels[c], digits))
        {
          return false;
        }
        q += digits;
        if (c < 2)
        {
          if (q >= limit || line[q] != ';')
          {
            return false;
          }
          q++;
        }
      }
      if (q < limit && line[q] == 'm')
      {
        rgb = ((std::uint32_t)channels[0] << 16) | ((std::uint32_t)channels[1] << 8)
              | (std::uint32_t)channels[2];
        end = q + 1;
        return true;
      }
      return false;
    }
    return false;
  }

  bool
  parse_ls_colors(const std::string &line, size_t i, size_t limit, std::uint32_t &rgb, size_t &end)
  {
    if (i >= limit || line[i] != '=')
    {
      return false;
    }
    // Collect the [digit;]+ run after the '='.
    size_t p = i + 1;
    const size_t run_start = p;
    while (p < limit && (is_digit(line[p]) || line[p] == ';'))
    {
      p++;
    }
    if (p == run_start)
    {
      return false;
    }

    // Walk the codes, tracking the first foreground/background and any bold flag.
    // Foreground wins when both are present, matching the upstream parser.
    bool have_fg = false;
    bool have_bg = false;
    std::uint32_t fg = 0;
    std::uint32_t bg = 0;
    bool bright = false;

    size_t k = run_start;
    while (k < p)
    {
      // One numeric token.
      int value = 0;
      size_t digits = 0;
      if (!read_uint(line, k, p, 4, value, digits))
      {
        break;
      }
      k += digits;
      if (k < p && line[k] == ';')
      {
        k++;
      }

      if (value == 1)
      {
        bright = true;
      }
      else if (value >= 30 && value <= 37 && !have_fg)
      {
        fg = xterm256_rgb(value - 30);
        have_fg = true;
      }
      else if (value >= 40 && value <= 47 && !have_bg)
      {
        bg = xterm256_rgb(value - 40);
        have_bg = true;
      }
      else if (value >= 90 && value <= 97 && !have_fg)
      {
        fg = xterm256_rgb(value - 90 + 8);
        have_fg = true;
      }
      else if (value >= 100 && value <= 107 && !have_bg)
      {
        bg = xterm256_rgb(value - 100 + 8);
        have_bg = true;
      }
      else if (value == 38 || value == 48)
      {
        const bool is_bg = (value == 48);
        int sub = 0;
        size_t sub_digits = 0;
        if (k < p && read_uint(line, k, p, 1, sub, sub_digits))
        {
          k += sub_digits;
          if (k < p && line[k] == ';')
          {
            k++;
          }
          if (sub == 5)
          {
            int index = 0;
            size_t index_digits = 0;
            if (read_uint(line, k, p, 3, index, index_digits) && index <= 255)
            {
              k += index_digits;
              if (is_bg && !have_bg)
              {
                bg = xterm256_rgb(index);
                have_bg = true;
              }
              else if (!is_bg && !have_fg)
              {
                fg = xterm256_rgb(index);
                have_fg = true;
              }
            }
          }
          else if (sub == 2)
          {
            int channels[3] = {0, 0, 0};
            bool ok = true;
            for (int c = 0; c < 3 && ok; c++)
            {
              size_t digits3 = 0;
              ok = read_uint(line, k, p, 3, channels[c], digits3);
              if (ok)
              {
                k += digits3;
                if (c < 2)
                {
                  if (k < p && line[k] == ';')
                  {
                    k++;
                  }
                  else
                  {
                    ok = false;
                  }
                }
              }
            }
            if (ok)
            {
              const std::uint32_t value_rgb = ((std::uint32_t)channels[0] << 16)
                                              | ((std::uint32_t)channels[1] << 8)
                                              | (std::uint32_t)channels[2];
              if (is_bg && !have_bg)
              {
                bg = value_rgb;
                have_bg = true;
              }
              else if (!is_bg && !have_fg)
              {
                fg = value_rgb;
                have_fg = true;
              }
            }
          }
        }
      }
    }

    if (!have_fg && !have_bg)
    {
      return false;
    }
    // Bold promotes one of the eight base colours to its bright variant, which is
    // how dircolors expresses "bold red" without a 256-colour index.
    if (have_fg && bright)
    {
      for (int base = 0; base < 8; base++)
      {
        if (fg == xterm256_rgb(base))
        {
          fg = xterm256_rgb(base + 8);
          break;
        }
      }
    }
    rgb = have_fg ? fg : bg;
    end = p;
    return true;
  }
} // namespace jot_color
