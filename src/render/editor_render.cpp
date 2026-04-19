#include "bracket.h"
#include "editor.h"
#include <cstdio>

namespace {
constexpr int kLineNumberGutterWidth = 7;

int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

int compute_visual_column(const std::string &line, int logical_col,
                          int tab_size) {
  int clamped = std::clamp(logical_col, 0, (int)line.size());
  int visual = 0;
  for (int i = 0; i < clamped; i++) {
    if (line[i] == '\t') {
      visual += tab_advance(visual, tab_size);
    } else {
      visual += 1;
    }
  }
  return visual;
}

void compute_code_cursor_screen_pos(const SplitPane &pane, const FileBuffer &buf,
                                    bool show_minimap, int minimap_width,
                                    int tab_size,
                                    int &display_x, int &display_y) {
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int code_start_x = pane.x + 1 + kLineNumberGutterWidth;
  const int code_end_x = pane.x + draw_w - 2;
  const int min_y = pane.y + 1;
  int max_y = pane.y + pane.h - 1;

  display_y = buf.cursor.y - buf.scroll_offset + pane.y + 1;

  int logical_cursor_x = buf.cursor.x;
  int logical_scroll_x = buf.scroll_x;
  if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.lines.size()) {
    const std::string &line = buf.lines[buf.cursor.y];
    int cursor_visual = compute_visual_column(line, logical_cursor_x, tab_size);
    int scroll_visual = compute_visual_column(line, logical_scroll_x, tab_size);
    display_x = code_start_x + (cursor_visual - scroll_visual);
  } else {
    display_x = code_start_x + (logical_cursor_x - logical_scroll_x);
  }

  if (max_y < min_y)
    max_y = min_y;
  if (display_y < min_y)
    display_y = min_y;
  if (display_y > max_y)
    display_y = max_y;

  if (code_end_x < code_start_x) {
    display_x = code_start_x;
    return;
  }
  if (display_x < code_start_x)
    display_x = code_start_x;
  if (display_x > code_end_x)
    display_x = code_end_x;
}
} // namespace

void Editor::render() {
  IntegratedTerminal *active_terminal = get_integrated_terminal();

  if (!needs_redraw) {
    if (show_home_menu) {
      ui->hide_cursor();
      return;
    }

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
      int display_x = 0;
      int display_y = 0;
      compute_code_cursor_screen_pos(pane, buf, show_minimap, minimap_width,
                                     tab_size,
                                     display_x, display_y);
      ui->set_cursor(display_x, display_y);
    }
    return;
  }

  ui->clear();
  int w = ui->get_width();

  if (show_home_menu) {
    render_home_menu();
    render_status_line();
    ui->render();
    ui->hide_cursor();
    needs_redraw = false;
    return;
  }

  render_tabs();
  update_pane_layout();

  if (telescope.is_active()) {
    // Keep the editor visible and draw Telescope as an overlay instead of
    // replacing the whole scene.
    if (show_sidebar) {
      render_sidebar();
    }
    render_panes();
    render_lsp_completion();
    render_integrated_terminal();
    render_telescope();
    render_status_line();
    ui->render();
    ui->hide_cursor();
    needs_redraw = false;
    return;
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
      render_lsp_completion();
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
        int display_x = 0;
        int display_y = 0;
        compute_code_cursor_screen_pos(pane, buf, show_minimap, minimap_width,
                                       tab_size,
                                       display_x, display_y);
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
  int draw_w = std::max(1, pane.w);
  if (pane.h <= 0)
    return;

  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  render_buffer_content(pane, pane.buffer_id);

  if (show_minimap && pane.w > 20) {
    render_minimap(pane.x + draw_w, pane.y + 1, minimap_width, pane.h - 1,
                   pane.buffer_id);
  }

  UIRect rect = {pane.x, pane.y, draw_w, pane.h};
  int border_fg = pane.active ? theme.fg_active_border : theme.fg_panel_border;
  int border_bg = pane.active ? theme.bg_active_border : theme.bg_panel_border;
  ui->draw_border(rect, border_fg, border_bg);
  render_scrollbar(pane, draw_w);
}

void Editor::render_scrollbar(const SplitPane &pane, int draw_w) {
  if (draw_w < 3) {
    return;
  }

  auto &buf = get_buffer(pane.buffer_id);

  const int track_x = pane.x + draw_w - 1;
  const int track_y = pane.y + tab_height;
  const int track_h = std::max(0, pane.h - tab_height - 1);
  if (track_h <= 0) {
    return;
  }

  const int total_lines = std::max(1, (int)buf.lines.size());
  const int visible_lines = std::max(1, track_h);
  const int max_scroll = std::max(0, total_lines - visible_lines);
  const int clamped_scroll = std::clamp(buf.scroll_offset, 0, max_scroll);

  int thumb_h = track_h;
  if (max_scroll > 0) {
    thumb_h = std::max(1, (visible_lines * visible_lines) / total_lines);
    thumb_h = std::min(track_h, thumb_h);
  }

  int thumb_y = track_y;
  if (max_scroll > 0 && track_h > thumb_h) {
    thumb_y = track_y + (clamped_scroll * (track_h - thumb_h)) / max_scroll;
  }

  for (int i = 0; i < track_h; i++) {
    ui->draw_text(track_x, track_y + i, "│", theme.fg_panel_border,
                  theme.bg_default);
  }

  const int thumb_fg = pane.active ? theme.fg_active_border : theme.fg_line_num;
  for (int i = 0; i < thumb_h; i++) {
    ui->draw_text(track_x, thumb_y + i, "█", thumb_fg, theme.bg_default);
  }
}
