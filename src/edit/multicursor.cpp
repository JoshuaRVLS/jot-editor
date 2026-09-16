#include "editor.h"
#include "jot/lua/api.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <chrono>

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

void Editor::restart_blink()
{
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  blink_anchor_ms = now_ms;
  blink_suspend_until_ms = now_ms + 1200;
  blink_visible = true;
  needs_redraw = true;
}

// Deletes at every caret, bottom-up so earlier row shifts never invalidate
// later spans: the main cursor (as a one-grapheme point when no primary
// selection), every active extra-caret span, and every inactive (Alt+click)
// point caret. Overlapping spans (e.g. the caret that shares its cell with
// the main cursor right after an Alt+click) are applied once.
bool Editor::delete_at_all_carets(bool forward)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active && buf.extra_carets.empty())
    return false;

  struct Span
  {
    int start_y, end_y, start_x, end_x;
  };
  auto normalize = [&](const Selection &sel, Span &out) {
    out.start_y = std::min(sel.start.y, sel.end.y);
    out.end_y = std::max(sel.start.y, sel.end.y);
    out.start_x = sel.start.y < sel.end.y
                      ? sel.start.x
                      : (sel.start.y == sel.end.y ? std::min(sel.start.x, sel.end.x)
                                                  : sel.end.x);
    out.end_x = sel.start.y < sel.end.y
                    ? sel.end.x
                    : (sel.start.y == sel.end.y ? std::max(sel.start.x, sel.end.x)
                                                : sel.start.x);
  };
  // One grapheme around a point caret; line joins when it sits on a boundary.
  auto point_span = [&](const Cursor &p, Span &out) {
    const std::string &line = buf.line(p.y);
    const int x = ui_clamp_to_utf8_boundary(line, std::clamp(p.x, 0, (int)line.size()));
    if (forward)
    {
      if (x < (int)line.size())
      {
        out = {p.y, p.y, x, ui_next_grapheme_boundary(line, x)};
      }
      else if (p.y < (int)buf.line_count() - 1)
      {
        out = {p.y, p.y + 1, x, 0};
      }
      else
      {
        out = {p.y, p.y, x, x};
      }
    }
    else if (x > 0)
    {
      out = {p.y, p.y, ui_prev_grapheme_boundary(line, x), x};
    }
    else if (p.y > 0)
    {
      out = {p.y - 1, p.y, (int)buf.line(p.y - 1).size(), 0};
    }
    else
    {
      out = {p.y, p.y, x, x};
    }
  };

  struct SpanEdit
  {
    Span span;
    bool is_primary;
    size_t extra_idx;
  };
  std::vector<SpanEdit> ordered;
  {
    Span s{};
    if (buf.selection.active)
    {
      normalize(buf.selection, s);
    }
    else
    {
      point_span(buf.cursor, s);
    }
    ordered.push_back({s, true, 0});
  }
  for (size_t i = 0; i < buf.extra_carets.size(); i++)
  {
    const Selection &c = buf.extra_carets[i];
    Span s{};
    if (c.active)
    {
      normalize(c, s);
    }
    else
    {
      point_span(c.end, s);
    }
    ordered.push_back({s, false, i});
  }
  // Bottom-up; ties (caret sharing its cell with the main cursor) go to the
  // primary so its cursor always lands on its own span start.
  std::sort(ordered.begin(), ordered.end(), [](const SpanEdit &a, const SpanEdit &b) {
    if (a.span.start_y != b.span.start_y)
      return a.span.start_y > b.span.start_y;
    if (a.span.start_x != b.span.start_x)
      return a.span.start_x > b.span.start_x;
    return a.is_primary && !b.is_primary;
  });

  // Dedupe: a span fully covered by an earlier-applied (lower) span is a
  // duplicate of that deletion and would double-erase the same cells.
  // Spans are half-open intervals in (row, col) lexicographic order, so a
  // point span on row 0 never "overlaps" a join span that starts further
  // right on the same row just because the join spans two rows.
  std::vector<Span> applied;
  auto overlaps_applied = [&](const Span &s) {
    auto lex_less = [](int y1, int x1, int y2, int x2) {
      return y1 < y2 || (y1 == y2 && x1 < x2);
    };
    for (const auto &k : applied)
    {
      if (lex_less(s.start_y, s.start_x, k.end_y, k.end_x)
          && lex_less(k.start_y, k.start_x, s.end_y, s.end_x))
      {
        return true;
      }
    }
    return false;
  };

  save_state();
  for (const auto &item : ordered)
  {
    const Span &s = item.span;
    if (s.start_x == s.end_x && s.start_y == s.end_y)
    {
      // Nothing to delete (buffer edge): normalize the caret to a point.
      if (!item.is_primary)
      {
        auto &caret = buf.extra_carets[item.extra_idx];
        caret.start = {s.start_x, s.start_y};
        caret.end = caret.start;
        caret.active = false;
      }
      continue;
    }
    const Span *covering = nullptr;
    for (const auto &k : applied)
    {
      if (k.start_y > s.end_y || (k.start_y == s.end_y && k.start_x >= s.end_x))
        continue;
      if (s.start_y > k.end_y || (s.start_y == k.end_y && s.start_x >= k.end_x))
        continue;
      covering = &k;
      break;
    }
    if (covering)
    {
      // A duplicate of an applied delete (e.g. the caret sharing its cell
      // with the main cursor after an Alt+click): instead of double-erasing,
      // snap the caret to where the covering deletion left its content.
      if (!item.is_primary)
      {
        auto &caret = buf.extra_carets[item.extra_idx];
        caret.start = {ui_clamp_to_utf8_boundary(buf.line(covering->start_y), covering->start_x),
                       covering->start_y};
        caret.end = caret.start;
        caret.active = false;
      }
      continue;
    }
    applied.push_back(s);
    const int start_x = ui_clamp_to_utf8_boundary(buf.line(s.start_y), s.start_x);
    const int end_x = ui_clamp_to_utf8_boundary(buf.line(s.end_y), s.end_x);
    if (s.start_y == s.end_y)
    {
      buf.line_mut(s.start_y).erase(start_x, end_x - start_x);
    }
    else
    {
      buf.line_mut(s.start_y) =
          buf.line_mut(s.start_y).substr(0, start_x) + buf.line_mut(s.end_y).substr(end_x);
      buf.lines.erase(buf.lines.begin() + s.start_y + 1, buf.lines.begin() + s.end_y + 1);
    }
    if (item.is_primary)
    {
      buf.cursor.y = s.start_y;
      buf.cursor.x = start_x;
      buf.preferred_x = start_x;
    }
    else
    {
      auto &caret = buf.extra_carets[item.extra_idx];
      caret.start = {start_x, s.start_y};
      caret.end = caret.start;
      caret.active = false;
    }
  }

  buf.selection.active = false;
  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
  return true;
}

