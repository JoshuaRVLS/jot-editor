// UTF-8 decoding for cell text. Shared by the glyph painter (which needs a
// codepoint per cell to look up the atlas) and the input translator (which
// walks multi-codepoint IME/text-input strings one codepoint per event).
#include "gui/gui.h"

uint32_t decode_utf8(const char *s, size_t len, size_t &i)
{
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80)
  {
    i++;
    return c;
  }
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0)
  {
    extra = 1;
    cp = c & 0x1F;
  }
  else if ((c & 0xF0) == 0xE0)
  {
    extra = 2;
    cp = c & 0x0F;
  }
  else if ((c & 0xF8) == 0xF0)
  {
    extra = 3;
    cp = c & 0x07;
  }
  else
  {
    i++;
    return 0;
  }
  if (i + (size_t)extra >= len)
  {
    i = len;
    return 0;
  }
  for (int k = 1; k <= extra; k++)
  {
    unsigned char cc = static_cast<unsigned char>(s[i + (size_t)k]);
    if ((cc & 0xC0) != 0x80)
    {
      i += (size_t)k;
      return 0;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += (size_t)extra + 1;
  return cp;
}