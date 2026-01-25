#include "bracket.h"
#include "editor.h"
#include <cstdio>
#include <sstream>

void Editor::render() {
  if (!needs_redraw) {
    if (!show_command_palette && !show_search) {
      if (!panes.empty()) {
        auto &pane = get_pane();
        auto &buf = get_buffer(pane.buffer_id);
        int display_y = buf.cursor.y - buf.scroll_offset + pane.y + 1;
        int display_x = buf.cursor.x - buf.scroll_x + pane.x + 7;
        if (display_y >= pane.y && display_y < pane.y + pane.h &&
            display_x >= pane.x && display_x < pane.x + pane.w) {
          ui->set_cursor(display_x, display_y);
        }
      }
    }
    return;
  }

  ui->clear();
  int h = ui->get_height();
  int w = ui->get_width();

  // Render Tabs
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
        int editor_x = sidebar_width;
        int editor_w = ui->get_width() - sidebar_width;

        render_sidebar();

        // Adjust panes
        if (!panes.empty()) {
          panes[0].x = editor_x;
          panes[0].w = editor_w;
        }
      } else {
        // Reset pane layout
        if (!panes.empty()) {
          panes[0].x = 0;
          panes[0].w = ui->get_width();
        }
      }

      render_panes();

      if (show_minimap && !panes.empty()) {
        render_minimap(
            ui->get_width() - minimap_width, tab_height, minimap_width,
            ui->get_height() - tab_height - status_height, panes[0].buffer_id);
      }
    }

    render_status_line();
    render_command_palette();
    render_search_panel();
    render_popup();
    render_context_menu();

    // Final render to terminal
    ui->render();

    // Cursor Handling
    if (show_command_palette || show_search || show_save_prompt ||
        show_quit_prompt) {
      // Leave cursor handling to prompts or default
    } else if (show_sidebar && focus_state == FOCUS_SIDEBAR) {
      ui->hide_cursor();
    } else if (!telescope.is_active()) {
      // Editor cursor
      if (!panes.empty()) {
        auto &pane = get_pane();
        auto &buf = get_buffer(pane.buffer_id);
        int display_y = buf.cursor.y - buf.scroll_offset + pane.y + 1;
        int display_x = buf.cursor.x - buf.scroll_x + pane.x + 7;
        if (display_y >= pane.y && display_y < pane.y + pane.h &&
            display_x >= pane.x && display_x < pane.x + pane.w) {
          ui->set_cursor(display_x, display_y);
        } else {
          ui->set_cursor(
              display_x,
              display_y); // Should we hide? Actually original code unsets it
          // But here in main render loop we should just hide if OOB
          ui->hide_cursor();
        }
      }
    }

    needs_redraw = false;
  }
}

void Editor::render_telescope() {
  int h = ui->get_height();
  int w = ui->get_width();

  int list_w = w / 2;
  int preview_w = w - list_w;
  int list_h = h - 2;

  UIRect rect = {0, 0, w, h};
  ui->fill_rect(rect, " ", theme.fg_telescope, theme.bg_telescope);

  ui->draw_text(1, 0, " FIND FILES ", theme.fg_telescope, theme.bg_telescope);

  std::string query = telescope.get_query();
  ui->draw_text(1, 1, "> " + query, theme.fg_telescope, theme.bg_telescope);

  const auto &results = telescope.get_results();
  int selected = telescope.get_selected_index();
  int start_idx = std::max(0, selected - (list_h - 3));
  int end_idx = std::min((int)results.size(), start_idx + list_h - 2);

  for (int i = start_idx; i < end_idx; i++) {
    int y = 2 + (i - start_idx);
    int fg = theme.fg_telescope, bg = theme.bg_telescope;

    if (i == selected) {
      fg = theme.fg_telescope_selected;
      bg = theme.bg_telescope_selected;
    }

    std::string icon = results[i].is_directory ? "D " : "F ";
    std::string name = results[i].name;
    if ((int)name.length() > list_w - 5) {
      name = name.substr(0, list_w - 8) + "...";
    }

    ui->draw_text(1, y, icon + name, fg, bg);
  }

  if (!results.empty() && selected >= 0 && selected < (int)results.size()) {
    auto preview_lines = telescope.get_preview_lines();
    int preview_x = list_w;
    int preview_h = h - 2;

    ui->draw_text(preview_x, 0, " PREVIEW ", theme.fg_telescope,
                  theme.bg_telescope);

    std::string path = telescope.get_selected_path();
    if (!path.empty()) {
      std::string path_display = path;
      if ((int)path_display.length() > preview_w - 2) {
        path_display =
            "..." + path_display.substr(path_display.length() - preview_w + 5);
      }
      ui->draw_text(preview_x + 1, 1, path_display, theme.fg_telescope_preview,
                    theme.bg_telescope_preview);

      for (size_t i = 0;
           i < preview_lines.size() && i < (size_t)(preview_h - 2); i++) {
        std::string line = preview_lines[i];
        if ((int)line.length() > preview_w - 2) {
          line = line.substr(0, preview_w - 5) + "...";
        }
        ui->draw_text(preview_x + 1, 2 + (int)i, line,
                      theme.fg_telescope_preview, theme.bg_telescope_preview);
      }
    }
  }
}