void Editor::delete_selection_for_test()
{
  delete_selection();
}

void Editor::delete_char_for_test(bool forward)
{
  delete_char(forward);
}

void Editor::insert_string_for_test(const std::string &str)
{
  insert_string(str);
}

void Editor::set_inlay_hints_for_test(const std::string &filepath,
                                      std::vector<LSPInlayHint> hints)
{
  auto &cache = lsp_inlay_hint_caches[filepath];
  cache.hints = std::move(hints);
  // Same normalization the real result path applies (renderer and the
  // coordinate helpers assume position-sorted hints).
  std::sort(cache.hints.begin(),
            cache.hints.end(),
            [](const LSPInlayHint &a, const LSPInlayHint &b)
            {
              if (a.line != b.line)
              {
                return a.line < b.line;
              }
              return a.character < b.character;
            });
}

void Editor::mouse_event_for_test(int x, int y, int bstate)
{
  mouse_event_for_test(x, y, bstate, false);
}

void Editor::mouse_event_for_test(int x, int y, int bstate, bool ctrl)
{
  struct TestMouseEvent
  {
    int x, y, bstate;
    bool ctrl, shift, alt;
  } ev{x, y, bstate, ctrl, false, false};
  handle_mouse(&ev);
}

void Editor::create_new_buffer_for_test()
{
  create_new_buffer();
}

void Editor::move_to_line_start_for_test()
{
  move_to_line_start();
}

void Editor::render_for_test()
{
  render();
}

FileBuffer &Editor::buffer_for_test(int id)
{
  return get_buffer(id);
}

SplitPane &Editor::pane_for_test(int id)
{
  return get_pane(id);
}

