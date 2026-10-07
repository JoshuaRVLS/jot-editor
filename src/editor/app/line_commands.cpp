// The editor half of the line commands: `:sort`, `:sortdesc`, `:reverselines`,
// `:shufflelines`, `:uniquelines`, `:trimblank`, `:join` and `:stats`.
//
// A command is three things -- a range worked out from the selection, one edit,
// and the bookkeeping around it -- and only the middle one is about text rather
// than about this editor. The middle one is text/line_ops.h and
// text/buffer_stats.h; what is left here is the range, the undo step, the
// cursor, the message line and the two notifications a changed buffer owes (the
// Lua event and the language server).
//
// These bodies used to be Editor:: methods scattered over eight files in
// src/text/, one module below this one, which has no business including
// editor.h. Moving the commands here and the algorithms there is what lets each
// half live where it belongs.
#include "editor.h"
#include "lua/api.h"
#include "text/buffer_stats.h"
#include "text/line_ops.h"

#include <algorithm>
#include <string>

namespace
{
  // The lines a command works on, in forward order and clamped to the buffer.
  // It reads the count rather than the vector because three of these commands
  // materialise a lazy buffer after the range is worked out, exactly as they did
  // before the move.
  struct LineRange
  {
    int first = 0;
    int last = 0;
    bool ok = false;

    // The selection's two ends, whichever way round it was dragged. False when
    // the selection does not name more than one line, which every caller but
    // `:trimblank` reports as "select more lines" and gives up on.
    bool selected(const FileBuffer &buf)
    {
      if (!buf.selection.active || buf.line_count() == 0)
      {
        return false;
      }
      const int last_line = (int)buf.line_count() - 1;
      first = std::clamp(std::min(buf.selection.start.y, buf.selection.end.y), 0, last_line);
      last = std::clamp(std::max(buf.selection.start.y, buf.selection.end.y), 0, last_line);
      ok = first < last;
      return ok;
    }
  };
} // namespace

void Editor::sort_selected_lines()
{
  auto &buf = get_buffer();
  if (buf.line_count() == 0)
  {
    set_message("Nothing to sort");
    return;
  }
  if (!buf.selection.active)
  {
    set_message("Select lines first to sort");
    return;
  }

  LineRange range;
  if (!range.selected(buf))
  {
    set_message("Select multiple lines to sort");
    return;
  }

  save_state();
  if (buf.is_lazy())
    buf.materialize();
  line_ops::sort_lines(buf.lines, range.first, range.last, false);

  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)range.first].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Sorted selected lines");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::sort_selected_lines_desc()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
  {
    set_message("Select lines first: :sortdesc");
    return;
  }

  LineRange range;
  if (!range.selected(buf))
  {
    set_message("Select multiple lines to sort descending");
    return;
  }

  save_state();
  line_ops::sort_lines(buf.lines, range.first, range.last, true);

  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)range.first].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Sorted selected lines (desc)");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::reverse_selected_lines()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
  {
    set_message("Select lines first: :reverselines");
    return;
  }

  LineRange range;
  if (!range.selected(buf))
  {
    set_message("Select multiple lines to reverse");
    return;
  }

  save_state();
  line_ops::reverse_lines(buf.lines, range.first, range.last);

  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)range.first].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Reversed selected lines");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::shuffle_selected_lines()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
  {
    set_message("Select lines first: :shufflelines");
    return;
  }

  LineRange range;
  if (!range.selected(buf))
  {
    set_message("Select multiple lines to shuffle");
    return;
  }

  save_state();
  line_ops::shuffle_lines(buf.lines, range.first, range.last);

  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)range.first].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Shuffled selected lines");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::unique_selected_lines()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
  {
    set_message("Select lines first: :uniquelines");
    return;
  }

  LineRange range;
  if (!range.selected(buf))
  {
    set_message("Select multiple lines for unique");
    return;
  }

  save_state();
  const int removed = line_ops::remove_duplicate_lines(buf.lines, range.first, range.last);
  if (removed == 0)
  {
    set_message("No duplicate lines found");
    return;
  }

  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)buf.cursor.y].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed " + std::to_string(removed) + " duplicate line(s)");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::trim_blank_lines_in_selection()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.empty())
  {
    set_message("Nothing to trim");
    return;
  }

  // `:trimblank` has no such thing as "no selection": with nothing selected it
  // trims the whole buffer, which is what the command is for.
  LineRange range;
  if (buf.selection.active)
  {
    (void)range.selected(buf);
  }
  else
  {
    range.first = 0;
    range.last = (int)buf.lines.size() - 1;
  }

  save_state();
  const int removed = line_ops::remove_blank_lines(buf.lines, range.first, range.last);
  if (removed == 0)
  {
    set_message("No blank lines removed");
    return;
  }

  buf.modified = true;
  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Removed " + std::to_string(removed) + " blank line(s)");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::join_lines_selection_or_current()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.lines.size() <= 1)
  {
    set_message("Nothing to join");
    return;
  }

  // With nothing selected the join is the cursor's line and the one under it.
  LineRange range;
  range.first = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  range.last = std::clamp(buf.cursor.y + 1, 0, (int)buf.lines.size() - 1);
  if (buf.selection.active)
  {
    (void)range.selected(buf);
  }
  if (range.last <= range.first)
  {
    set_message("Nothing to join");
    return;
  }

  save_state();
  const int joins = line_ops::join_lines(buf.lines, range.first, range.last);
  if (joins == 0)
  {
    set_message("Nothing to join");
    return;
  }

  clear_selection();
  buf.modified = true;
  buf.cursor.y = range.first;
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.lines[(size_t)range.first].size());
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
  needs_redraw = true;
  set_message("Joined " + std::to_string(joins + 1) + " lines");
  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  if (!buf.filepath.empty())
  {
    notify_lsp_change(buf.filepath);
  }
}

void Editor::show_buffer_stats()
{
  auto &buf = get_buffer();
  buffer_stats::Counter counter(buf.selection.active,
                                buf.selection.start.y,
                                buf.selection.start.x,
                                buf.selection.end.y,
                                buf.selection.end.x);
  for (int i = 0; i < (int)buf.line_count(); i++)
  {
    counter.add_line(i, buf.line(i));
  }
  const buffer_stats::Counts counts = counter.totals();

  const std::string sel_text =
      buf.selection.active ? std::to_string(counts.selected_chars) + " chars" : std::string("none");
  set_message("Stats: " + std::to_string(counts.lines) + " lines, " + std::to_string(counts.chars)
              + " chars, " + std::to_string(counts.words) + " words, selection: " + sel_text);
  needs_redraw = true;
}
