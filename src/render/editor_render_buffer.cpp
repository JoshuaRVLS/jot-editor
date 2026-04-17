#include "editor.h"
#include <algorithm>
#include <cstdio>

void Editor::render_buffer_content(const SplitPane &pane, int buffer_id) {
  auto &buf = get_buffer(buffer_id);
  highlighter.set_language(get_file_extension(buf.filepath));
  int x = pane.x;
  int y = pane.y + tab_height;
  int w = std::max(1, pane.w);
  int h = std::max(0, pane.h - tab_height);
  if (h <= 0)
    return;

  UIRect pane_rect = {x, y, w, h};
  ui->fill_rect(pane_rect, " ", theme.fg_default, theme.bg_default);

  if (show_minimap && w > 20)
    w = std::max(1, w - minimap_width);

  int line_num_width = 6;

  for (int i = 0; i < h; i++) {
    int line_idx = i + buf.scroll_offset;
    int draw_y = y + i;

    if (line_idx < (int)buf.lines.size()) {
      char num_buf[16];
      snprintf(num_buf, sizeof(num_buf), "%4d ", line_idx + 1);
      int ln_bg = theme.bg_line_num;
      int ln_fg = theme.fg_line_num;
      if (line_idx == buf.cursor.y) {
        ln_fg = theme.fg_default;
      }
      ui->draw_text(x + 1, draw_y, num_buf, ln_fg, ln_bg);

      std::string &line = buf.lines[line_idx];
      int scroll_x = buf.scroll_x;

      if (scroll_x < (int)line.length()) {
        auto colors = highlighter.get_colors(line);

        int current_x = x + 1 + line_num_width;
        int visible_len = w - 2 - line_num_width;

        auto draw_chunk = [&](int start_idx, int len, int color) {
          if (len <= 0)
            return;

          int ch_start = start_idx;

          for (int k = 0; k < len; k++) {
            int char_idx = ch_start + k;
            if (char_idx < scroll_x)
              continue;
            int vis_idx = char_idx - scroll_x;
            if (vis_idx >= visible_len)
              break;

            int fg = color;
            int bg = theme.bg_default;

            if (buf.selection.active) {
              Cursor p = {char_idx, line_idx};
              Cursor s = buf.selection.start;
              Cursor e = buf.selection.end;

              if (s.y > e.y || (s.y == e.y && s.x > e.x))
                std::swap(s, e);

              bool in_sel = false;
              if (p.y > s.y && p.y < e.y)
                in_sel = true;
              else if (p.y == s.y && p.y == e.y) {
                if (p.x >= s.x && p.x < e.x)
                  in_sel = true;
              } else if (p.y == s.y) {
                if (p.x >= s.x)
                  in_sel = true;
              } else if (p.y == e.y) {
                if (p.x < e.x)
                  in_sel = true;
              }

              if (in_sel) {
                bg = theme.bg_selection;
                fg = theme.fg_selection;
              }
            }

            if (show_search && !search_query.empty()) {
              for (const auto &res : search_results) {
                if (res.first == line_idx) {
                  if (char_idx >= res.second &&
                      char_idx < res.second + (int)search_query.length()) {
                    bg = 3;
                    fg = 0;
                    break;
                  }
                } else if (res.first > line_idx) {
                  break;
                }
              }
            }

            char c = line[char_idx];
            ui->draw_text(current_x + vis_idx, draw_y, std::string(1, c), fg,
                          bg);
          }
        };

        if (colors.empty()) {
          draw_chunk(0, (int)line.length(), theme.fg_default);
        } else {
          int chunk_start = 0;
          int last_type = -1;
          int last_token = 0;

          for (int i = 0; i <= (int)line.length(); i++) {
            int current_type = -1;
            int current_token = 0;

            if (i < (int)line.length() && i < (int)colors.size()) {
              current_token = colors[i].first;
              current_type = colors[i].second;
            }

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
                  break;
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

  for (const auto &diag : buf.diagnostics) {
    int start_l = diag.line;
    int end_l = diag.end_line;

    if (end_l < buf.scroll_offset || start_l >= buf.scroll_offset + h)
      continue;

    for (int l = std::max(start_l, buf.scroll_offset);
         l <= std::min(end_l, buf.scroll_offset + h - 1); l++) {

      if (l < 0 || l >= (int)buf.lines.size())
        continue;

      int draw_y = y + (l - buf.scroll_offset);
      std::string &line = buf.lines[l];
      int line_len = line.length();

      int s_col = (l == start_l) ? diag.col : 0;
      int e_col = (l == end_l) ? diag.end_col : line_len;

      if (s_col > line_len)
        s_col = line_len;
      if (e_col > line_len)
        e_col = line_len;

      if (l == start_l) {
        int vt_dist = 4;
        int vis_x_end = line_len - buf.scroll_x;
        if (vis_x_end < 0)
          vis_x_end = 0;

        int cx = x + 1 + line_num_width + vis_x_end + vt_dist;

        if (cx < x + w) {
          std::string msg = "  " + diag.message;
          int msg_fg = 8;
          if (diag.severity == 1)
            msg_fg = 1;
          else if (diag.severity == 2)
            msg_fg = 3;

          ui->draw_text(cx, draw_y, msg, msg_fg, theme.bg_default);
        }
      }
    }
  }
}
