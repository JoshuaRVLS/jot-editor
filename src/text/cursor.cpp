#include "column_utils.h"
#include "core/editor.h"
#include "folding.h"
#include "lua/api/api.h"
#include "ui/text.h"
#include <cctype>

namespace
{
  bool word_grapheme_at(const std::string &line, int pos)
  {
    if (pos < 0 || pos >= (int)line.size())
      return false;
    unsigned char c = static_cast<unsigned char>(line[pos]);
    if (c >= 0x80)
      return true;
    return std::isalnum(c) || c == '_';
  }

  // The word motion's own view of a boundary: a word is what a reader would
  // call one, so an identifier is several stops rather than one. `fooBar` stops
  // at the capital, `x1` and `1x` at the digit run, and the underscores of
  // `snake_case` are crossed rather than landed on -- which is what makes the
  // motion usable in code, where the words are identifiers.
  bool subword_break_between(const std::string &line, int before, int at)
  {
    if (before < 0 || at <= before || at >= (int)line.size())
      return false;
    const unsigned char prev = static_cast<unsigned char>(line[before]);
    const unsigned char next = static_cast<unsigned char>(line[at]);
    if (prev == '_' || next == '_')
      return true;
    if (std::isdigit(prev) != std::isdigit(next))
      return true;
    return std::islower(prev) != 0 && std::isupper(next) != 0;
  }

  // A character the word motion steps over on its way to the next stop: the
  // separators between words, underscores included (see subword_break_between).
  bool word_separator_at(const std::string &line, int pos)
  {
    if (pos < 0 || pos >= (int)line.size())
      return true;
    return !word_grapheme_at(line, pos) || line[pos] == '_';
  }
} // namespace

void Editor::move_cursor(int dx, int dy, bool extend_selection)
{
  auto &buf = get_buffer();

  if (!extend_selection && buf.selection.active)
  {
    clear_selection();
  }
  if (!extend_selection && !buf.extra_carets.empty())
  {
    buf.extra_carets.clear();
  }

  if (extend_selection && !buf.selection.active)
  {
    buf.selection.start = buf.cursor;
    buf.selection.active = true;
  }

  int max_y = buf.line_count() > 0 ? (int)buf.line_count() - 1 : 0;

  if (dy != 0 && dx == 0)
  {
    int target_y =
        Folding::advance_visible_lines(buf.fold_ranges, buf.cursor.y, dy, (int)buf.line_count());
    int desired_x = std::max(buf.cursor.x, std::max(0, buf.preferred_x));
    buf.cursor.y = target_y;
    int line_len = (int)buf.line(buf.cursor.y).length();
    buf.cursor.x = ui_clamp_to_utf8_boundary(buf.line(buf.cursor.y), std::min(desired_x, line_len));
  }
  else
  {
    buf.cursor.y = std::max(0, std::min(max_y, buf.cursor.y + dy));
    const std::string &line = buf.line(buf.cursor.y);
    if (dx > 0 && dy == 0)
    {
      for (int i = 0; i < dx; i++)
        buf.cursor.x = ui_next_grapheme_boundary(line, buf.cursor.x);
    }
    else if (dx < 0 && dy == 0)
    {
      for (int i = 0; i < -dx; i++)
        buf.cursor.x = ui_prev_grapheme_boundary(line, buf.cursor.x);
    }
    else
    {
      buf.cursor.x = ui_clamp_to_utf8_boundary(line, buf.cursor.x + dx);
    }
    // Horizontal movement (or mixed dx/dy) sets new preferred column.
    buf.preferred_x = buf.cursor.x;
  }
  clamp_cursor(get_pane().buffer_id);
  if (dy == 0 && dx == 0)
  {
    buf.preferred_x = buf.cursor.x;
  }
  ensure_cursor_visible();

  if (extend_selection)
  {
    buf.selection.end = buf.cursor;
  }
  needs_redraw = true;
  if (lua_api)
    lua_api->fire_autocmd("CursorMoved", buf.filepath, current_buffer);
}

void Editor::clamp_cursor(int buffer_id)
{
  auto &buf = get_buffer(buffer_id);
  if (buf.line_count() == 0)
  {
    buf.cursor.y = 0;
    buf.cursor.x = 0;
    return;
  }
  if (buf.cursor.y >= (int)buf.line_count())
    buf.cursor.y = buf.line_count() - 1;
  if (buf.cursor.y < 0)
    buf.cursor.y = 0;
  while (buf.cursor.y > 0 && Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y))
  {
    buf.cursor.y--;
  }
  int line_len = buf.line(buf.cursor.y).length();
  buf.cursor.x = ui_clamp_to_utf8_boundary(buf.line(buf.cursor.y),
                                           std::max(0, std::min(line_len, buf.cursor.x)));
}

