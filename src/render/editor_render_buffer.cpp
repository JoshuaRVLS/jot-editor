#include "editor.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace {
int diagnostic_severity_color(const Theme &theme, int severity) {
  switch (severity) {
  case 1:
    return 1;
  case 2:
    return 3;
  case 3:
    return 6;
  case 4:
    return 2;
  default:
    return theme.fg_comment;
  }
}

char diagnostic_severity_marker(int severity) {
  switch (severity) {
  case 1:
    return 'E';
  case 2:
    return 'W';
  case 3:
    return 'I';
  case 4:
    return 'H';
  default:
    return '!';
  }
}

std::string diagnostic_severity_label(int severity) {
  switch (severity) {
  case 1:
    return "Error";
  case 2:
    return "Warning";
  case 3:
    return "Info";
  case 4:
    return "Hint";
  default:
    return "Diagnostic";
  }
}

bool diagnostic_covers_line(const Diagnostic &diag, int line) {
  return line >= diag.line && line <= diag.end_line;
}

const Diagnostic *find_line_diagnostic(const FileBuffer &buf, int line,
                                       int cursor_col) {
  const Diagnostic *best = nullptr;
  for (const auto &diag : buf.diagnostics) {
    if (!diagnostic_covers_line(diag, line)) {
      continue;
    }

    bool contains_cursor = false;
    if (line == diag.line && line == diag.end_line) {
      contains_cursor = cursor_col >= diag.col && cursor_col <= diag.end_col;
    } else if (line == diag.line) {
      contains_cursor = cursor_col >= diag.col;
    } else if (line == diag.end_line) {
      contains_cursor = cursor_col <= diag.end_col;
    } else {
      contains_cursor = true;
    }

    if (contains_cursor) {
      if (!best || diag.severity < best->severity) {
        best = &diag;
      }
      continue;
    }

    if (!best) {
      best = &diag;
    }
  }
  return best;
}

int line_diagnostic_severity(const FileBuffer &buf, int line) {
  int best = 0;
  for (const auto &diag : buf.diagnostics) {
    if (!diagnostic_covers_line(diag, line)) {
      continue;
    }
    if (best == 0 || diag.severity < best) {
      best = diag.severity;
    }
  }
  return best;
}

std::vector<std::string> wrap_diagnostic_text(const std::string &text,
                                              int max_width) {
  std::vector<std::string> lines;
  if (max_width <= 0) {
    return lines;
  }

  std::istringstream input(text);
  std::string source_line;
  while (std::getline(input, source_line)) {
    if (source_line.empty()) {
      lines.push_back("");
      continue;
    }

    std::istringstream words(source_line);
    std::string word;
    std::string current;
    while (words >> word) {
      if ((int)word.size() > max_width) {
        if (!current.empty()) {
          lines.push_back(current);
          current.clear();
        }
        for (size_t i = 0; i < word.size(); i += (size_t)max_width) {
          lines.push_back(word.substr(i, (size_t)max_width));
        }
        continue;
      }

      if (current.empty()) {
        current = word;
      } else if ((int)current.size() + 1 + (int)word.size() <= max_width) {
        current += " " + word;
      } else {
        lines.push_back(current);
        current = word;
      }
    }

    if (!current.empty()) {
      lines.push_back(current);
    }
  }

  if (lines.empty()) {
    lines.push_back(text.substr(0, (size_t)max_width));
  }

  return lines;
}
} // namespace

