#include "bracket.h"
#include "editor.h"
#include <cstdio>
#include <filesystem>
#include <unordered_map>

namespace {
constexpr int kLineNumberGutterWidth = 7;
std::string ellipsize_right(const std::string &s, int max_len) {
  if (max_len <= 0) {
    return "";
  }
  if ((int)s.size() <= max_len) {
    return s;
  }
  if (max_len <= 3) {
    return s.substr(0, (size_t)max_len);
  }
  return s.substr(0, (size_t)(max_len - 3)) + "...";
}

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
                                    int tab_size, int tab_height,
                                    int &display_x, int &display_y) {
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int code_start_x = pane.x + 1 + kLineNumberGutterWidth;
  const int code_end_x = pane.x + draw_w - 2;
  const int min_y = pane.y + tab_height;
  int max_y = pane.y + pane.h - 1;

  display_y = buf.cursor.y - buf.scroll_offset + pane.y + tab_height;

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
        show_quit_prompt || input_prompt_visible) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      } else if (input_prompt_visible) {
        int prompt_w = 40;
        int prompt_x = ui->get_width() / 2 - prompt_w / 2;
        int prompt_y = ui->get_height() / 4;
        int cursor_x =
            prompt_x + 1 + (int)input_prompt_message.length() +
            (int)input_prompt_buffer.length();
        cursor_x = std::clamp(cursor_x, 0, std::max(0, ui->get_width() - 1));
        int cursor_y =
            std::clamp(prompt_y + 1, 0, std::max(0, ui->get_height() - 1));
        ui->set_cursor(cursor_x, cursor_y);
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
                                     tab_size, tab_height,
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
    render_input_prompt();
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
        show_quit_prompt || input_prompt_visible) {
      if (show_command_palette) {
        int x = std::min(ui->get_width() - 1,
                         std::max(1, (int)command_palette_query.length() + 1));
        int y = ui->get_height() - 1;
        ui->set_cursor(x, y);
      } else if (input_prompt_visible) {
        int prompt_w = 40;
        int prompt_x = ui->get_width() / 2 - prompt_w / 2;
        int prompt_y = ui->get_height() / 4;
        int cursor_x =
            prompt_x + 1 + (int)input_prompt_message.length() +
            (int)input_prompt_buffer.length();
        cursor_x = std::clamp(cursor_x, 0, std::max(0, ui->get_width() - 1));
        int cursor_y =
            std::clamp(prompt_y + 1, 0, std::max(0, ui->get_height() - 1));
        ui->set_cursor(cursor_x, cursor_y);
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
                                       tab_size, tab_height,
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

  // Pane-local file tabs.
  {
    int tabs_x = pane.x + 1;
    int tabs_y = pane.y;
    int tabs_w = std::max(1, draw_w - 2);
    UIRect tab_rect = {tabs_x, tabs_y, tabs_w, 1};
    ui->fill_rect(tab_rect, " ", theme.fg_status, theme.bg_status);

    std::vector<int> tab_ids = pane.tab_buffer_ids;
    if (tab_ids.empty() && pane.buffer_id >= 0 &&
        pane.buffer_id < (int)buffers.size()) {
      tab_ids.push_back(pane.buffer_id);
    }
    std::vector<std::string> base_names(buffers.size());
    std::unordered_map<std::string, int> base_count;
    for (int i = 0; i < (int)tab_ids.size(); i++) {
      int buf_id = tab_ids[i];
      if (buf_id < 0 || buf_id >= (int)buffers.size()) {
        continue;
      }
      std::string base = get_filename(buffers[buf_id].filepath);
      if (base.empty()) {
        base = "[No Name]";
      }
      base_names[buf_id] = base;
      base_count[base]++;
    }

    int tab_x = tabs_x;
    for (int tab_i = 0; tab_i < (int)tab_ids.size(); tab_i++) {
      int i = tab_ids[tab_i];
      if (i < 0 || i >= (int)buffers.size()) {
        continue;
      }
      std::string name = base_names[i];
      if (base_count[name] > 1 && !buffers[i].filepath.empty()) {
        std::filesystem::path p(buffers[i].filepath);
        std::string parent = p.parent_path().filename().string();
        if (!parent.empty()) {
          name += " <" + parent + ">";
        }
      }

      std::string label = " " + name + (buffers[i].modified ? " *" : "");
      int max_tab_w = std::max(6, tabs_w / 2);
      label = ellipsize_right(label, max_tab_w);
      int need = (int)label.size() + 2; // close button + separator
      if (tab_x + need > tabs_x + tabs_w) {
        break;
      }

      bool active_tab = (i == pane.buffer_id);
      int fg = active_tab ? theme.fg_tab_active : theme.fg_tab_inactive;
      int bg = active_tab ? theme.bg_tab_active : theme.bg_tab_inactive;
      ui->draw_text(tab_x, tabs_y, label, fg, bg, active_tab,
                    buffers[i].is_preview);
      tab_x += (int)label.size();
      ui->draw_text(tab_x, tabs_y, "x", theme.fg_tab_close, bg);
      tab_x += 1;
      ui->draw_text(tab_x, tabs_y, "|", theme.fg_tab_separator, theme.bg_status);
      tab_x += 1;
    }
  }

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