void Editor::clamp_carets(FileBuffer &buf)
{
  if (buf.line_count() == 0)
  {
    return;
  }
  const int last_line = (int)buf.line_count() - 1;
  // ui_clamp_to_utf8_boundary already answers 0 for a negative column and the
  // line's length for one past its end, so a caret in the middle of a UTF-8
  // sequence comes back to a boundary as well.
  auto clamp_point = [&](Cursor &point)
  {
    point.y = std::max(0, std::min(last_line, point.y));
    point.x = ui_clamp_to_utf8_boundary(buf.line(point.y), point.x);
  };
  clamp_point(buf.cursor);
  buf.preferred_x = buf.cursor.x;
  for (auto &caret : buf.extra_carets)
  {
    clamp_point(caret.start);
    clamp_point(caret.end);
  }
}

void Editor::ensure_cursor_visible(bool adjust_horizontal)
{
  if (panes.empty())
    return;
  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);
  // A tab in this buffer is this wide when the buffer holds a width of its own
  // (jot.buffer.set_option), else as wide as the session says. The horizontal
  // reveal below measures the caret's column the same way the painter does.
  const int tab_size = option_int(buf, "tab_size", this->tab_size);

  int viewport_h = pane_viewport_h(pane);
  if (viewport_h < 1)
    viewport_h = 1;

  // One prepared view for every fold question below (the caret reveal runs on
  // every press, drag cell and keystroke).
  const auto fold_view = Folding::view_of(buf.fold_ranges);
  buf.scroll_offset =
      fold_view->clamp_scroll_offset(buf.scroll_offset, viewport_h, (int)buf.line_count());

  if (buf.cursor.y < buf.scroll_offset)
  {
    buf.scroll_offset = buf.cursor.y;
  }
  else
  {
    const int last_visible = fold_view->buffer_line_for_visible_offset(
        buf.scroll_offset, viewport_h - 1, (int)buf.line_count());
    if (buf.cursor.y > last_visible)
    {
      buf.scroll_offset = buf.cursor.y;
      for (int i = 1; i < viewport_h; i++)
      {
        buf.scroll_offset = fold_view->previous_visible_line(buf.scroll_offset);
      }
    }
  }

  if (adjust_horizontal)
  {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
      draw_w = std::max(1, draw_w - minimap_width);
    int viewport_w = draw_w - 9;
    if (viewport_w < 1)
      viewport_w = 1;

    if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count())
    {
      const std::string &line = buf.line(buf.cursor.y);
      // The caret sits after any inlay hints inserted before it, so the
      // viewport comparison runs in rendered (hint-shifted) coordinates:
      // scroll_x is the anchor and stays unshifted, matching the renderer.
      int cursor_visual =
          compute_visual_column(line, buf.cursor.x, tab_size)
          + lsp_inlay_hint_cells_before(buf.filepath, buf.cursor.y, buf.cursor.x, line);
      int scroll_visual = compute_visual_column(line, buf.scroll_x, tab_size);
      if (cursor_visual < scroll_visual)
      {
        buf.scroll_x = buf.cursor.x;
      }
      else if (cursor_visual >= scroll_visual + viewport_w)
      {
        int cur = buf.scroll_x;
        while (cur < (int)line.size())
        {
          int next = ui_next_grapheme_boundary(line, cur);
          if (next <= cur)
            next = cur + 1;
          int next_visual = compute_visual_column(line, next, tab_size)
                            + lsp_inlay_hint_cells_before(buf.filepath, buf.cursor.y, next, line);
          if (next_visual > cursor_visual - viewport_w + 1)
            break;
          cur = next;
        }
        buf.scroll_x = cur;
      }
    }
    else
    {
      if (buf.cursor.x < buf.scroll_x)
      {
        buf.scroll_x = buf.cursor.x;
      }
      else if (buf.cursor.x >= buf.scroll_x + viewport_w)
      {
        buf.scroll_x = buf.cursor.x - viewport_w + 1;
      }
    }
  }

  if (buf.scroll_x < 0)
    buf.scroll_x = 0;
  if (buf.scroll_offset < 0)
    buf.scroll_offset = 0;
}

void Editor::reveal_cursor_centered()
{
  if (panes.empty())
    return;
  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);

  int viewport_h = pane_viewport_h(pane);
  if (viewport_h < 1)
    viewport_h = 1;

  // A jump that lands somewhere already on screen keeps the view it found: the
  // caret moving is enough to follow, and recentring a view the reader was
  // reading costs them their place. Only a landmark that is not on screen moves
  // the viewport, and then it arrives with the code around it rather than
  // pinned to the last row -- which is where minimal scrolling used to leave it,
  // so half the function was still below the fold.
  const auto fold_view = Folding::view_of(buf.fold_ranges);
  if (fold_view->visible_row_for_line(
          buf.scroll_offset, buf.cursor.y, viewport_h, (int)buf.line_count())
      >= 0)
  {
    ensure_cursor_visible();
    return;
  }

  buf.scroll_offset = buf.cursor.y;
  for (int i = 0; i < viewport_h / 2; i++)
  {
    buf.scroll_offset = fold_view->previous_visible_line(buf.scroll_offset);
  }
  // Clamps, and the caret is inside the viewport now, so the vertical part has
  // nothing left to do; horizontal is what this call is still for.
  ensure_cursor_visible();
}