bool Editor::mouse_selecting_for_test() const
{
  return mouse_selecting;
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
  restart_blink();
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
          restart_blink();
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

// --- Selection manipulation (helix's selection-first commands) ----------------
//
// All of these work on the primary selection plus the extra carets, and none of
// them mutate the buffer: they only move selections around, so there is no undo
// state to save.

namespace
{
  // The primary as a range, normalised, whether or not it is active.
  void primary_range(const FileBuffer &buf, Cursor &from, Cursor &to)
  {
    if (buf.selection.active)
    {
      from = buf.selection.start;
      to = buf.selection.end;
    }
    else
    {
      from = buf.cursor;
      to = buf.cursor;
    }
    if (from.y > to.y || (from.y == to.y && from.x > to.x))
    {
      std::swap(from, to);
    }
  }
} // namespace

void Editor::keep_primary_selection()
{
  auto &buf = get_buffer();
  if (buf.extra_carets.empty())
  {
    set_message("No extra selections to drop");
    return;
  }
  const size_t dropped = buf.extra_carets.size();
  buf.extra_carets.clear();
  set_message("Kept the primary (dropped " + std::to_string(dropped) + ")");
  needs_redraw = true;
}

bool Editor::rotate_primary_selection(int direction)
{
  auto &buf = get_buffer();
  if (buf.extra_carets.empty())
  {
    set_message("Only one selection");
    return false;
  }
  Selection primary{buf.selection.start, buf.selection.end, buf.selection.active};
  if (!buf.selection.active)
  {
    primary = {buf.cursor, buf.cursor, false};
  }
  // The old primary goes to the far end of the list and the neighbour at the
  // other end takes over, so the caret order travels with the rotation.
  if (direction >= 0)
  {
    const Selection next = buf.extra_carets.front();
    buf.extra_carets.erase(buf.extra_carets.begin());
    buf.extra_carets.push_back(primary);
    buf.selection = next;
  }
  else
  {
    const Selection next = buf.extra_carets.back();
    buf.extra_carets.pop_back();
    buf.extra_carets.insert(buf.extra_carets.begin(), primary);
    buf.selection = next;
  }
  buf.cursor = buf.selection.active ? buf.selection.end : buf.selection.start;
  if (!buf.selection.active)
  {
    buf.cursor = buf.selection.start;
  }
  buf.preferred_x = buf.cursor.x;
  restart_blink();
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

// Copying the primary onto the neighbouring line is how helix grows a column of
// cursors (C / Alt-C): the selection travels with it, clamped to the new line,
// so a rectangle of same-named things can be edited in one pass.
bool Editor::add_caret_on_adjacent_line(int direction)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  Cursor from{};
  Cursor to{};
  primary_range(buf, from, to);
  const int target_y = from.y + direction;
  if (target_y < 0 || target_y >= (int)buf.line_count())
  {
    set_message("No line that way");
    return false;
  }
  if ((int)buf.extra_carets.size() >= kMaxExtraCarets)
  {
    set_message("Too many cursors");
    return false;
  }
  const std::string &line = buf.line(target_y);
  const int len = (int)line.size();
  Selection borrowed;
  borrowed.start = {std::clamp(from.x, 0, len), target_y};
  borrowed.end = {std::clamp(to.x, 0, len), target_y};
  borrowed.active = buf.selection.active && borrowed.start.x != borrowed.end.x;
  if (caret_present(buf, borrowed))
  {
    set_message("Already a cursor there");
    return false;
  }
  Selection primary{buf.selection.start, buf.selection.end, buf.selection.active};
  if (!buf.selection.active)
  {
    primary = {buf.cursor, buf.cursor, false};
  }
  buf.extra_carets.push_back(primary);
  buf.selection = borrowed;
  buf.cursor = borrowed.active ? borrowed.end : borrowed.start;
  buf.preferred_x = buf.cursor.x;
  restart_blink();
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

// One cursor per line of a multi-line selection, at the first non-blank column of
// each (helix's Alt-s): the starting point for editing the same thing on every
// line of a block.
bool Editor::split_selection_on_newlines()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  if (!buf.selection.active)
  {
    set_message("Select several lines first");
    return false;
  }
  Cursor from{};
  Cursor to{};
  primary_range(buf, from, to);
  if (from.y == to.y)
  {
    set_message("Selection is one line");
    return false;
  }

  buf.extra_carets.clear();
  const int last = to.y;
  for (int y = from.y; y <= last; y++)
  {
    const std::string &line = buf.line(y);
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t'))
    {
      indent++;
    }
    const int col = (int)indent;
    const bool is_last = (y == last);
    if (is_last)
    {
      continue; // installed as the primary below
    }
    if ((int)buf.extra_carets.size() >= kMaxExtraCarets)
    {
      set_message("Too many cursors");
      break;
    }
    buf.extra_carets.push_back(Selection{{col, y}, {col, y}, false});
  }
  const std::string &last_line = buf.line(last);
  size_t last_indent = 0;
  while (last_indent < last_line.size()
         && (last_line[last_indent] == ' ' || last_line[last_indent] == '\t'))
  {
    last_indent++;
  }
  buf.selection.start = {(int)last_indent, last};
  buf.selection.end = buf.selection.start;
  buf.selection.active = false;
  buf.cursor = buf.selection.start;
  buf.preferred_x = buf.cursor.x;
  restart_blink();
  ensure_cursor_visible();
  set_message("Split into " + std::to_string(buf.extra_carets.size() + 1) + " cursors");
  needs_redraw = true;
  return true;
}

// Every occurrence of the selection (or of the word under the cursor) becomes a
// selection: Ctrl+D repeated, in one step.
bool Editor::select_all_occurrences()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  std::string needle;
  if (buf.selection.active)
  {
    Cursor from{};
    Cursor to{};
    primary_range(buf, from, to);
    if (from.y != to.y)
    {
      set_message("Select within one line first");
      return false;
    }
    const std::string &line = buf.line(from.y);
    const int a = std::clamp(from.x, 0, (int)line.size());
    const int b = std::clamp(to.x, 0, (int)line.size());
    if (b > a)
    {
      needle = line.substr((size_t)a, (size_t)(b - a));
    }
  }
  if (needle.empty())
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
    {
      return false;
    }
    const std::string &line = buf.line(buf.cursor.y);
    int start = 0;
    int end = 0;
    word_span_at(line, buf.cursor.x, start, end);
    if (start >= end)
    {
      set_message("Nothing to match");
      return false;
    }
    needle = line.substr((size_t)start, (size_t)(end - start));
  }

  buf.extra_carets.clear();
  std::vector<Selection> found;
  for (int y = 0; y < (int)buf.line_count(); y++)
  {
    const std::string &line = buf.line(y);
    size_t pos = line.find(needle);
    while (pos != std::string::npos)
    {
      found.push_back(Selection{{(int)pos, y}, {(int)(pos + needle.size()), y}, true});
      pos = line.find(needle, pos + 1);
    }
  }
  if (found.empty())
  {
    set_message("No occurrence found");
    return false;
  }
  if (found.size() == 1)
  {
    // One occurrence is still the answer: the selection is already the only
    // match, so say so rather than reporting a failure.
    set_message("Only one occurrence");
  }
  // The last occurrence becomes the primary (the caret travels to the end of the
  // list, like Ctrl+D does) and the rest are extras, capped.
  for (size_t i = 0; i + 1 < found.size(); i++)
  {
    if ((int)buf.extra_carets.size() >= kMaxExtraCarets)
    {
      break;
    }
    buf.extra_carets.push_back(found[i]);
  }
  buf.selection = found.back();
  buf.cursor = buf.selection.end;
  buf.preferred_x = buf.cursor.x;
  restart_blink();
  ensure_cursor_visible();
  set_message("Selected " + std::to_string(buf.extra_carets.size() + 1) + " occurrences");
  needs_redraw = true;
  return true;
}

// Selects the word under the cursor: the object the word operators act on, and
// the same span Ctrl+D starts from, so "select the word" and "select the next
// occurrence" agree on what a word is.
bool Editor::select_word_at_cursor()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
  {
    return false;
  }
  const std::string &line = buf.line(buf.cursor.y);
  int start = 0;
  int end = 0;
  word_span_at(line, buf.cursor.x, start, end);
  if (start >= end)
  {
    set_message("No word here");
    return false;
  }
  buf.selection.start = {start, buf.cursor.y};
  buf.selection.end = {end, buf.cursor.y};
  buf.selection.active = true;
  buf.cursor = buf.selection.end;
  buf.preferred_x = buf.cursor.x;
  restart_blink();
  needs_redraw = true;
  return true;
}
