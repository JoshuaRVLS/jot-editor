// Telescope mouse support: list navigation and selection clicks.
#include "editor.h"
#include <algorithm>

bool Editor::handle_telescope_mouse(
    int x, int y, bool is_click, bool is_double_click, bool is_scroll_up, bool is_scroll_down)
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

  bool inside =
      x >= layout.x && x < layout.x + layout.w && y >= layout.y && y < layout.y + layout.h;
  if (!inside)
  {
    return true;
  }

  bool in_list = x >= layout.list_x - 1 && x < layout.list_x + layout.list_w + 1
                 && y >= layout.list_y && y < layout.list_y + layout.list_h;
  bool in_preview = layout.show_preview && x >= layout.preview_x
                    && x < layout.preview_x + layout.preview_w && y >= layout.preview_y
                    && y < layout.preview_y + layout.preview_h;

  if (is_scroll_up || is_scroll_down)
  {
    int delta = is_scroll_down ? 3 : -3;
    if (in_preview)
    {
      telescope.set_focus(TelescopeFocus::Preview);
      int line_start_y = layout.preview_y + 4;
      int preview_lines_h = std::max(1, layout.preview_y + layout.preview_h - line_start_y);
      telescope.scroll_preview(delta, preview_lines_h);
    }
    else if (in_list || inside)
    {
      telescope.set_focus(TelescopeFocus::Results);
      telescope.move_by(delta);
      telescope.ensure_selected_visible(layout.list_h);
    }
    needs_redraw = true;
    return true;
  }

  if (is_click && in_list)
  {
    telescope.set_focus(TelescopeFocus::Results);
    int target = telescope.get_list_scroll_offset() + (y - layout.list_y);
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

  if (is_click)
  {
    if (x >= layout.query_x && x < layout.query_x + layout.query_w && y == layout.query_y)
    {
      telescope.set_focus(TelescopeFocus::Query);
    }
    else if (in_preview)
    {
      telescope.set_focus(TelescopeFocus::Preview);
    }
    needs_redraw = true;
  }
  return true;
}

