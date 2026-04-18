#include "bracket.h"
#include "editor.h"
#include <cstdio>

void Editor::render() {
  IntegratedTerminal *active_terminal = get_integrated_terminal();

  if (!needs_redraw) {
    // Keep cursor visibility in sync even when no redraw is needed.
    if (show_command_palette || show_search || show_save_prompt ||
        show_quit_prompt) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      }
      return;
    }
    if (show_integrated_terminal && active_terminal &&
        active_terminal->is_focused()) {
      place_integrated_terminal_cursor();
      return;
    }
    if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      ui->hide_cursor();
      return;
    }
    if (!telescope.is_active() && !panes.empty()) {
      auto &pane = get_pane();
      auto &buf = get_buffer(pane.buffer_id);
      int display_y = buf.cursor.y - buf.scroll_offset + pane.y + 1;
      int display_x = buf.cursor.x - buf.scroll_x + pane.x + 7;
      int min_y = pane.y + 1;
      int max_y = pane.y + pane.h - 1;
      int min_x = pane.x + 1;
      int max_x = pane.x + pane.w - 2;
      if (max_y < min_y)
        max_y = min_y;
      if (max_x < min_x)
        max_x = min_x;
      if (display_y < min_y)
        display_y = min_y;
      if (display_y > max_y)
        display_y = max_y;
      if (display_x < min_x)
        display_x = min_x;
      if (display_x > max_x)
        display_x = max_x;
      ui->set_cursor(display_x, display_y);
    }
    return;
  }

  ui->clear();
  int w = ui->get_width();

  render_tabs();
  update_pane_layout();

  if (telescope.is_active()) {
    render_telescope();
  } else {
    if (image_viewer.is_active()) {
      int img_w = w / 2;
      int editor_w = w - img_w;
      if (!panes.empty()) {
        panes[0].w = editor_w;
      }
      render_image_viewer();
    } else if (show_save_prompt) {
      render_save_prompt();
    } else if (show_quit_prompt) {
      render_quit_prompt();
    } else {
      if (show_sidebar) {
        render_sidebar();
      }
      render_panes();
      render_integrated_terminal();
    }

    render_status_line();
    render_command_palette();
    render_search_panel();
    render_popup();

    if (easter_egg_timer > 0) {
      render_easter_egg();
      easter_egg_timer--;
      needs_redraw = true;
    }

    ui->render();

    int target_cursor_shape = 1;
    if (target_cursor_shape != last_cursor_shape) {
      printf("\033[5 q");
      fflush(stdout);
      last_cursor_shape = target_cursor_shape;
    }

    if (show_command_palette || show_search || show_save_prompt ||
        show_quit_prompt) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      }
    } else if (show_integrated_terminal && active_terminal &&
               active_terminal->is_focused()) {
      place_integrated_terminal_cursor();
    } else if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      ui->hide_cursor();
    } else if (!telescope.is_active()) {
      if (!panes.empty()) {
        auto &pane = get_pane();
        auto &buf = get_buffer(pane.buffer_id);
        int display_y = buf.cursor.y - buf.scroll_offset + pane.y + 1;
        int display_x = buf.cursor.x - buf.scroll_x + pane.x + 7;
        int min_y = pane.y + 1;
        int max_y = pane.y + pane.h - 1;
        int min_x = pane.x + 1;
        int max_x = pane.x + pane.w - 2;
        if (max_y < min_y)
          max_y = min_y;
        if (max_x < min_x)
          max_x = min_x;
        if (display_y < min_y)
          display_y = min_y;
        if (display_y > max_y)
          display_y = max_y;
        if (display_x < min_x)
          display_x = min_x;
        if (display_x > max_x)
          display_x = max_x;
        ui->set_cursor(display_x, display_y);
      }
    }

    needs_redraw = false;
  }
}

void Editor::render_panes() {
  for (const auto &pane : panes) {
    render_pane(pane);
  }
}

void Editor::render_pane(const SplitPane &pane) {
  int w = std::max(1, pane.w);
  if (pane.h <= 0)
    return;

  if (show_minimap && w > 20) {
    w = std::max(1, w - minimap_width);
  }

  render_buffer_content(pane, pane.buffer_id);

  if (show_minimap && pane.w > 20) {
    render_minimap(pane.x + w, pane.y + 1, minimap_width, pane.h - 1,
                   pane.buffer_id);
  }

  UIRect rect = {pane.x, pane.y, w, pane.h};
  int border_fg = pane.active ? theme.fg_bracket_match : theme.fg_panel_border;
  ui->draw_border(rect, border_fg, theme.bg_panel_border);
}