void Editor::render_image_viewer() {
  int w = ui->get_width();
  int h = ui->get_height();
  int img_w = w / 2;
  int img_h = h - status_height - tab_height;

  image_viewer.render(w - img_w, tab_height, img_w, img_h,
                      theme.fg_image_border, theme.bg_image_border);
}

void Editor::render_minimap(int x, int y, int w, int h, int buffer_id) {
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
    return;
  auto &buf = buffers[buffer_id];

  // Draw background
  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", 7, theme.bg_minimap);

  // Simple compressed view
  // Map total lines to h
  int total_lines = buf.lines.size();
  if (total_lines == 0)
    return;

  // Viewport indicator
  auto &pane = get_pane();
  float ratio = (float)h / total_lines;

  int viewport_y = (int)(buf.scroll_offset * ratio);
  int viewport_h = (int)(pane.h * ratio);
  if (viewport_h < 1)
    viewport_h = 1;

  UIRect viewport = {x, y + viewport_y, w, viewport_h};
  ui->fill_rect(viewport, " ", 7, theme.bg_selection);

  // Draw content (simplified blocks)
  for (int i = 0; i < h; i++) {
    int line_idx = (int)(i / ratio);
    if (line_idx < total_lines) {
      std::string line = buf.lines[line_idx];
      // Simply draw a few dots based on line length
      int dots = line.length() / 4;
      if (dots > w - 2)
        dots = w - 2;
      std::string miniline(dots, '.');
      ui->draw_text(x + 1, y + i, miniline, 8, -1);
    }
  }
}

void Editor::render_panes() {
  for (const auto &pane : panes) {
    render_pane(pane);
  }
}

void Editor::render_pane(const SplitPane &pane) {
  // Calculate geometry
  int w = pane.w;

  if (show_minimap) {
    w -= minimap_width;
  }

  render_buffer_content(pane, pane.buffer_id);

  // Border
  UIRect rect = {pane.x, pane.y, w, pane.h};
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_panel_border);
}

