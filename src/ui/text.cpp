#include "ui/text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utf8proc.h>

namespace
{
  bool decode_at(const std::string &text, int i, utf8proc_int32_t &codepoint, int &len)
  {
    codepoint = -1;
    len = 0;
    if (i < 0 || i >= (int)text.size())
      return false;

    const auto *bytes = reinterpret_cast<const utf8proc_uint8_t *>(text.data() + i);
    utf8proc_ssize_t remaining = (utf8proc_ssize_t)text.size() - i;
    utf8proc_ssize_t result = utf8proc_iterate(bytes, remaining, &codepoint);
    if (result <= 0)
    {
      codepoint = -1;
      len = 1;
      return false;
    }

    len = (int)result;
    return true;
  }

  int codepoint_width(utf8proc_int32_t codepoint)
  {
    if (codepoint < 0)
      return 1;
    int width = utf8proc_charwidth(codepoint);
    return std::max(0, width);
  }

  std::string normalize_with_options(const std::string &text, utf8proc_option_t options)
  {
    if (text.empty())
      return "";

    utf8proc_uint8_t *out = nullptr;
    utf8proc_ssize_t result = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(text.data()),
                                           (utf8proc_ssize_t)text.size(),
                                           &out,
                                           (utf8proc_option_t)(UTF8PROC_STABLE | options));
    if (result < 0 || out == nullptr)
    {
      return text;
    }

    std::string normalized(reinterpret_cast<char *>(out), (size_t)result);
    std::free(out);
    return normalized;
  }

  bool is_combining_mark(utf8proc_int32_t codepoint)
  {
    const utf8proc_property_t *prop = utf8proc_get_property(codepoint);
    if (!prop)
      return false;
    return prop->category == UTF8PROC_CATEGORY_MN || prop->category == UTF8PROC_CATEGORY_MC
           || prop->category == UTF8PROC_CATEGORY_ME;
  }
} // namespace

int ui_utf8_char_len(const std::string &text, int i)
{
  utf8proc_int32_t codepoint = -1;
  int len = 0;
  return decode_at(text, i, codepoint, len) ? len : 0;
}

bool ui_is_valid_utf8_sequence(const std::string &text)
{
  if (text.empty())
    return false;

  int i = 0;
  while (i < (int)text.size())
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    if (!decode_at(text, i, codepoint, len))
      return false;
    i += len;
  }
  return true;
}

std::string ui_sanitized_cell_text(const std::string &text)
{
  if (text.empty())
    return " ";
  if (!ui_is_valid_utf8_sequence(text))
    return "?";
  for (int i = 0; i < (int)text.size();)
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    if (!decode_at(text, i, codepoint, len) || codepoint < 0x20 || codepoint == 0x7f)
    {
      return "?";
    }
    i += len;
  }
  return text;
}

int ui_range_cell_count(const std::string &text, int begin, int end)
{
  const int n = (int)text.size();
  int i = std::clamp(begin, 0, n);
  const int stop = std::clamp(end, i, n);
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());

  // Fast path: printable ASCII (0x20..0x7e) is exactly one cell per byte and
  // needs no decoding. In an editor practically every character is ASCII, and
  // a byte scan is far cheaper than a utf8proc decode per character -- this is
  // the hottest text predicate in the renderer (it runs once per grapheme per
  // visible row per frame). Control bytes and anything >= 0x7f still go
  // through the decoder below so the result stays bit-identical to the
  // previous all-decoder implementation.
  int cells = 0;
  while (i < stop)
  {
    const unsigned char c = bytes[i];
    if (c < 0x20 || c >= 0x7f)
    {
      break;
    }
    cells += 1;
    i++;
  }

  while (i < stop)
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    // A sequence that overruns the requested range is counted byte by byte,
    // which is what decoding the equivalent truncated substring would do.
    if (!decode_at(text, i, codepoint, len) || i + len > stop)
    {
      cells += 1;
      i += 1;
      continue;
    }
    cells += codepoint_width(codepoint);
    i += len;
  }
  return cells;
}

int ui_cell_count(const std::string &text)
{
  return ui_range_cell_count(text, 0, (int)text.size());
}

std::string ui_take_cells(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";

  std::string out;
  int cells = 0;
  int i = 0;
  while (i < (int)text.size() && cells < max_cells)
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    if (!decode_at(text, i, codepoint, len))
    {
      out += "?";
      i++;
      cells++;
    }
    else
    {
      int width = codepoint_width(codepoint);
      if (width > 0 && cells + width > max_cells)
        break;
      out.append(text, (size_t)i, (size_t)len);
      i += len;
      cells += width;
    }
  }
  return out;
}

