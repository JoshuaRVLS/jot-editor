#include "editor.h"
#include "text_features.h"
#include <algorithm>
#include <cctype>

void Editor::accept_telescope_selection()
{
  if (!telescope.can_accept_selection())
  {
    set_message(telescope.scan_pending() ? "Telescope is scanning" : "No file selected");
    needs_redraw = true;
    return;
  }
  std::string path = telescope.get_selected_path();
  if (path.empty())
  {
    return;
  }
  if (fs::is_directory(path))
  {
    auto scan_tq = task_queue_.get();
    telescope.select();
    telescope.scan_async(scan_tq, [this] { needs_redraw = true; });
    needs_redraw = true;
    return;
  }

  std::string ext = get_file_extension(path);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp"
      || ext == ".svg" || ext == ".webp" || ext == ".ico" || ext == ".tif" || ext == ".tiff"
      || ext == ".avif" || ext == ".heic" || ext == ".ppm" || ext == ".pgm" || ext == ".pbm"
      || ext == ".xpm" || ext == ".jxl")
  {
    image_viewer.open(path);
  }
  else
  {
    open_file(path);
  }
  telescope.close();
  needs_redraw = true;
}

void Editor::handle_telescope(int ch)
{
  auto scan_tq = task_queue_.get();

  if (ch == 27)
  {
    telescope.close();
    needs_redraw = true;
    return;
  }

  if (ch == '\t')
  {
    telescope.cycle_focus(1);
    needs_redraw = true;
    return;
  }

  if (telescope.focus() == TelescopeFocus::Preview)
  {
    if (ch == 1008 || ch == 'k' || ch == 16)
    {
      telescope.scroll_preview(-1, 10);
    }
    else if (ch == 1009 || ch == 'j' || ch == 14)
    {
      telescope.scroll_preview(1, 10);
    }
    else if (ch == 1015)
    {
      telescope.scroll_preview(-10, 10);
    }
    else if (ch == 1016)
    {
      telescope.scroll_preview(10, 10);
    }
    else
    {
      telescope.set_focus(TelescopeFocus::Query);
    }
    needs_redraw = true;
    if (telescope.focus() == TelescopeFocus::Preview)
      return;
  }

  if (ch == 1011)
  {
    std::string q = telescope.get_query();
    if (q.empty())
    {
      telescope.go_parent();
      telescope.scan_async(scan_tq, [this] { needs_redraw = true; });
    }
    else
    {
      q.pop_back();
      telescope.set_query(q, scan_tq, [this] { needs_redraw = true; });
    }
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13)
  {
    accept_telescope_selection();
    return;
  }

  if (ch == 1008 || ch == 'k' || ch == 16)
  { // Up or Ctrl+P
    telescope.set_focus(TelescopeFocus::Results);
    telescope.move_up();
    needs_redraw = true;
    return;
  }
  if (ch == 1009 || ch == 'j' || ch == 14)
  { // Down or Ctrl+N
    telescope.set_focus(TelescopeFocus::Results);
    telescope.move_down();
    needs_redraw = true;
    return;
  }
  if (ch == 1015)
  { // Page up
    telescope.set_focus(TelescopeFocus::Results);
    for (int i = 0; i < 10; i++)
    {
      telescope.move_up();
    }
    needs_redraw = true;
    return;
  }
  if (ch == 1016)
  { // Page down
    telescope.set_focus(TelescopeFocus::Results);
    for (int i = 0; i < 10; i++)
    {
      telescope.move_down();
    }
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8)
  {
    telescope.set_focus(TelescopeFocus::Query);
    std::string q = telescope.get_query();
    if (!q.empty())
    {
      q.pop_back();
      telescope.set_query(q, scan_tq, [this] { needs_redraw = true; });
    }
    else
    {
      telescope.go_parent();
      telescope.scan_async(scan_tq, [this] { needs_redraw = true; });
    }
    needs_redraw = true;
    return;
  }

  if (ch == 21)
  { // Ctrl+U: clear query
    telescope.set_focus(TelescopeFocus::Query);
    telescope.set_query("", scan_tq, [this] { needs_redraw = true; });
    needs_redraw = true;
    return;
  }

  if (ch >= 32 && ch < 127)
  {
    telescope.set_focus(TelescopeFocus::Query);
    std::string q = telescope.get_query();
    q += (char)ch;
    telescope.set_query(q, scan_tq, [this] { needs_redraw = true; });
    needs_redraw = true;
    return;
  }
}

void Editor::toggle_minimap()
{
  show_minimap = !show_minimap;
}

void Editor::jump_to_matching_bracket()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
    buf.materialize();
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
    return;
  if (buf.cursor.x < 0 || buf.cursor.x >= (int)buf.line(buf.cursor.y).length())
    return;

  char ch = buf.char_at(buf.cursor.y, buf.cursor.x);
  int match = -1;

  if (ch == '(')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '(', ')');
  else if (ch == ')')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '(', ')');
  else if (ch == '{')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '{', '}');
  else if (ch == '}')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '{', '}');
  else if (ch == '[')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '[', ']');
  else if (ch == ']')
    match = EditorFeatures::find_matching_bracket(buf.lines, buf.cursor.y, buf.cursor.x, '[', ']');

  if (match >= 0)
  {
    buf.cursor.y = match / 10000;
    buf.cursor.x = match % 10000;
    clamp_cursor(get_pane().buffer_id);
    ensure_cursor_visible();
    needs_redraw = true;
    message = "Jumped to matching bracket";
  }
}

void Editor::select_current_function()
{
  auto &buf = get_buffer();
  if (buf.line_count() == 0)
  {
    return;
  }

  int open_line = -1;
  int open_col = -1;
  int depth = 0;

  for (int y = buf.cursor.y; y >= 0; --y)
  {
    const std::string &line = buf.line(y);
    if (line.empty())
    {
      continue;
    }

    int start_x =
        (y == buf.cursor.y) ? std::min(buf.cursor.x, (int)line.size() - 1) : (int)line.size() - 1;
    for (int x = start_x; x >= 0; --x)
    {
      char ch = line[x];
      if (ch == '}')
      {
        depth++;
      }
      else if (ch == '{')
      {
        if (depth == 0)
        {
          open_line = y;
          open_col = x;
          break;
        }
        depth--;
      }
    }

    if (open_line != -1)
    {
      break;
    }
  }

  if (open_line == -1 || open_col == -1)
  {
    set_message("No surrounding function block found");
    needs_redraw = true;
    return;
  }

  if (buf.is_lazy())
    buf.materialize();
  int match = EditorFeatures::find_matching_bracket(buf.lines, open_line, open_col, '{', '}');
  if (match < 0)
  {
    set_message("Function end not found");
    needs_redraw = true;
    return;
  }

  int close_line = match / 10000;
  int close_col = match % 10000;

  buf.selection.start = {0, open_line};
  buf.selection.end = {close_col + 1, close_line};
  buf.selection.active = true;
  buf.cursor = buf.selection.end;
  buf.preferred_x = buf.cursor.x;

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  set_message("Selected function block");
  needs_redraw = true;
}
