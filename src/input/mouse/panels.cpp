// Mouse handling that runs before the buffer interaction path: git
// panel, integrated terminal, pane resizing, and scroll-wheel routing.
#include "editor.h"
#include "folding.h"
#include <algorithm>
#include <chrono>
#include <string>

void Editor::handle_mouse_input(int x,
                                  int y,
                                  bool is_click,
                                  bool is_scroll_up,
                                  bool is_scroll_down,
                                  bool is_scroll_left,
                                  bool is_scroll_right)
{
  if (is_click || is_scroll_up || is_scroll_down || is_scroll_left || is_scroll_right)
  {
    clear_debugger_breakpoint_hover();
    // Scroll repaints the viewport under the cursor; a stale LSP popup
    // anchored to the old spot would linger. Match what cursor movement
    // does: drop the hover, signature and completion popups the same way
    // the keyboard path cancels them.
    cancel_lsp_mouse_hover();
    hide_lsp_signature();
    hide_lsp_completion();
  }

  if (is_click && handle_menu_bar_mouse(x, y, true, false))
  {
    return;
  }

  if (show_settings_menu)
  {
    if (is_scroll_up && !settings_entries.empty())
    {
      settings_selected = std::max(0, settings_selected - 3);
      needs_redraw = true;
      return;
    }
    if (is_scroll_down && !settings_entries.empty())
    {
      settings_selected =
          std::min((int)settings_entries.size() - 1, settings_selected + 3);
      needs_redraw = true;
      return;
    }
    if (is_click && handle_settings_mouse(x, y, true))
    {
      return;
    }
    return;
  }

  // Command palette / quick pick own the wheel while open (scroll moves the
  // selection; clicks go through the handle_mouse path).
  if (show_command_palette)
  {
    if (is_scroll_up || is_scroll_down)
    {
      handle_palette_mouse(x, y, false, is_scroll_up, is_scroll_down);
      return;
    }
    return;
  }

  if (show_quick_pick)
  {
    if (is_scroll_up || is_scroll_down)
    {
      handle_quick_pick_mouse(x, y, false, is_scroll_up, is_scroll_down);
      return;
    }
    return;
  }

  if (show_home_menu)
  {
    if (is_click)
    {
      handle_home_menu_mouse(x, y, true);
      return;
    }

    if (is_scroll_up && !home_menu_entries.empty())
    {
      home_menu_selected =
          (home_menu_selected - 1 + (int)home_menu_entries.size()) % (int)home_menu_entries.size();
      needs_redraw = true;
      return;
    }
    if (is_scroll_down && !home_menu_entries.empty())
    {
      home_menu_selected = (home_menu_selected + 1) % (int)home_menu_entries.size();
      needs_redraw = true;
      return;
    }
    return;
  }

  if (show_tree_sitter_status_modal)
  {
    if (is_scroll_up)
    {
      tree_sitter_status_scroll = std::max(0, tree_sitter_status_scroll - 3);
      needs_redraw = true;
      return;
    }
    if (is_scroll_down)
    {
      tree_sitter_status_scroll += 3;
      needs_redraw = true;
      return;
    }
    if (is_click)
    {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54)
      {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16)
      {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = x >= modal_x && x < modal_x + modal_w && y >= modal_y && y < modal_y + modal_h;
      if (!inside)
      {
        show_tree_sitter_status_modal = false;
      }
      needs_redraw = true;
      return;
    }
    return;
  }

  if (show_lsp_status_modal)
  {
    if (is_scroll_up)
    {
      lsp_status_scroll = std::max(0, lsp_status_scroll - 3);
      needs_redraw = true;
      return;
    }
    if (is_scroll_down)
    {
      lsp_status_scroll += 3;
      needs_redraw = true;
      return;
    }
    if (is_click)
    {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54)
      {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16)
      {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = x >= modal_x && x < modal_x + modal_w && y >= modal_y && y < modal_y + modal_h;
      if (!inside)
      {
        show_lsp_status_modal = false;
      }
      needs_redraw = true;
      return;
    }
    return;
  }

  if (is_click && begin_right_panel_resize_drag(x, y))
  {
    return;
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT_DIFF && ui)
  {
    int panel_w = effective_right_panel_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = topbar_height();
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside)
    {
      if (is_scroll_up)
      {
        scroll_git_diff_panel(-3);
      }
      else if (is_scroll_down)
      {
        scroll_git_diff_panel(3);
      }
      else if (is_click)
      {
        needs_redraw = true;
      }
      return;
    }
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_SYMBOLS && ui)
  {
    int panel_w = effective_right_panel_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = topbar_height();
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside)
    {
      if (is_scroll_up)
      {
        outline_move_selection(-3);
      }
      else if (is_scroll_down)
      {
        outline_move_selection(3);
      }
      else if (is_click)
      {
        ensure_outline_fresh();
        int row = y - (panel_y + 4) + outline_panel.scroll;
        if (row >= 0 && row < (int)outline_panel.symbols.size())
        {
          outline_panel.selected = row;
          outline_jump_selected();
        }
        else
        {
          needs_redraw = true;
        }
      }
      return;
    }
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_PLUGIN && ui)
  {
    int panel_w = effective_right_panel_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = topbar_height();
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside)
    {
      needs_redraw = true;
      return;
    }
  }

  if (handle_right_panel_tab_strip_mouse(x, y, is_click))
  {
    return;
  }

  if (handle_debugger_mouse(x, y, is_click, is_scroll_up, is_scroll_down))
  {
    return;
  }

  if (handle_git_panel_mouse(x, y, is_click, false))
  {
    return;
  }

  // The panel's Problems view scrolls its own list; the handler declines when
  // the panel shows the shell instead.
  if ((is_scroll_up || is_scroll_down)
      && handle_problems_scroll(x, y, is_scroll_up, is_scroll_down))
  {
    return;
  }

  if ((is_scroll_up || is_scroll_down)
      && handle_integrated_terminal_scroll(x, y, is_scroll_up, is_scroll_down))
  {
    return;
  }

  if ((is_scroll_up || is_scroll_down) && !panes.empty())
  {
    int pane_index = -1;
    for (int i = 0; i < (int)panes.size(); i++)
    {
      const auto &candidate = panes[i];
      if (x >= candidate.x && x < candidate.x + candidate.w && y == candidate.y)
      {
        pane_index = i;
        break;
      }
    }
    if (pane_index >= 0)
    {
      if (pane_index != current_pane)
      {
        activate_pane(pane_index);
      }
      auto &pane = get_pane(current_pane);
      int draw_w = std::max(1, pane.w);
      if (show_minimap && draw_w > 20)
      {
        draw_w = std::max(1, draw_w - minimap_width);
      }
      FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
      if (x >= tabs.x && x < tabs.x + tabs.w)
      {
        if (scroll_local_tabs(pane, is_scroll_up ? -1 : 1))
        {
          needs_redraw = true;
        }
        return;
      }
    }
  }

  if (show_sidebar)
  {
    int sidebar_w = effective_sidebar_width();
    if (x < sidebar_w)
    {
      if (is_scroll_up)
      {
        if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
        {
          if (git_sidebar_scroll > 0)
            git_sidebar_scroll--;
        }
        else if (file_tree_scroll > 0)
        {
          file_tree_scroll--;
        }
        needs_redraw = true;
      }
      else if (is_scroll_down)
      {
        if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
        {
          git_sidebar_scroll++;
          int view_h = std::max(1, sidebar_list_rows());
          int max_scroll = std::max(0, (int)build_git_sidebar_rows().size() - view_h);
          git_sidebar_scroll = std::clamp(git_sidebar_scroll, 0, max_scroll);
        }
        else
        {
          file_tree_scroll++;
        }
        needs_redraw = true;
      }
      else if (is_click)
      {
        focus_state = FOCUS_SIDEBAR;
        handle_sidebar_mouse(x, y, is_click, false);
      }
      return;
    }
  }

  if (is_click)
  {
    focus_state = FOCUS_EDITOR;
  }

  // Horizontal wheel (SGR buttons 66/67, or Shift+vertical wheel): shift
  // the viewport sideways. Handled before the pane-hit test so scrolling
  // works in single-pane and multi-pane alike, even when the cursor sits
  // on a gutter, border, or dead spot the hit test would reject.
  int h_step = 0;
  if (is_scroll_left)
    h_step = -4;
  else if (is_scroll_right)
    h_step = 4;
  if (h_step != 0)
  {
    auto &target_pane = get_pane(current_pane);
    auto &target_buf = get_buffer(target_pane.buffer_id);
    target_buf.scroll_x = std::max(0, target_buf.scroll_x + h_step);
    needs_redraw = true;
    return;
  }

  int pane_index = -1;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    const auto &pane = panes[i];
    if (x >= pane.x && x < pane.x + pane.w && y >= pane.y && y < pane.y + pane.h)
    {
      pane_index = i;
      break;
    }
  }
  if (pane_index == -1)
    return;

  if (is_click && pane_index != current_pane)
  {
    activate_pane(pane_index);
  }

  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);
  refresh_folds(buf);

  int visible_rows = std::max(1, pane.h - tab_height - 1);
  const int wheel_step = std::max(1, std::min(5, visible_rows / 6));

  if (is_scroll_up)
  {
    for (int i = 0; i < wheel_step && buf.scroll_offset > 0; i++)
    {
      int prev = Folding::previous_visible_line(buf.fold_ranges, buf.scroll_offset);
      if (prev == buf.scroll_offset)
      {
        break;
      }
      buf.scroll_offset = prev;
      needs_redraw = true;
    }
    return;
  }
  if (is_scroll_down)
  {
    // Walk the next visible lines directly instead of counting all visible
    // lines before the scroll offset (O(scroll_offset) per wheel event).
    for (int i = 0; i < wheel_step; i++)
    {
      int next =
          Folding::next_visible_line(buf.fold_ranges, buf.scroll_offset, (int)buf.line_count());
      if (next <= buf.scroll_offset || next >= (int)buf.line_count())
      {
        break;
      }
      buf.scroll_offset = next;
      needs_redraw = true;
    }
    return;
  }
}

