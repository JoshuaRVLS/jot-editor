#include "editor.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>

namespace
{
  constexpr int kMaxExtraCarets = 31;

  bool is_word_byte(char c)
  {
    const unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) || c == '_';
  }

  void word_span_at(const std::string &line, int x, int &start, int &end)
  {
    start = std::clamp(x, 0, (int)line.size());
    end = start;
    while (start > 0 && is_word_byte(line[start - 1]))
      start--;
    while (end < (int)line.size() && is_word_byte(line[end]))
      end++;
  }

  bool same_point(const Cursor &a, const Cursor &b)
  {
    return a.x == b.x && a.y == b.y;
  }

  bool ranges_overlap(const Selection &a, const Selection &b)
  {
    auto norm = [](const Selection &s, Cursor &lo, Cursor &hi) {
      if (!s.active)
      {
        lo = s.start;
        hi = s.start;
        return;
      }
      lo = s.start;
      hi = s.end;
      if (lo.y > hi.y || (lo.y == hi.y && lo.x > hi.x))
        std::swap(lo, hi);
    };
    Cursor a0, a1, b0, b1;
    norm(a, a0, a1);
    norm(b, b0, b1);
    bool a_point = a0 == a1;
    bool b_point = b0 == b1;
    if (a_point && b_point)
      return a0 == b0;
    if (a_point)
      return (a0.y > b0.y || (a0.y == b0.y && a0.x >= b0.x))
             && (a0.y < b1.y || (a0.y == b1.y && a0.x < b1.x));
    if (b_point)
      return (b0.y > a0.y || (b0.y == a0.y && b0.x >= a0.x))
             && (b0.y < a1.y || (b0.y == a1.y && b0.x < a1.x));
    if (a1.y < b0.y || (a1.y == b0.y && a1.x <= b0.x))
      return false;
    if (b1.y < a0.y || (b1.y == a0.y && b1.x <= a0.x))
      return false;
    return true;
  }

  bool caret_present(const FileBuffer &buf, const Selection &candidate)
  {
    Selection primary{buf.selection.start, buf.selection.end, buf.selection.active};
    if (!buf.selection.active)
      primary = {buf.cursor, buf.cursor, false};
    if (ranges_overlap(primary, candidate))
      return true;
    for (const auto &c : buf.extra_carets)
    {
      if (ranges_overlap(c, candidate))
        return true;
    }
    return false;
  }
} // namespace

bool Editor::multicursor_active()
{
  return !get_buffer().extra_carets.empty();
}

void Editor::delete_selection_for_test()
{
  delete_selection();
}

void Editor::insert_string_for_test(const std::string &str)
{
  insert_string(str);
}

void Editor::clear_extra_carets()
{
  auto &buf = get_buffer();
  buf.extra_carets.clear();
  needs_redraw = true;
}

bool Editor::add_caret_at(int line_y, int x)
{
  auto &buf = get_buffer();
  if (line_y < 0 || line_y >= (int)buf.line_count())
    return false;
  if ((int)buf.extra_carets.size() >= kMaxExtraCarets)
  {
    set_message("Too many cursors");
    return false;
  }
  const std::string &line = buf.line(line_y);
  Cursor pos{ui_clamp_to_utf8_boundary(line, std::clamp(x, 0, (int)line.size())), line_y};
  Selection candidate{pos, pos, false};
  if (caret_present(buf, candidate))
    return false;
  buf.extra_carets.push_back(candidate);
  buf.cursor = pos;
  buf.preferred_x = pos.x;
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

bool Editor::select_next_occurrence()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();

  std::string needle;
  Cursor search_from = buf.cursor;
  bool had_selection = buf.selection.active;
  if (had_selection)
  {
    Cursor s = buf.selection.start;
    Cursor e = buf.selection.end;
    if (s.y > e.y || (s.y == e.y && s.x > e.x))
      std::swap(s, e);
    if (s.y != e.y)
      return false;
    if (s.x == e.x)
      return false;
    const std::string &line = buf.line(s.y);
    int sx = std::clamp(s.x, 0, (int)line.size());
    int ex = std::clamp(e.x, 0, (int)line.size());
    if (ex <= sx)
      return false;
    needle = line.substr(sx, ex - sx);
    search_from = e;
  }
  else
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(buf.cursor.y);
    int start = 0;
    int end = 0;
    word_span_at(line, buf.cursor.x, start, end);
    if (start >= end)
      return false;
    needle = line.substr(start, end - start);
    buf.selection.start = {start, buf.cursor.y};
    buf.selection.end = {end, buf.cursor.y};
    buf.selection.active = true;
    search_from = buf.selection.end;
  }
  if (needle.empty())
    return false;

  if (!had_selection)
  {
    needs_redraw = true;
    return true;
  }
  Selection anchor{buf.selection.start, buf.selection.end, true};
  for (int pass = 0; pass < 2; pass++)
  {
    int y0 = pass == 0 ? search_from.y : 0;
    for (int y = y0; y < (int)buf.line_count(); y++)
    {
      const std::string &line = buf.line(y);
      size_t x0 = (pass == 0 && y == search_from.y) ? (size_t)std::max(0, search_from.x) : 0;
      size_t pos = line.find(needle, x0);
      while (pos != std::string::npos)
      {
        Selection candidate{{(int)pos, y}, {(int)(pos + needle.size()), y}, true};
        if (!caret_present(buf, candidate))
        {
          if ((int)buf.extra_carets.size() >= kMaxExtraCarets)
          {
            set_message("Too many cursors");
            return false;
          }
          buf.extra_carets.push_back(anchor);
          buf.selection.start = candidate.start;
          buf.selection.end = candidate.end;
          buf.selection.active = true;
          buf.cursor = candidate.end;
          buf.preferred_x = buf.cursor.x;
          ensure_cursor_visible();
          needs_redraw = true;
          return true;
        }
        pos = line.find(needle, pos + 1);
      }
    }
  }
  set_message("No more occurrences");
  return false;
}