// Forward word motion: the start of the next word, in the sense above -- across
// the rest of the current one, across whatever separates them (whitespace,
// punctuation, the end of the line, blank lines), and stopping on the first
// character of what it found. A caret sitting on a separator moves to the word
// after it, and one with nothing after it ends at the last line's end.
void Editor::move_word_forward(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  const int line_count = (int)buf.line_count();
  if (line_count <= 0)
    return;

  buf.cursor.y = std::clamp(buf.cursor.y, 0, line_count - 1);
  buf.cursor.x = ui_clamp_to_utf8_boundary(buf.line(buf.cursor.y), buf.cursor.x);

  int x = buf.cursor.x;
  int y = buf.cursor.y;

  // Leave the word the caret is standing on, hump by hump.
  while (!word_separator_at(buf.line(y), x))
  {
    const int next = ui_next_grapheme_boundary(buf.line(y), x);
    if (next <= x)
      break;
    const bool boundary = word_separator_at(buf.line(y), next)
                          || subword_break_between(buf.line(y), x, next);
    x = next;
    if (boundary)
      break;
  }

  // Cross the separator run -- lines, indentation and blank lines included --
  // to the next word. A file that runs out of words leaves the caret at its end.
  while (true)
  {
    const std::string &line = buf.line(y);
    while (x < (int)line.size() && word_separator_at(line, x))
      x = ui_next_grapheme_boundary(line, x);
    if (x < (int)line.size())
      break;
    const int next_y = Folding::advance_visible_lines(buf.fold_ranges, y, 1, line_count);
    if (next_y == y)
    {
      x = (int)line.size();
      break;
    }
    y = next_y;
    x = 0;
  }

  buf.cursor.x = x;
  buf.cursor.y = y;
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

// Backward word motion: the start of the word before the caret, in the same
// sense -- backwards over separators (the start of the line and blank lines
// included), then back to the start of the word, hump by hump. From inside a
// word this reaches that word's own start, which is what makes it the mirror of
// the motion above.
void Editor::move_word_backward(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  const int line_count = (int)buf.line_count();
  if (line_count <= 0)
    return;

  buf.cursor.y = std::clamp(buf.cursor.y, 0, line_count - 1);
  buf.cursor.x = ui_clamp_to_utf8_boundary(buf.line(buf.cursor.y), buf.cursor.x);

  int x = buf.cursor.x;
  int y = buf.cursor.y;

  // Cross separators (and line starts) leftwards, stopping on the first word
  // character, which is the end of the previous word.
  while (true)
  {
    if (x == 0)
    {
      const int prev_y = Folding::advance_visible_lines(buf.fold_ranges, y, -1, line_count);
      if (prev_y == y)
        break; // the start of the buffer: nowhere left to go
      y = prev_y;
      x = (int)buf.line(y).size();
      continue;
    }
    const int prev = ui_prev_grapheme_boundary(buf.line(y), x);
    if (!word_separator_at(buf.line(y), prev))
    {
      x = prev;
      break;
    }
    x = prev;
  }

  // Then walk back to the start of that word's own hump.
  while (x > 0)
  {
    const int prev = ui_prev_grapheme_boundary(buf.line(y), x);
    if (word_separator_at(buf.line(y), prev) || subword_break_between(buf.line(y), prev, x))
      break;
    x = prev;
  }

  buf.cursor.x = x;
  buf.cursor.y = y;
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_smart_start(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;

  const std::string &line = buf.line(buf.cursor.y);
  int first_non_ws = 0;
  while (first_non_ws < (int)line.size()
         && (line[first_non_ws] == ' ' || line[first_non_ws] == '\t'))
  {
    first_non_ws++;
  }
  if (first_non_ws >= (int)line.size())
  {
    first_non_ws = 0;
  }

  int target_x = (buf.cursor.x == first_non_ws) ? 0 : first_non_ws;
  buf.cursor.x = target_x;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();

  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_start(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.x = 0;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_line_end(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.x = buf.line(buf.cursor.y).length();
  clamp_cursor(get_pane().buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_file_start(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  buf.cursor.y = 0;
  buf.cursor.x = 0;
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}

void Editor::move_to_file_end(bool extend_selection)
{
  auto &buf = get_buffer();
  Cursor anchor = buf.cursor;
  if (buf.line_count() == 0)
  {
    buf.cursor.y = 0;
    buf.cursor.x = 0;
  }
  else
  {
    buf.cursor.y = buf.line_count() - 1;
    buf.cursor.x = buf.line(buf.cursor.y).length();
  }
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  if (extend_selection)
  {
    if (!buf.selection.active)
    {
      buf.selection.start = anchor;
      buf.selection.active = true;
    }
    buf.selection.end = buf.cursor;
  }
}
