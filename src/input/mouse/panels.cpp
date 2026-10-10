// Mouse handling that runs before the buffer interaction path: git
// panel, integrated terminal, pane resizing, and scroll-wheel routing.
#include "core/editor.h"
#include "folding.h"
#include "smooth_scroll.h"
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
  // The other half of the prompt gate in handle_mouse: this is the wheel's
  // entry point, and the panel has nothing to scroll -- a wheel over it would
  // scroll the buffer behind it instead.
  if (show_save_prompt || show_rename_prompt || show_quit_prompt)
  {
    return;
  }

  // The completion popup owns the wheel over its own box, the way the palette
  // owns it over its list: a notch walks the selection three rows -- the same
  // step the other lists take -- and the ghost preview follows the row it lands
  // on. The test is against the box the last frame painted, not a layout
  // recomputed here, because what the pointer is over is the picture on screen.
  // It has to come before the block below, which hides the popup on every
  // wheel, and before the buffer's own scroll takes the notch.
  if ((is_scroll_up || is_scroll_down) && lsp_completion_visible
      && !lsp_completion_items.empty() && lsp_completion_box_w > 0
      && x >= lsp_completion_box_x && x < lsp_completion_box_x + lsp_completion_box_w
      && y >= lsp_completion_box_y && y < lsp_completion_box_y + lsp_completion_box_h)
  {
    const int delta = is_scroll_up ? -3 : 3;
    const int sel = std::clamp(lsp_completion_selected + delta,
                               0,
                               (int)lsp_completion_items.size() - 1);
    if (sel != lsp_completion_selected)
    {
      lsp_completion_selected = sel;
      update_lsp_completion_ghost();
    }
    needs_redraw = true;
    return;
  }

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

  // The winbar's drop-down: the wheel walks the rows of the cascade level under
  // the pointer, like the other menus.
  if (winbar_menu_open() && (is_scroll_up || is_scroll_down) && ui
      && winbar_menu_level_at(x, y) >= 0)
  {
    winbar_menu_scroll(winbar_menu_level_at(x, y), is_scroll_up ? -1 : 1);
    return;
  }

  if (is_click && handle_menu_bar_mouse(x, y, true, false))
  {
    return;
  }

  if (show_settings_menu)
  {
    // The wheel walks the selectable list, so its ends are the *filtered*
    // list's ends -- a query that keeps three rows must not let a notch park
    // the selection on a row the search bar is hiding.
    const int settings_matches = (int)settings_filtered.size();
    if (is_scroll_up && settings_matches > 0)
    {
      settings_selected = std::max(0, settings_selected - 3);
      needs_redraw = true;
      return;
    }
    if (is_scroll_down && settings_matches > 0)
    {
      settings_selected = std::min(settings_matches - 1, settings_selected + 3);
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

  // The git panel scrolls its own list; the pointer only has to be in the dock.
  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT && ui)
  {
    int panel_w = effective_right_panel_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = side_column_top();
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside && (is_scroll_up || is_scroll_down))
    {
      scroll_git_panel(is_scroll_up ? -3 : 3);
      return;
    }
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT_DIFF && ui)
  {
    int panel_w = effective_right_panel_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = side_column_top();
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
    int panel_y = side_column_top();
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
        if (row >= 0 && row < (int)outline_panel.visible.size())
        {
          const int flat = outline_panel.visible[(size_t)row];
          // A click on the chevron folds/unfolds; anywhere else selects+jumps.
          const int text_x = panel_x + 1;
          const SymbolMatch &symbol = outline_panel.symbols[(size_t)flat];
          const int chevron_span = symbol.has_children() ? 2 : 0;
          const bool on_chevron = chevron_span > 0 && x >= text_x + symbol.depth * 2
                                  && x < text_x + symbol.depth * 2 + chevron_span;
          if (on_chevron)
          {
            outline_panel.selected = row;
            outline_toggle_expanded(flat);
          }
          else
          {
            outline_panel.selected = row;
            outline_jump_selected();
          }
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
    int panel_y = side_column_top();
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside)
    {
      // A panel taller than the dock (the LeetCode console) scrolls its row
      // list; the renderer clamps the offset to the visible window.
      if (is_scroll_up || is_scroll_down)
      {
        plugin_panel_scroll = std::max(0, plugin_panel_scroll + (is_scroll_up ? -3 : 3));
      }
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

  // The floating terminal scrolls its own scrollback; it declines when the
  // wheel is outside its box.
  if ((is_scroll_up || is_scroll_down)
      && handle_floating_terminal_scroll(x, y, is_scroll_up, is_scroll_down))
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

  // The wheel over a pane's own strip scrolls its tab window, the way the
  // workspace strip below scrolls: the strip owns the notch whether or not
  // anything is hidden, so it can never fall through and scroll the buffer
  // under the pointer.
  if (handle_pane_tab_scroll(x, y, is_scroll_up, is_scroll_down))
  {
    return;
  }

  // The wheel over the workspace strip scrolls it. The strip spans the columns
  // between the explorer and the dock, not the whole row, so the notch only
  // goes to it where it actually paints.
  if ((is_scroll_up || is_scroll_down) && tabline_shown() && y == tabline_y()
      && tabline_contains_x(x))
  {
    if (tabline_scroll(is_scroll_up ? -1 : 1))
    {
      needs_redraw = true;
    }
    return;
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

  int visible_rows = std::max(1, pane_viewport_h(pane));
  const int wheel_step = std::max(1, std::min(5, visible_rows / 6));

  // An image pane has no text to scroll: the wheel pans the viewer's preview
  // instead, so it behaves there the way it does in any other file.
  if ((is_scroll_up || is_scroll_down) && image_viewer.is_image_file(buf.filepath))
  {
    image_viewer.scroll_preview(is_scroll_down ? wheel_step : -wheel_step);
    needs_redraw = true;
    return;
  }

  // Vertical wheel over the code area: a viewport-only scroll, animated over a
  // few frames (ui/smooth_scroll.h). Scrolls arriving while the previous
  // one is still easing extend it instead of restarting it, so a burst reads as
  // one continuous glide.
  if (is_scroll_up || is_scroll_down)
  {
    if (scroll_view_smooth(is_scroll_down ? wheel_step : -wheel_step,
                           SmoothScroll::kWheelDurationMs))
    {
      needs_redraw = true;
    }
    return;
  }
}
