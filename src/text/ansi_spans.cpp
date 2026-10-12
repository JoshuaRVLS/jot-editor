#include "text/ansi_spans.h"

#include <algorithm>

namespace
{
  bool is_digit(char c)
  {
    return c >= '0' && c <= '9';
  }

  int clamp_channel(int value)
  {
    return std::clamp(value, 0, 255);
  }

  // The parameter list of one SGR, split on both ';' and ':' because programs
  // write both spellings (`38;5;196` and `38:5:196`). An empty field reads as
  // -1 -- the colon form of an exact colour carries one (`38:2::r:g:b`, the gap
  // where a colour space would go) and it has to be told apart from a 0.
  std::vector<int> split_params(const std::string &text, size_t begin, size_t end)
  {
    std::vector<int> params;
    if (begin >= end)
    {
      // `ESC[m` is `ESC[0m`: a reset.
      params.push_back(0);
      return params;
    }
    size_t i = begin;
    while (i < end)
    {
      int value = -1;
      while (i < end && is_digit(text[i]))
      {
        value = (value < 0 ? 0 : value) * 10 + (text[i] - '0');
        i++;
      }
      params.push_back(value);
      if (i < end)
      {
        i++; // the separator
      }
    }
    return params;
  }

  // The colours a row is drawn in right now, with the bold promotion applied:
  // every terminal (and the palette preview in syntax/color) brightens one of
  // the eight base colours when bold is on, which is how a build tool says
  // "bright red" with SGR 1;31.
  ansi_spans::Color bold_promoted(const ansi_spans::Color &colour, bool bold)
  {
    if (!bold || !colour.set || colour.index < 0 || colour.index > 7)
    {
      return colour;
    }
    ansi_spans::Color bright = colour;
    bright.index = colour.index + 8;
    return bright;
  }
} // namespace

namespace ansi_spans
{
  void Parser::reset()
  {
    fg_ = Color();
    bg_ = Color();
    bold_ = false;
    dim_ = false;
    underline_ = false;
  }

  Row Parser::feed(const std::string &raw)
  {
    Row row;
    row.text.reserve(raw.size());

    // The span being collected: its style and where it started. A style change
    // closes it; so does the end of the row (or of the output's colour state).
    Span open;
    int open_col = -1;
    auto close_span = [&]()
    {
      if (open_col < 0)
      {
        return;
      }
      open.col = open_col;
      open.width = (int)row.text.size() - open_col;
      row.spans.push_back(open);
      open_col = -1;
    };
    auto style_of_open = [&]()
    {
      Span style;
      style.fg = bold_promoted(fg_, bold_);
      style.bg = bg_;
      style.dim = dim_;
      style.underline = underline_;
      return style;
    };

    size_t i = 0;
    while (i < raw.size())
    {
      const unsigned char c = (unsigned char)raw[i];
      if (c == 0x1b)
      {
        close_span();
        i += consume_escape(raw, i);
        continue;
      }
      if (c < 0x20 && c != '\t')
      {
        // Whatever else a program writes raw -- a bell, a backspace, a lone
        // carriage return the caller did not split on -- is not text.
        i++;
        continue;
      }

      const Span style = style_of_open();
      const bool plain = !style.fg.set && !style.bg.set && !style.dim && !style.underline;
      if (plain)
      {
        close_span();
      }
      else if (open_col < 0)
      {
        open = style;
        open_col = (int)row.text.size();
      }
      else if (open.fg != style.fg || open.bg != style.bg || open.dim != style.dim
               || open.underline != style.underline)
      {
        close_span();
        open = style;
        open_col = (int)row.text.size();
      }
      row.text.push_back(raw[i]);
      i++;
    }
    close_span();
    return row;
  }