void Editor::render_buffer_content(const SplitPane &pane, int buffer_id) {
  auto &buf = get_buffer(buffer_id);
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

  int line_num_width = 7;

  for (int i = 0; i < h; i++) {
    int line_idx = i + buf.scroll_offset;
    int draw_y = y + i;

    if (line_idx < (int)buf.lines.size()) {
      int line_diag_severity = line_diagnostic_severity(buf, line_idx);
      char diag_marker = line_diag_severity > 0
                             ? diagnostic_severity_marker(line_diag_severity)
                             : ' ';
      int diag_fg = line_diag_severity > 0
                        ? diagnostic_severity_color(theme, line_diag_severity)
                        : theme.fg_line_num;

      ui->draw_text(x + 1, draw_y, std::string(1, diag_marker), diag_fg,
                    theme.bg_default, line_diag_severity > 0);

      char num_buf[16];
      snprintf(num_buf, sizeof(num_buf), "%4d ", line_idx + 1);
      int ln_bg = theme.bg_line_num;
      int ln_fg = theme.fg_line_num;
      if (line_idx == buf.cursor.y) {
        ln_fg = theme.fg_default;
      } else if (line_diag_severity > 0) {
        ln_fg = diag_fg;
      }
      ui->draw_text(x + 2, draw_y, num_buf, ln_fg, ln_bg);

      std::string &line = buf.lines[line_idx];
      int scroll_x = buf.scroll_x;
      int current_x = x + 1 + line_num_width;
      int visible_len = w - 2 - line_num_width;
      int leading_ws_end = 0;
      while (leading_ws_end < (int)line.length() &&
             (line[leading_ws_end] == ' ' || line[leading_ws_end] == '\t')) {
        leading_ws_end++;
      }

      auto is_in_selection = [&](int char_idx) {
        if (!buf.selection.active)
          return false;

        Cursor p = {char_idx, line_idx};
        Cursor s = buf.selection.start;
        Cursor e = buf.selection.end;

        if (s.y > e.y || (s.y == e.y && s.x > e.x))
          std::swap(s, e);

        if (p.y > s.y && p.y < e.y)
          return true;
        if (p.y == s.y && p.y == e.y)
          return (p.x >= s.x && p.x < e.x);
        if (p.y == s.y)
          return (p.x >= s.x);
        if (p.y == e.y)
          return (p.x < e.x);
        return false;
      };

      if (scroll_x < (int)line.length()) {
        const auto &colors = get_line_syntax_colors(buf, line_idx);
        std::vector<int> search_hit_columns;
        if (show_search && !search_query.empty()) {
          auto it = std::lower_bound(search_results.begin(), search_results.end(),
                                     std::make_pair(line_idx, 0));
          while (it != search_results.end() && it->first == line_idx) {
            search_hit_columns.push_back(it->second);
            ++it;
          }
        }
        size_t next_search_hit = 0;

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

            bool in_sel = is_in_selection(char_idx);
            if (in_sel) {
              bg = theme.bg_selection;
              fg = theme.fg_selection;
            }

            if (!search_hit_columns.empty()) {
              while (next_search_hit < search_hit_columns.size() &&
                     char_idx >=
                         search_hit_columns[next_search_hit] +
                             (int)search_query.length()) {
                next_search_hit++;
              }
              if (next_search_hit < search_hit_columns.size() &&
                  char_idx >= search_hit_columns[next_search_hit] &&
                  char_idx < search_hit_columns[next_search_hit] +
                                 (int)search_query.length()) {
                bg = 3;
                fg = 0;
              }
            }

            char c = line[char_idx];
            bool in_leading_indent = char_idx < leading_ws_end;
            bool indent_guide_col =
                (c == '\t') ||
                (c == ' ' && tab_size > 0 && ((char_idx + 1) % tab_size == 0));

            if (in_leading_indent && indent_guide_col) {
              int guide_fg = in_sel ? theme.fg_selection : theme.fg_line_num;
              ui->draw_text(current_x + vis_idx, draw_y, "|", guide_fg, bg);
              continue;
            }

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

  if (pane.active) {
    const Diagnostic *active_diag =
        find_line_diagnostic(buf, buf.cursor.y, buf.cursor.x);
    if (active_diag && diagnostic_covers_line(*active_diag, buf.cursor.y) &&
        buf.cursor.y >= buf.scroll_offset &&
        buf.cursor.y < buf.scroll_offset + h) {
      const int severity_fg =
          diagnostic_severity_color(theme, active_diag->severity);
      const std::string header =
          diagnostic_severity_label(active_diag->severity) + "  Ln " +
          std::to_string(active_diag->line + 1) + ":" +
          std::to_string(active_diag->col + 1);

      int max_box_width = std::max(24, std::min(w - 4, 56));
      std::vector<std::string> body_lines =
          wrap_diagnostic_text(active_diag->message, max_box_width - 4);
      if (body_lines.size() > 4) {
        body_lines.resize(4);
      }

      int content_width = (int)header.size();
      for (const auto &line : body_lines) {
        content_width = std::max(content_width, (int)line.size());
      }

      int box_w = std::min(w - 2, content_width + 4);
      int box_h = std::min(h, (int)body_lines.size() + 3);
      if (box_w >= 8 && box_h >= 3) {
        int cursor_draw_y = y + (buf.cursor.y - buf.scroll_offset);
        int box_x = x + w - box_w - 1;
        int box_y = cursor_draw_y + 1;
        if (box_y + box_h > y + h) {
          box_y = cursor_draw_y - box_h;
        }
        box_y = std::max(y, std::min(box_y, y + h - box_h));

        UIRect shadow = {box_x + 1, box_y + 1, box_w, box_h};
        UIRect rect = {box_x, box_y, box_w, box_h};
        ui->draw_rect(shadow, 8, 0);
        ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
        ui->draw_border(rect, severity_fg, theme.bg_command);
        ui->draw_text(box_x + 2, box_y + 1, header, severity_fg,
                      theme.bg_command, true);

        int body_limit = box_h - 2;
        for (int i = 0; i < (int)body_lines.size() && i < body_limit - 1; i++) {
          ui->draw_text(box_x + 2, box_y + 2 + i, body_lines[i],
                        theme.fg_command, theme.bg_command);
        }
      }
    }
  }
}
