#include "editor.h"
#include <algorithm>
#include <cstdio>
#include <sstream>


namespace {
constexpr int kBracketDepthScanLimitLines = 500;
constexpr int kBracketMatchSearchLimitLines = 5000;

struct ActiveBracketGuide {
  bool active = false;
  int visual_column = 0;
  int start_line = 0;
  int end_line = 0;
};

struct BracketPairMatch {
  bool found = false;
  int open_line = -1;
  int open_col = -1;
  int close_line = -1;
  int close_col = -1;
};

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

int diagnostic_severity_color(const Theme &theme, int severity) {
  switch (severity) {
  case 1:
    return theme.fg_diagnostic_error;
  case 2:
    return theme.fg_diagnostic_warning;
  case 3:
    return theme.fg_diagnostic_info;
  case 4:
    return theme.fg_diagnostic_hint;
  default:
    return theme.fg_comment;
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

bool is_open_bracket(char c) { return c == '(' || c == '[' || c == '{'; }

bool is_close_bracket(char c) { return c == ')' || c == ']' || c == '}'; }

int rainbow_bracket_color(const Theme &theme, int depth) {
  static const int kPaletteSize = 6;
  int normalized = depth % kPaletteSize;
  if (normalized < 0)
    normalized += kPaletteSize;
  switch (normalized) {
  case 0:
    return theme.fg_bracket1;
  case 1:
    return theme.fg_bracket2;
  case 2:
    return theme.fg_bracket3;
  case 3:
    return theme.fg_bracket4;
  case 4:
    return theme.fg_bracket5;
  default:
    return theme.fg_bracket6;
  }
}

void apply_bracket_depth_delta(char c, int &depth) {
  if (is_open_bracket(c)) {
    depth++;
  } else if (is_close_bracket(c)) {
    depth = std::max(0, depth - 1);
  }
}

bool bracket_chars(char c, char &open, char &close, bool &is_open) {
  switch (c) {
  case '(':
    open = '(';
    close = ')';
    is_open = true;
    return true;
  case ')':
    open = '(';
    close = ')';
    is_open = false;
    return true;
  case '[':
    open = '[';
    close = ']';
    is_open = true;
    return true;
  case ']':
    open = '[';
    close = ']';
    is_open = false;
    return true;
  case '{':
    open = '{';
    close = '}';
    is_open = true;
    return true;
  case '}':
    open = '{';
    close = '}';
    is_open = false;
    return true;
  default:
    return false;
  }
}

BracketPairMatch find_pair_at(const std::vector<std::string> &lines, int line,
                              int col) {
  BracketPairMatch result;
  if (line < 0 || line >= (int)lines.size())
    return result;
  if (col < 0 || col >= (int)lines[line].size())
    return result;

  char open = 0, close = 0;
  bool is_open = false;
  if (!bracket_chars(lines[line][col], open, close, is_open)) {
    return result;
  }

  if (is_open) {
    int depth = 1;
    const int max_line = std::min((int)lines.size() - 1,
                                  line + kBracketMatchSearchLimitLines);
    for (int y = line; y <= max_line; y++) {
      int start_x = (y == line) ? col + 1 : 0;
      for (int x = start_x; x < (int)lines[y].size(); x++) {
        char ch = lines[y][x];
        if (ch == open) {
          depth++;
        } else if (ch == close) {
          depth--;
          if (depth == 0) {
            result.found = true;
            result.open_line = line;
            result.open_col = col;
            result.close_line = y;
            result.close_col = x;
            return result;
          }
        }
      }
    }
  } else {
    int depth = 1;
    const int min_line = std::max(0, line - kBracketMatchSearchLimitLines);
    for (int y = line; y >= min_line; y--) {
      int start_x = (y == line) ? col - 1 : (int)lines[y].size() - 1;
      for (int x = start_x; x >= 0; x--) {
        char ch = lines[y][x];
        if (ch == close) {
          depth++;
        } else if (ch == open) {
          depth--;
          if (depth == 0) {
            result.found = true;
            result.open_line = y;
            result.open_col = x;
            result.close_line = line;
            result.close_col = col;
            return result;
          }
        }
      }
    }
  }

  return result;
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

std::string compact_diagnostic_text(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  bool last_space = false;
  for (char ch : text) {
    const bool is_space = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    if (is_space) {
      if (!last_space) {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    out.push_back(ch);
    last_space = false;
  }

  while (!out.empty() && out.front() == ' ') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

ActiveBracketGuide build_active_bracket_guide(const FileBuffer &buf,
                                              int tab_size) {
  ActiveBracketGuide guide;
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size()) {
    return guide;
  }

  const std::string &line = buf.lines[buf.cursor.y];
  int candidates[3] = {buf.cursor.x, buf.cursor.x - 1, buf.cursor.x + 1};
  for (int c : candidates) {
    if (c < 0 || c >= (int)line.size()) {
      continue;
    }
    if (!is_open_bracket(line[c]) && !is_close_bracket(line[c])) {
      continue;
    }

    BracketPairMatch pair = find_pair_at(buf.lines, buf.cursor.y, c);
    if (!pair.found) {
      continue;
    }

    int top = std::min(pair.open_line, pair.close_line);
    int bottom = std::max(pair.open_line, pair.close_line);
    if (bottom - top < 2) {
      continue; // No inner rows to draw a connector on
    }

    guide.active = true;
    const std::string &open_line = buf.lines[pair.open_line];
    const std::string &close_line = buf.lines[pair.close_line];
    int open_visual = compute_visual_column(open_line, pair.open_col, tab_size);
    int close_visual =
        compute_visual_column(close_line, pair.close_col, tab_size);
    guide.visual_column = std::min(open_visual, close_visual);
    guide.start_line = top;
    guide.end_line = bottom;
    return guide;
  }

  return guide;
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
  ActiveBracketGuide bracket_guide = build_active_bracket_guide(buf, tab_size);
  int bracket_depth = 0;
  const int scan_start =
      std::max(0, buf.scroll_offset - kBracketDepthScanLimitLines);
  for (int scan_line = scan_start;
       scan_line < std::min(buf.scroll_offset, (int)buf.lines.size());
       scan_line++) {
    const std::string &line = buf.lines[scan_line];
    for (char c : line) {
      apply_bracket_depth_delta(c, bracket_depth);
    }
  }

  std::vector<int> visual_cols;

  for (int i = 0; i < h; i++) {
    int line_idx = i + buf.scroll_offset;
    int draw_y = y + i;

    if (line_idx < (int)buf.lines.size()) {
      int line_diag_severity = line_diagnostic_severity(buf, line_idx);
      int diag_fg = line_diag_severity > 0
                        ? diagnostic_severity_color(theme, line_diag_severity)
                        : theme.fg_line_num;
      if (line_diag_severity > 0) {
        // VSCode-like gutter accent: a solid color block instead of W/E glyphs.
        ui->draw_text(x + 1, draw_y, " ", diag_fg, diag_fg, true);
      } else {
        ui->draw_text(x + 1, draw_y, " ", theme.fg_line_num, theme.bg_default);
      }

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

      const std::string &line = buf.lines[line_idx];
      int scroll_x = buf.scroll_x;
      int current_x = x + 1 + line_num_width;
      int visible_len = w - 2 - line_num_width;
      visual_cols.resize(line.size() + 1);
      visual_cols[0] = 0;
      for (int vi = 0; vi < (int)line.size(); vi++) {
        visual_cols[vi + 1] = visual_cols[vi] +
                              (line[vi] == '\t' ? tab_advance(visual_cols[vi], tab_size)
                                                : 1);
      }
      int clamped_scroll_x = std::clamp(scroll_x, 0, (int)line.size());
      int start_visual = visual_cols[clamped_scroll_x];
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
        int line_bracket_depth = bracket_depth;
        std::vector<int> search_hit_columns;
        int active_search_col = -1;
        if (show_search && !search_query.empty()) {
          auto it = std::lower_bound(search_results.begin(), search_results.end(),
                                     std::make_pair(line_idx, 0));
          while (it != search_results.end() && it->first == line_idx) {
            search_hit_columns.push_back(it->second);
            ++it;
          }
          if (search_result_index >= 0 &&
              search_result_index < (int)search_results.size() &&
              search_results[search_result_index].first == line_idx) {
            active_search_col = search_results[search_result_index].second;
          }
        }
        size_t next_search_hit = 0;

        auto draw_chunk = [&](int start_idx, int len, int color) {
          if (len <= 0)
            return;

          int ch_start = start_idx;

          for (int k = 0; k < len; k++) {
            int char_idx = ch_start + k;
            if (char_idx < 0 || char_idx >= (int)line.size())
              continue;

            char c = line[char_idx];
            const bool tokenized =
                char_idx < (int)colors.size() && colors[char_idx].first == 1;
            const int token_type = tokenized ? colors[char_idx].second : 0;
            const bool skip_bracket_logic = (token_type == 2 || token_type == 3);
            int bracket_color = -1;
            if (!skip_bracket_logic && is_open_bracket(c)) {
              bracket_color = rainbow_bracket_color(theme, line_bracket_depth);
              line_bracket_depth++;
            } else if (!skip_bracket_logic && is_close_bracket(c)) {
              line_bracket_depth = std::max(0, line_bracket_depth - 1);
              bracket_color = rainbow_bracket_color(theme, line_bracket_depth);
            }

            if (char_idx < scroll_x)
              continue;
            int vis_idx = visual_cols[char_idx] - start_visual;
            if (vis_idx >= visible_len)
              break;
            int char_w =
                std::max(1, visual_cols[char_idx + 1] - visual_cols[char_idx]);

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
                const bool is_active_match =
                    search_hit_columns[next_search_hit] == active_search_col;
                if (is_active_match) {
                  bg = theme.bg_selection;
                  fg = theme.fg_selection;
                } else {
                  bg = theme.bg_search_match;
                  fg = theme.fg_search_match;
                }
              }
            }

            bool in_leading_indent = char_idx < leading_ws_end;
            bool indent_guide_col =
                (c == '\t') ||
                (c == ' ' && tab_size > 0 && ((char_idx + 1) % tab_size == 0));

            if (in_leading_indent && indent_guide_col) {
              int guide_fg = in_sel ? theme.fg_selection : theme.fg_line_num;
              ui->draw_text(current_x + vis_idx, draw_y, "·", guide_fg, bg);
              if (c == '\t') {
                for (int fill = 1; fill < char_w && vis_idx + fill < visible_len;
                     fill++) {
                  ui->draw_text(current_x + vis_idx + fill, draw_y, " ", guide_fg,
                                bg);
                }
              }
              continue;
            }

            if (bracket_color != -1 && !in_sel &&
                !(next_search_hit < search_hit_columns.size() &&
                  char_idx >= search_hit_columns[next_search_hit] &&
                  char_idx < search_hit_columns[next_search_hit] +
                                 (int)search_query.length())) {
              fg = bracket_color;
            }

            if (c == '\t') {
              for (int fill = 0; fill < char_w && vis_idx + fill < visible_len;
                   fill++) {
                ui->draw_text(current_x + vis_idx + fill, draw_y, " ", fg, bg);
              }
            } else {
              ui->draw_text(current_x + vis_idx, draw_y, std::string(1, c), fg,
                            bg);
            }
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
        bracket_depth = line_bracket_depth;
      } else {
        for (char c : line) {
          apply_bracket_depth_delta(c, bracket_depth);
        }
      }

      if (bracket_guide.active && line_idx > bracket_guide.start_line &&
          line_idx < bracket_guide.end_line) {
        int guide_vis_idx = bracket_guide.visual_column - start_visual;
        if (guide_vis_idx >= 0 && guide_vis_idx < visible_len) {
          ui->draw_text(current_x + guide_vis_idx, draw_y, "│",
                        theme.fg_bracket_match, theme.bg_default);
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
      const int code_start_x = x + 1 + line_num_width;
      const int code_end_x = x + w - 2;

      int anchor_col = buf.cursor.x;
      if (buf.cursor.y < active_diag->line) {
        anchor_col = active_diag->col;
      } else if (buf.cursor.y > active_diag->end_line) {
        anchor_col = active_diag->end_col;
      } else if (buf.cursor.y == active_diag->line &&
                 buf.cursor.y == active_diag->end_line) {
        anchor_col =
            std::clamp(buf.cursor.x, active_diag->col, active_diag->end_col);
      } else if (buf.cursor.y == active_diag->line) {
        anchor_col = std::max(buf.cursor.x, active_diag->col);
      } else if (buf.cursor.y == active_diag->end_line) {
        anchor_col = std::min(buf.cursor.x, active_diag->end_col);
      }

      int cursor_line = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
      const std::string &anchor_line = buf.lines[cursor_line];
      int anchor_visual = compute_visual_column(anchor_line, anchor_col, tab_size);
      int scroll_visual =
          compute_visual_column(anchor_line, buf.scroll_x, tab_size);
      int anchor_x = code_start_x + (anchor_visual - scroll_visual);
      anchor_x = std::clamp(anchor_x, code_start_x, code_end_x);
      int anchor_y = y + (buf.cursor.y - buf.scroll_offset);
      int line_end_visual =
          compute_visual_column(anchor_line, (int)anchor_line.size(), tab_size);
      int line_end_x = code_start_x + (line_end_visual - scroll_visual);
      // Draw diagnostics only in trailing whitespace area, never over code.
      int inline_x = std::max(anchor_x + 2, line_end_x + 1);
      int inline_y = std::max(y, std::min(anchor_y, y + h - 1));
      if (inline_x <= code_end_x) {
        int available = code_end_x - inline_x + 1;
        if (available >= 8) {
          std::string msg = compact_diagnostic_text(active_diag->message);
          std::string inline_text =
              diagnostic_severity_label(active_diag->severity) + ": " + msg;
          if ((int)inline_text.length() > available) {
            if (available > 3) {
              inline_text =
                  inline_text.substr(0, (size_t)(available - 3)) + "...";
            } else {
              inline_text = inline_text.substr(0, (size_t)available);
            }
          }
          ui->draw_text(inline_x, inline_y, inline_text, theme.fg_comment,
                        theme.bg_default);
        }
      }
    }
  }
}
