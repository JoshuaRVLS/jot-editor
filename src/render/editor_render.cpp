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
        render_sidebar();
      }

      render_panes();
    }

    render_status_line();
    render_command_palette();
    render_search_panel();
    render_popup();
    render_context_menu();

    // Easter egg overlay (on top of everything)
    if (easter_egg_timer > 0) {
      render_easter_egg();
      easter_egg_timer--;
      needs_redraw = true; // keep animating until timer expires
    }

    // Final render to terminal
    ui->render();

    // Cursor shape: only update when mode actually changes to avoid jitter.
    int target_cursor_shape = (mode == MODE_INSERT) ? 1 : 0;
    if (target_cursor_shape != last_cursor_shape) {
      if (mode == MODE_INSERT) {
        printf("\033[5 q"); // Blinking bar cursor
      } else {
        printf("\033[1 q"); // Steady block cursor
      }
      fflush(stdout);
      last_cursor_shape = target_cursor_shape;
    }

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
          ui->set_cursor(display_x, display_y);
          ui->hide_cursor();
        }
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
  // Calculate geometry
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

  // Border
  UIRect rect = {pane.x, pane.y, w, pane.h};
  int border_fg = pane.active ? theme.fg_bracket_match : theme.fg_panel_border;
  ui->draw_border(rect, border_fg, theme.bg_panel_border);
}

void Editor::render_buffer_content(const SplitPane &pane, int buffer_id) {
  auto &buf = get_buffer(buffer_id);
  highlighter.set_language(get_file_extension(buf.filepath));
  int x = pane.x;
  int y = pane.y + tab_height;
  int w = std::max(1, pane.w);
  int h = std::max(0, pane.h - tab_height);
  if (h <= 0)
    return;

  // Fill pane background first
  UIRect pane_rect = {x, y, w, h};
  // We can fill the whole pane first to ensure no artifacts
  ui->fill_rect(pane_rect, " ", theme.fg_default, theme.bg_default);

  if (show_minimap && w > 20)
    w = std::max(1, w - minimap_width);

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
              // ... selection logic ...
              Cursor p = {char_idx, line_idx};
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

            // Check Search Highlights
            if (show_search && !search_query.empty()) {
              // Determine if char_idx is within a search match on this line
              // This is slightly inefficient (nested loop), but for visible
              // chars ok Better: Pre-calculate matches for this line? But we
              // have global search_results. Let's just check if (line_idx,
              // char_idx) is in search_results Actually search_results stores
              // (line, start_col). We need to check if char_idx >= start_col &&
              // char_idx < start_col + query_len

              // Optimize: binary search finding matches on this line?
              // Or just iterate linear if count is low?
              for (const auto &res : search_results) {
                if (res.first == line_idx) {
                  if (char_idx >= res.second &&
                      char_idx < res.second + (int)search_query.length()) {
                    bg = 3; // Yellow background for search
                    fg = 0; // Black text
                    break;
                  }
                } else if (res.first > line_idx) {
                  break; // Sorted by line usually
                }
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

  // Render Diagnostics (Underline)
  // We do this after text so it overlays or we could do it during text render
  // Doing it after is easier to manage overlapping logic, though TUI text cells
  // only hold FG/BG. We can't "underline" easily in TUI unless we have a
  // specific attribute. Our UI class `draw_text` takes FG/BG. Only some
  // terminals support underline. Alternatively, we can change the BG color of
  // the error text or the FG color to Red. Let's iterate diagnostics and
  // override colors if we haven't drawn them yet? But we already drew text.
  // Let's redraw text for diagnostics? Inefficient but works.
  // Better: Check diagnostics loop INSIDE the text render loop.
  // Wait, I can't modify the previous massive loop easily with
  // `replace_file_content`. Let's implement a "post-render" pass for
  // diagnostics that just redraws the affect characters with red color?

  // Render Diagnostics (Background Highlight)
  for (const auto &diag : buf.diagnostics) {
    // Only single line highlighting effectively implemented for now
    // If multi-line, we clip to current line logic or expand logic
    // Let's iterate over the lines affected by diagnostic that are visible

    int start_l = diag.line;
    int end_l = diag.end_line;

    // Bounds check visibility
    if (end_l < buf.scroll_offset || start_l >= buf.scroll_offset + h)
      continue;

    // Iterate visible lines involved in this diagnostic
    for (int l = std::max(start_l, buf.scroll_offset);
         l <= std::min(end_l, buf.scroll_offset + h - 1); l++) {

      if (l < 0 || l >= (int)buf.lines.size())
        continue;

      int draw_y = y + (l - buf.scroll_offset);
      std::string &line = buf.lines[l];
      int line_len = line.length();

      int s_col = (l == start_l) ? diag.col : 0;
      int e_col = (l == end_l) ? diag.end_col : line_len; // Exclusive

      // Clip to line length
      if (s_col > line_len)
        s_col = line_len;
      if (e_col > line_len)
        e_col = line_len;

      // Virtual Text (Gray message at end of line)
      if (l == start_l) {
        int vt_dist = 4; // Padding
        int vis_x_end = line_len - buf.scroll_x;
        if (vis_x_end < 0)
          vis_x_end = 0;

        int cx = x + 1 + line_num_width + vis_x_end + vt_dist;

        // Check if we have space (soft check)
        if (cx < x + w) {
          std::string msg = "  " + diag.message;
          int msg_fg = 8; // Gray
          if (diag.severity == 1)
            msg_fg = 1; // Red
          else if (diag.severity == 2)
            msg_fg = 3; // Yellow

          // Draw
          ui->draw_text(cx, draw_y, msg, msg_fg, theme.bg_default);
        }
      }
    }
  }
}