std::string ui_truncate_cells(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";
  if (ui_cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return ui_take_cells(text, max_cells);
  return ui_take_cells(text, max_cells - 2) + "..";
}

std::string ui_truncate_cells_ellipsis(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";
  if (ui_cell_count(text) <= max_cells)
    return text;
  // The ellipsis is one cell wide, so the kept prefix gets the rest of the
  // budget. ui_take_cells drops any wide character that would straddle the
  // cut, so the result can be shorter than the budget but never wider.
  if (max_cells == 1)
    return "\u2026";
  return ui_take_cells(text, max_cells - 1) + "\u2026";
}

std::string ui_truncate_left_cells(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";
  if (ui_cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return ui_take_cells(text, max_cells);

  int keep = max_cells - 2;
  int total = ui_cell_count(text);
  int skip = total - keep;
  int cells = 0;
  int i = 0;
  while (i < (int)text.size() && cells < skip)
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    if (!decode_at(text, i, codepoint, len))
    {
      i++;
      cells++;
    }
    else
    {
      int width = codepoint_width(codepoint);
      if (width > 0 && cells + width > skip)
        break;
      i += len;
      cells += width;
    }
  }
  return ".." + ui_take_cells(text.substr((size_t)i), keep);
}

std::string ui_one_line(std::string text)
{
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char c : text)
  {
    unsigned char uc = (unsigned char)c;
    if (c == '\n' || c == '\r' || c == '\t' || std::isspace(uc))
    {
      if (!last_space && !out.empty())
      {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(c);
    last_space = false;
  }
  while (!out.empty() && out.back() == ' ')
  {
    out.pop_back();
  }
  return out;
}

int ui_clamp_to_utf8_boundary(const std::string &text, int byte_index)
{
  if (byte_index <= 0)
    return 0;
  if (byte_index >= (int)text.size())
    return (int)text.size();

  // Fast path: a byte below 0x80 is a complete single-byte codepoint, so the
  // index already sits on a boundary -- a continuation byte is always >= 0x80
  // and an invalid lead byte is left to the slow walk below. The render and
  // edit walks advance one grapheme at a time, and without this every step
  // re-scanned the line from byte 0, making ui_next_grapheme_boundary O(n) per
  // call and a whole-line walk O(n^2).
  if ((unsigned char)text[byte_index] < 0x80)
    return byte_index;

  int i = 0;
  int last = 0;
  while (i < (int)text.size())
  {
    utf8proc_int32_t codepoint = -1;
    int len = 0;
    if (!decode_at(text, i, codepoint, len))
    {
      if (byte_index <= i)
        return last;
      last = ++i;
      continue;
    }
    if (byte_index < i + len)
      return i;
    last = i + len;
    i += len;
  }
  return last;
}

int ui_next_grapheme_boundary(const std::string &text, int byte_index)
{
  const int n = (int)text.size();
  int i = ui_clamp_to_utf8_boundary(text, byte_index);
  if (i >= n)
    return n;

  // Fast path: an ASCII byte always decodes to a single-byte codepoint, and it
  // can only be extended into a cluster by a following combining mark -- all of
  // which encode with a lead byte >= 0xCC. A following ASCII byte therefore
  // always starts the next grapheme, so the decode and the utf8proc property
  // lookup below (the two costs that dominate the per-row column walk on
  // ordinary code) can both be skipped.
  if ((unsigned char)text[i] < 0x80
      && (i + 1 >= n || (unsigned char)text[i + 1] < 0x80))
  {
    return i + 1;
  }

  utf8proc_int32_t codepoint = -1;
  int len = 0;
  if (!decode_at(text, i, codepoint, len))
    return std::min(n, i + 1);
  i += len;

  while (i < n)
  {
    utf8proc_int32_t next = -1;
    int next_len = 0;
    if (!decode_at(text, i, next, next_len))
      break;
    if (!is_combining_mark(next))
      break;
    i += next_len;
  }
  return i;
}

int ui_prev_grapheme_boundary(const std::string &text, int byte_index)
{
  int target = ui_clamp_to_utf8_boundary(text, byte_index);
  if (target <= 0)
    return 0;

  int current = 0;
  int previous = 0;
  while (current < target)
  {
    previous = current;
    current = ui_next_grapheme_boundary(text, current);
    if (current <= previous)
      return previous;
  }
  return previous;
}

std::string ui_normalize_nfc(const std::string &text)
{
  return normalize_with_options(text, (utf8proc_option_t)(UTF8PROC_COMPOSE));
}

std::string ui_normalize_nfd(const std::string &text)
{
  return normalize_with_options(text, (utf8proc_option_t)(UTF8PROC_DECOMPOSE));
}
