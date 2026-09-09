#include "commenting.h"
#include "editor.h"
#include "features/language.h"
#include "jot/lua/api.h"
#include "text_features.h"
#include <algorithm>
#include <cctype>

namespace
{
  int count_one_indent_level(const std::string &line, int tab_size)
  {
    if (line.empty())
      return 0;

    if (line[0] == '\t')
    {
      return 1;
    }

    int removed = 0;
    while (removed < tab_size && removed < (int)line.size() && line[removed] == ' ')
    {
      removed++;
    }
    return removed;
  }

  int remove_one_indent_level(std::string &line, int tab_size)
  {
    int removed = count_one_indent_level(line, tab_size);
    if (removed > 0)
    {
      line.erase(0, removed);
    }
    return removed;
  }

  bool should_indent_after_line(const FileBuffer &buf, const std::string &line)
  {
    if (Language::is_python_file(buf.filepath))
    {
      return EditorFeatures::should_python_auto_indent(line);
    }
    if (Language::is_lua_file(buf.filepath))
    {
      return EditorFeatures::should_lua_auto_indent(line);
    }
    return EditorFeatures::should_auto_indent(line);
  }

} // namespace

void Editor::duplicate_line()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, buf.lines[buf.cursor.y]);
  buf.cursor.y++;
  buf.modified = true;
  needs_redraw = true;
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::insert_line_below()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  // Compute indent from current line
  std::string indent_str = "";
  if (auto_indent)
  {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    if (should_indent_after_line(buf, buf.lines[buf.cursor.y]))
      indent += tab_size;
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y + 1, indent_str);
  buf.cursor.y++;
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::insert_line_above()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  std::string indent_str = "";
  if (auto_indent)
  {
    int indent = EditorFeatures::get_indent_level(buf.lines[buf.cursor.y]);
    indent_str = EditorFeatures::get_indent_string(indent, tab_size);
  }
  buf.lines.insert(buf.lines.begin() + buf.cursor.y, indent_str);
  buf.cursor.x = indent_str.length();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::indent_selection()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (!buf.selection.active)
    return;

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);
  const std::string indent(tab_size, ' ');

  for (int y = start_y; y <= end_y; y++)
  {
    buf.lines[y].insert(0, indent);
  }

  buf.selection.start.x += tab_size;
  buf.selection.end.x += tab_size;
  buf.cursor.x += tab_size;

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::outdent_selection()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();

  if (!buf.selection.active)
  {
    int y = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
    int removed = count_one_indent_level(buf.lines[y], tab_size);
    if (removed <= 0)
    {
      return;
    }

    save_state();
    buf.lines[y].erase(0, removed);
    buf.cursor.x = std::max(0, buf.cursor.x - removed);

    clamp_cursor(get_pane().buffer_id);
    ensure_cursor_visible();
    buf.modified = true;
    needs_redraw = true;
    if (lua_api)
      lua_api->on_buffer_change(buf.filepath, "");
    if (!buf.filepath.empty())
      notify_lsp_change(buf.filepath);
    return;
  }

  save_state();

  const int start_y = std::min(buf.selection.start.y, buf.selection.end.y);
  const int end_y = std::max(buf.selection.start.y, buf.selection.end.y);

  int removed_start = 0;
  int removed_end = 0;
  int removed_cursor = 0;

  for (int y = start_y; y <= end_y; y++)
  {
    int removed = remove_one_indent_level(buf.lines[y], tab_size);
    if (y == buf.selection.start.y)
      removed_start = removed;
    if (y == buf.selection.end.y)
      removed_end = removed;
    if (y == buf.cursor.y)
      removed_cursor = removed;
  }

  buf.selection.start.x = std::max(0, buf.selection.start.x - removed_start);
  buf.selection.end.x = std::max(0, buf.selection.end.x - removed_end);
  buf.cursor.x = std::max(0, buf.cursor.x - removed_cursor);

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  buf.modified = true;
  needs_redraw = true;
  if (lua_api)
    lua_api->on_buffer_change(buf.filepath, "");
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}

void Editor::toggle_comment()
{
  save_state();
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  const commenting::Style style = commenting::style_for(get_file_extension(buf.filepath));

  const bool multi_line = buf.selection.active
                          && buf.selection.start.y != buf.selection.end.y;
  int start_y =
      buf.selection.active ? std::min(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;
  int end_y =
      buf.selection.active ? std::max(buf.selection.start.y, buf.selection.end.y) : buf.cursor.y;

  // Re-toggle support: a no-selection toggle with the cursor inside the
  // range of the last multi-line toggle applies to that same range again, so
  // select -> Ctrl+/ -> Ctrl+/ still cycles after the selection is cleared.
  const bool used_selection = buf.selection.active;
  const bool used_memory = !used_selection && buf.last_comment_start >= 0
                           && buf.cursor.y >= buf.last_comment_start
                           && buf.cursor.y <= buf.last_comment_end;
  bool block_boundary = used_memory;
  if (used_memory)
  {
    start_y = buf.last_comment_start;
    end_y = buf.last_comment_end;
  }

  // Without a selection, the cursor may still sit on a block boundary (the
  // opener or closer of a /* ... */ or <!-- ... --> pair). Treat the whole
  // enclosing block as the toggle range so a second Ctrl+/ after a block
  // wrap unwraps it from either edge.
  if (!buf.selection.active && !used_memory && !style.block_open.empty()
      && !style.block_close.empty())
  {
    const std::string cur_body =
        buf.lines[start_y].substr(commenting::line_indent(buf.lines[start_y]).size());
    if (cur_body.compare(0, style.block_open.size(), style.block_open) == 0)
    {
      // Opener on the cursor line: scan forward for the matching closer.
      for (int i = start_y; i < (int)buf.lines.size(); i++)
      {
        const std::string body = buf.lines[i].substr(commenting::line_indent(buf.lines[i]).size());
        if (body.size() >= style.block_close.size()
            && body.compare(body.size() - style.block_close.size(),
                            style.block_close.size(),
                            style.block_close)
                   == 0)
        {
          end_y = i;
          block_boundary = true;
          break;
        }
      }
    }
    else if (cur_body.size() >= style.block_close.size()
             && cur_body.compare(cur_body.size() - style.block_close.size(),
                                 style.block_close.size(),
                                 style.block_close)
                    == 0)
    {
      // Closer on the cursor line: scan backward for the opener.
      for (int i = start_y; i >= 0; i--)
      {
        const std::string body = buf.lines[i].substr(commenting::line_indent(buf.lines[i]).size());
        if (body.compare(0, style.block_open.size(), style.block_open) == 0)
        {
          start_y = i;
          block_boundary = true;
          break;
        }
      }
    }
  }

  commenting::toggle_lines(buf.lines, style, start_y, end_y, multi_line, block_boundary);

  buf.modified = true;
  needs_redraw = true;
  // The toggle consumes the selection. Leaving it armed is a footgun: the
  // next keystroke (e.g. ':' to save) replaces the freshly wrapped range
  // instead of typing normally. The re-toggle memory above keeps double-tap
  // cycling working without the armed selection.
  buf.selection.active = false;
  if (used_selection || used_memory || block_boundary)
  {
    buf.last_comment_start = start_y;
    buf.last_comment_end = end_y;
  }
  else
  {
    buf.last_comment_start = -1;
    buf.last_comment_end = -1;
  }
  clamp_cursor(get_pane().buffer_id);
  if (!buf.filepath.empty())
    notify_lsp_change(buf.filepath);
}
