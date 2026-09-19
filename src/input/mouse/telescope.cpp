// Telescope mouse support: hover highlights the row under the pointer, clicks
// select (double-click opens), and the wheel moves the list or scrolls the file
// view, depending on which box it is over.
#include "editor.h"
#include <algorithm>

bool Editor::handle_telescope_mouse(int x,
                                    int y,
                                    bool is_click,
                                    bool is_double_click,
                                    bool is_scroll_up,
                                    bool is_scroll_down,
                                    bool is_motion)
{
  if (!telescope.is_active())
  {
    return false;
  }

  TelescopeLayout layout = telescope_layout_for(
      ui->get_render_width(), ui->get_height(), 1, std::max(1, ui->get_height() - status_height));
  if (!layout.valid)
  {
    return false;
  }

  const bool inside_list =
      x >= layout.x && x < layout.x + layout.w && y >= layout.y && y < layout.y + layout.h;
  const bool inside_view =
      layout.show_preview && x >= layout.preview_x && x < layout.preview_x + layout.preview_w
      && y >= layout.preview_y && y < layout.preview_y + layout.preview_h;
  if (!inside_list && !inside_view)
  {
    // A press outside dismisses nothing (the picker owns the keyboard), it just
    // does not act on it.
    return true;
  }

  const bool over_rows =
      inside_list && y >= layout.list_y && y < layout.list_y + layout.list_h && layout.list_h > 0;

  if (is_scroll_up || is_scroll_down)
  {
    const int delta = is_scroll_down ? 3 : -3;
    if (inside_view)
    {
      telescope.set_focus(TelescopeFocus::Preview);
      telescope.scroll_preview(delta, std::max(1, layout.preview_inner_h));
    }
    else
    {
      telescope.set_focus(TelescopeFocus::Results);
      telescope.move_by(delta);
      telescope.ensure_selected_visible(layout.list_h);
    }
    needs_redraw = true;
    return true;
  }

  if (is_click && over_rows)
  {
    telescope.set_focus(TelescopeFocus::Results);
    const int target = telescope.get_list_scroll_offset() + (y - layout.list_y);
    if (target >= 0 && target < telescope.get_result_count())
    {
      telescope.select_index(target);
      telescope.ensure_selected_visible(layout.list_h);
      if (is_double_click)
      {
        accept_telescope_selection();
      }
      needs_redraw = true;
    }
    return true;
  }

  // Hover: the row under the pointer becomes the selection, so the highlight
  // band follows the mouse the way it follows j/k.
  if (is_motion && over_rows && !is_click)
  {
    const int target = telescope.get_list_scroll_offset() + (y - layout.list_y);
    if (target >= 0 && target < telescope.get_result_count()
        && target != telescope.get_selected_index())
    {
      telescope.select_index(target);
      telescope.ensure_selected_visible(layout.list_h);
      needs_redraw = true;
    }
    return true;
  }

  if (is_motion && inside_view)
  {
    return true;
  }

  if (is_click)
  {
    if (inside_list && y == layout.query_y)
    {
      telescope.set_focus(TelescopeFocus::Query);
    }
    else if (inside_view)
    {
      telescope.set_focus(TelescopeFocus::Preview);
    }
    needs_redraw = true;
    return true;
  }
  return true;
}