void Editor::render_buffer_content(const SplitPane &pane, int buffer_id) {
  auto &buf = get_buffer(buffer_id);
  int x = pane.x;
  int y = pane.y + tab_height;
  int w = pane.w;
  int h = pane.h - tab_height;

  // Fill pane background first
  UIRect pane_rect = {x, y, w, h};
  // We can fill the whole pane first to ensure no artifacts
  ui->fill_rect(pane_rect, " ", theme.fg_default, theme.bg_default);

  if (show_minimap)
    w -= minimap_width;

  int line_num_width = 6;

  for (int i = 0; i < h; i++) {
    int line_idx = i + buf.scroll_offset;

    // Calculate drawing y position
    int draw_y = y + i;

    // Clear the specific line area again just to be safe or rely on pane fill?
    // Pane fill is safer for "empty" space.
    // But let's define the line rect for text drawing context
    UIRect line_rect = {x, draw_y, w, 1};
    // ui->fill_rect(line_rect, " ", theme.fg_default, theme.bg_default);

    if (line_idx < (int)buf.lines.size()) {
      // Line number
      char num_buf[16];
      snprintf(num_buf, sizeof(num_buf), "%4d ", line_idx + 1);
      int ln_bg = theme.bg_line_num;
      int ln_fg = theme.fg_line_num;
      if (line_idx == buf.cursor.y) {
        ln_fg = theme.fg_default;
      }
      ui->draw_text(x + 1, draw_y, num_buf, ln_fg, ln_bg);

      // Content
      std::string &line = buf.lines[line_idx];
      int scroll_x = buf.scroll_x;

      if (scroll_x < (int)line.length()) {
        auto colors = highlighter.get_colors(line);

        int current_x = x + 1 + line_num_width;
        int visible_len = w - 2 - line_num_width;

        // Helper for rendering
        auto draw_chunk = [&](int start_idx, int len, int color) {
          if (len <= 0)
            return;

          // Selection logic
          int ch_start = start_idx;
          int ch_end = start_idx + len;

          for (int k = 0; k < len; k++) {
            int char_idx = ch_start + k;
            if (char_idx < scroll_x)
              continue;
            int vis_idx = char_idx - scroll_x;
            if (vis_idx >= visible_len)
              break;

            int fg = color;
            int bg = theme.bg_default;

            // Check selection
            if (buf.selection.active) {
              // Assuming single line selection for simplicity first or
              // multi-line Need full selection logic
              Cursor p = {char_idx, line_idx};

              // Order start/end
              Cursor s = buf.selection.start;
              Cursor e = buf.selection.end;

              if (s.y > e.y || (s.y == e.y && s.x > e.x))
                std::swap(s, e);

              bool in_sel = false;
              if (p.y > s.y && p.y < e.y)
                in_sel = true;
              else if (p.y == s.y && p.y == e.y) {
                if (p.x >= s.x && p.x <= e.x)
                  in_sel = true;
              } else if (p.y == s.y) {
                if (p.x >= s.x)
                  in_sel = true;
              } else if (p.y == e.y) {
                if (p.x <= e.x)
                  in_sel = true;
              }

              if (in_sel) {
                bg = theme.bg_selection;
                fg = theme.fg_selection;
              }
            }

            char c = line[char_idx];
            ui->draw_text(current_x + vis_idx, draw_y, std::string(1, c), fg,
                          bg);
          }
        };

        if (colors.empty()) {
          // No syntax highlighting (or failed), draw as default
          draw_chunk(0, (int)line.length(), theme.fg_default);
        } else {
          // Iterate per-character colors and batch them into chunks
          int chunk_start = 0;
          int last_type = -1; // -1 = default, otherwise color ID
          int last_token = 0; // 0 = no token, 1 = token

          for (int i = 0; i <= (int)line.length(); i++) {
            int current_type = -1;
            int current_token = 0;

            if (i < (int)line.length() && i < (int)colors.size()) {
              current_token = colors[i].first;
              current_type = colors[i].second;
            }

            // If color changed or end of line, draw previous chunk
            bool changed = (current_token != last_token) ||
                           (current_token == 1 && current_type != last_type);

            if (i > 0 && (changed || i == (int)line.length())) {
              int len = i - chunk_start;
              int color = theme.fg_default;

              if (last_token == 1) {
                switch (last_type) {
                case 1:
                  color = theme.fg_keyword;
                  break;
                case 2:
                  color = theme.fg_string;
                  break;
                case 3:
                  color = theme.fg_comment;
                  break;
                case 4:
                  color = theme.fg_number;
                  break;
                case 5:
                  color = theme.fg_type;
                  break; // Map 5 to Type/Function? Need strict map
                case 6:
                  color = theme.fg_function;
                  break;
                default:
                  color = theme.fg_default;
                  break;
                }
              }

              draw_chunk(chunk_start, len, color);
              chunk_start = i;
            }

            last_token = current_token;
            last_type = current_type;
          }
        }
      }
    } else {
      ui->draw_text(x + 1, draw_y, "~", theme.fg_line_num, theme.bg_default);
    }
  }
}
