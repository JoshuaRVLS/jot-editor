// Menu bar mouse support: hover highlighting and item clicks.
#include "editor.h"
#include <algorithm>

bool Editor::handle_menu_bar_mouse(int x, int y, bool is_click, bool is_motion)
{
  if (!kTopBarVisible || !ui)
  {
    return false;
  }

  auto hit_menu_label = [&](int mx) -> int
  {
    for (const auto &segment : menu_bar_segments)
    {
      if (mx >= segment.x && mx < segment.end_x)
      {
        return segment.menu_index;
      }
    }
    return -1;
  };

  if (y == 0)
  {
    int hit = hit_menu_label(x);
    if (hit >= 0)
    {
      if (is_click)
      {
        if (show_menu_bar_dropdown && menu_bar_active == hit)
        {
          close_menu_bar();
        }
        else
        {
          open_menu_bar(hit);
        }
      }
      else if (is_motion && show_menu_bar_dropdown && menu_bar_active != hit)
      {
        open_menu_bar(hit);
      }
      return true;
    }
    if (is_click && show_menu_bar_dropdown)
    {
      close_menu_bar();
      return true;
    }
    return y == 0;
  }

  if (!show_menu_bar_dropdown)
  {
    return false;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size())
  {
    close_menu_bar();
    return true;
  }

  const auto &menu = menus[menu_bar_active];
  int label_x = 0;
  for (const auto &segment : menu_bar_segments)
  {
    if (segment.menu_index == menu_bar_active)
    {
      label_x = segment.x;
      break;
    }
  }
  int max_label = 0;
  for (const auto &item : menu.items)
  {
    max_label = std::max(max_label, (int)item.label.size());
  }
  int menu_w = std::max(18, max_label + 4);
  int menu_h =
      std::min((int)menu.items.size() + 2, std::max(1, ui->get_height() - 1 - status_height));
  int menu_x = std::clamp(label_x, 0, std::max(0, ui->get_render_width() - menu_w));
  int menu_y = topbar_height();

  bool inside = x >= menu_x && x < menu_x + menu_w && y >= menu_y && y < menu_y + menu_h;
  if (!inside)
  {
    if (is_click)
    {
      close_menu_bar();
      return true;
    }
    return false;
  }

  int row = y - menu_y - 1;
  if (row >= 0 && row < (int)menu.items.size() && menu.items[row].enabled)
  {
    menu_bar_selected = row;
    needs_redraw = true;
    if (is_click)
    {
      execute_menu_bar_item(menu_bar_active, row);
    }
  }
  return true;
}