  // How many bytes the sequence starting at `i` (an ESC) occupies, applying it
  // when it is an SGR. State is only ever changed here.
  size_t Parser::consume_escape(const std::string &text, size_t i)
  {
    const size_t available = text.size() - i;
    if (available < 2)
    {
      return 1;
    }
    const char kind = text[i + 1];
    if (kind == ']')
    {
      // OSC: a window title or similar, ended by BEL or by ST (`ESC \`).
      for (size_t p = i + 2; p < text.size(); p++)
      {
        if (text[p] == '\a')
        {
          return p + 1 - i;
        }
        if (text[p] == 0x1b && p + 1 < text.size() && text[p + 1] == '\\')
        {
          return p + 2 - i;
        }
      }
      return available;
    }
    if (kind != '[')
    {
      // A two-byte escape (`ESC =`, `ESC c`), or a three-byte one whose second
      // byte is an intermediate: the charset selections are `ESC ( 0` and its
      // relatives, and the byte after those is text again.
      const unsigned char intermediate = (unsigned char)kind;
      if (intermediate >= 0x20 && intermediate <= 0x2f)
      {
        return std::min<size_t>(3, available);
      }
      return 2;
    }

    // CSI: parameter bytes, then intermediate bytes, then one final byte.
    size_t p = i + 2;
    const size_t params_end = [&]()
    {
      size_t q = p;
      while (q < text.size() && (unsigned char)text[q] >= 0x30 && (unsigned char)text[q] <= 0x3f)
      {
        q++;
      }
      return q;
    }();
    size_t r = params_end;
    while (r < text.size() && (unsigned char)text[r] >= 0x20 && (unsigned char)text[r] <= 0x2f)
    {
      r++;
    }
    if (r >= text.size())
    {
      // Truncated mid-sequence: there is no final byte to read.
      return available;
    }
    if (text[r] == 'm')
    {
      apply_sgr(text, i + 2, params_end);
    }
    return r + 1 - i;
  }

  void Parser::apply_sgr(const std::string &text, size_t begin, size_t end)
  {
    const std::vector<int> params = split_params(text, begin, end);
    for (size_t k = 0; k < params.size(); k++)
    {
      const int value = params[k] < 0 ? 0 : params[k];
      if (value == 0)
      {
        reset();
      }
      else if (value == 1)
      {
        bold_ = true;
      }
      else if (value == 2)
      {
        dim_ = true;
      }
      else if (value == 22)
      {
        bold_ = false;
        dim_ = false;
      }
      else if (value == 4 || value == 21)
      {
        underline_ = true;
      }
      else if (value == 24)
      {
        underline_ = false;
      }
      else if (value >= 30 && value <= 37)
      {
        fg_.set = true;
        fg_.index = value - 30;
      }
      else if (value == 39)
      {
        fg_ = Color();
      }
      else if (value >= 40 && value <= 47)
      {
        bg_.set = true;
        bg_.index = value - 40;
      }
      else if (value == 49)
      {
        bg_ = Color();
      }
      else if (value >= 90 && value <= 97)
      {
        fg_.set = true;
        fg_.index = value - 90 + 8;
      }
      else if (value >= 100 && value <= 107)
      {
        bg_.set = true;
        bg_.index = value - 100 + 8;
      }
      else if (value == 38 || value == 48)
      {
        Color colour;
        size_t p = k + 1;
        if (p >= params.size())
        {
          break;
        }
        const int mode = params[p] < 0 ? 0 : params[p];
        p++;
        bool read = false;
        if (mode == 5 && p < params.size() && params[p] >= 0 && params[p] <= 255)
        {
          colour.set = true;
          colour.index = params[p];
          p++;
          read = true;
        }
        else if (mode == 2)
        {
          // The colon spelling carries the colour space as an empty field.
          if (p < params.size() && params[p] < 0 && p + 3 < params.size())
          {
            p++;
          }
          if (p + 2 < params.size())
          {
            colour.set = true;
            colour.index = -1;
            colour.r = clamp_channel(params[p]);
            colour.g = clamp_channel(params[p + 1]);
            colour.b = clamp_channel(params[p + 2]);
            p += 3;
            read = true;
          }
        }
        if (read)
        {
          if (value == 38)
          {
            fg_ = colour;
          }
          else
          {
            bg_ = colour;
          }
        }
        k = p - 1;
      }
    }
  }
} // namespace ansi_spans
