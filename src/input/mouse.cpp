#include "editor.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>

// Local definition of MEVENT used by handle_mouse()
struct MEVENT {
  int x, y;
  int bstate;
};

static int classify_char(unsigned char c) {
  if (std::isspace(c))
    return 0; // whitespace run
  if (std::isalnum(c) || c == '_')
    return 1; // word run
  return 2;   // punctuation/symbol run
}

static bool bracket_pair(char c, char &open, char &close, bool &is_open) {
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

static int compare_cursor_pos(const Cursor &a, const Cursor &b) {
  if (a.y != b.y)
    return (a.y < b.y) ? -1 : 1;
  if (a.x != b.x)
    return (a.x < b.x) ? -1 : 1;
  return 0;
}

static int tab_advance(int visual_col, int tab_size) {
  const int ts = std::max(1, tab_size);
  const int rem = visual_col % ts;
  return rem == 0 ? ts : (ts - rem);
}

static int logical_to_visual_col(const std::string &line, int logical_col,
                                 int tab_size) {
  int clamped = std::clamp(logical_col, 0, (int)line.size());
  int visual = 0;
  for (int i = 0; i < clamped; i++) {
    visual += (line[i] == '\t') ? tab_advance(visual, tab_size) : 1;
  }
  return visual;
}

static int visual_to_logical_col(const std::string &line, int visual_col,
                                 int tab_size) {
  int target = std::max(0, visual_col);
  int visual = 0;
  for (int i = 0; i < (int)line.size(); i++) {
    int next = visual + ((line[i] == '\t') ? tab_advance(visual, tab_size) : 1);
    if (target < next) {
      int left_dist = target - visual;
      int right_dist = next - target;
      return (left_dist <= right_dist) ? i : (i + 1);
    }
    visual = next;
  }
  return (int)line.size();
}

void Editor::handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                                bool is_scroll_down) {
  if (show_home_menu) {
    if (is_click) {
      handle_home_menu_mouse(x, y, true);
      return;
    }

    if (is_scroll_up && !home_menu_entries.empty()) {
      home_menu_selected =
          (home_menu_selected - 1 + (int)home_menu_entries.size()) %
          (int)home_menu_entries.size();
      needs_redraw = true;
      return;
    }
    if (is_scroll_down && !home_menu_entries.empty()) {
      home_menu_selected =
          (home_menu_selected + 1) % (int)home_menu_entries.size();
      needs_redraw = true;
      return;
    }
    return;
  }

  if ((is_scroll_up || is_scroll_down) &&
      handle_integrated_terminal_scroll(x, y, is_scroll_up, is_scroll_down)) {
    return;
  }

  if (show_sidebar) {
    if (x < sidebar_width) {
      if (is_scroll_up) {
        if (file_tree_scroll > 0)
          file_tree_scroll--;
        needs_redraw = true;
      } else if (is_scroll_down) {
        file_tree_scroll++;
        needs_redraw = true;
      } else if (is_click) {
        focus_state = FOCUS_SIDEBAR;
        handle_sidebar_mouse(x, y, is_click, false);
      }
      return;
    }
  }

  if (is_click) {
    focus_state = FOCUS_EDITOR;
  }

  int pane_index = -1;
  for (int i = 0; i < (int)panes.size(); i++) {
    const auto &pane = panes[i];
    if (x >= pane.x && x < pane.x + pane.w && y >= pane.y &&
        y < pane.y + pane.h) {
      pane_index = i;
      break;
    }
  }
  if (pane_index == -1)
    return;

  if (is_click && pane_index != current_pane) {
    panes[current_pane].active = false;
    current_pane = pane_index;
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
  }

  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);

  if (is_scroll_up) {
    int wheel_step = std::max(1, std::min(5, std::max(1, pane.h - tab_height) / 6));
    if (buf.scroll_offset > 0) {
      buf.scroll_offset -= wheel_step;
      if (buf.scroll_offset < 0)
        buf.scroll_offset = 0;
      needs_redraw = true;
    }
    return;
  }
  if (is_scroll_down) {
    int wheel_step = std::max(1, std::min(5, std::max(1, pane.h - tab_height) / 6));
    if (buf.scroll_offset < (int)buf.lines.size() - pane.h + 1) {
      buf.scroll_offset += wheel_step;
      if (buf.scroll_offset > (int)buf.lines.size() - 1)
        buf.scroll_offset = (int)buf.lines.size() - 1;
      needs_redraw = true;
    }
    return;
  }
}

void Editor::handle_mouse(void *event_ptr) {
  MEVENT *event = (MEVENT *)event_ptr;
  if (panes.empty())
    return;

  int bstate = event->bstate;

  if (bstate == 3)
    return;

  bool is_click = (bstate == 1);
  bool is_click_release = (bstate == 2);
  bool is_primary_click = is_click || is_click_release;

  if (show_home_menu) {
    if (handle_home_menu_mouse(event->x, event->y, is_click)) {
      return;
    }
    if (show_home_menu) {
      return;
    }
  }

  if (show_sidebar && is_click) {
    int reserved_terminal_h = 0;
    if (show_integrated_terminal && !integrated_terminals.empty()) {
      reserved_terminal_h = std::clamp(integrated_terminal_height, 5,
                                       std::max(5, ui->get_height() / 2));
    }
    int content_bottom =
        terminal.get_height() - status_height - reserved_terminal_h;
    if (event->x < sidebar_width && event->y >= tab_height &&
        event->y < content_bottom) {
      focus_state = FOCUS_SIDEBAR;
      long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
      int sidebar_row = event->y - tab_height + file_tree_scroll;
      bool sidebar_double = (last_sidebar_click_ms > 0) &&
                            (now_ms - last_sidebar_click_ms <= 350) &&
                            (last_sidebar_click_row == sidebar_row);
      last_sidebar_click_ms = now_ms;
      last_sidebar_click_row = sidebar_row;
      handle_sidebar_mouse(event->x, event->y, true, sidebar_double);
      needs_redraw = true;
      return;
    }
  }

  if (is_primary_click && handle_integrated_terminal_mouse(event->x, event->y)) {
    return;
  }

  IntegratedTerminal *active_terminal = get_integrated_terminal();
  if (is_click && show_integrated_terminal && active_terminal &&
      active_terminal->is_focused()) {
    activate_integrated_terminal(current_integrated_terminal, false);
    needs_redraw = true;
  }

  int target_pane = current_pane;
  for (int i = 0; i < (int)panes.size(); i++) {
    const auto &candidate = panes[i];
    if (event->x >= candidate.x && event->x < candidate.x + candidate.w &&
        event->y >= candidate.y && event->y < candidate.y + candidate.h) {
      target_pane = i;
      break;
    }
  }

  if (is_click && target_pane != current_pane) {
    panes[current_pane].active = false;
    current_pane = target_pane;
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
  }

  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);

  if (is_click && event->y == pane.y && !buffers.empty()) {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    int tabs_x = pane.x + 1;
    int tabs_w = std::max(1, draw_w - 2);
    if (event->x >= tabs_x && event->x < tabs_x + tabs_w) {
      std::vector<int> tab_ids = pane.tab_buffer_ids;
      if (tab_ids.empty() && pane.buffer_id >= 0 &&
          pane.buffer_id < (int)buffers.size()) {
        tab_ids.push_back(pane.buffer_id);
      }

      std::vector<std::string> base_names(buffers.size());
      std::unordered_map<std::string, int> base_count;
      for (int tab_i = 0; tab_i < (int)tab_ids.size(); tab_i++) {
        int i = tab_ids[tab_i];
        if (i < 0 || i >= (int)buffers.size()) {
          continue;
        }
        std::string base = get_filename(buffers[i].filepath);
        if (base.empty())
          base = "[No Name]";
        base_names[i] = base;
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
        if ((int)label.size() > max_tab_w) {
          if (max_tab_w <= 3) {
            label = label.substr(0, (size_t)max_tab_w);
          } else {
            label = label.substr(0, (size_t)(max_tab_w - 3)) + "...";
          }
        }
        int need = (int)label.size() + 2; // close button + separator
        if (tab_x + need > tabs_x + tabs_w) {
          break;
        }

        int close_x = tab_x + (int)label.size();
        if (event->x == close_x) {
          close_buffer_at(i);
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }

        if (event->x >= tab_x && event->x < close_x) {
          pane.buffer_id = i;
          current_buffer = i;
          if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                        i) == pane.tab_buffer_ids.end()) {
            pane.tab_buffer_ids.push_back(i);
          }
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }
        tab_x += need;
      }
    }
  }

  if (show_minimap && is_click) {
    if (event->x >= pane.x + pane.w - minimap_width &&
        event->x < pane.x + pane.w) {
      if (event->y >= pane.y + tab_height && event->y < pane.y + pane.h) {
        int rel_y = event->y - (pane.y + tab_height);
        int h = pane.h - tab_height;
        int total_lines = buf.lines.size();
        if (total_lines > 0) {
          float ratio = (float)h / total_lines;
          if (ratio > 1.0f)
            ratio = 1.0f;
          int target_line = (int)(rel_y / ratio);
          buf.scroll_offset = target_line;
          if (buf.scroll_offset < 0)
            buf.scroll_offset = 0;
          if (buf.scroll_offset > (int)buf.lines.size() - 1)
            buf.scroll_offset = (int)buf.lines.size() - 1;
          needs_redraw = true;
          return;
        }
      }
    }
  }

  // Global top tab bar is disabled; pane-local headers are rendered per pane.

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting)
    return;

  if (inside_pane && is_click)
    focus_state = FOCUS_EDITOR;

  const int line_num_width = 7;
  const int code_start_x = pane.x + 1 + line_num_width;
  const int content_top = pane.y + tab_height;
  const int content_bottom = pane.y + pane.h;

  int raw_rel_y = event->y - content_top;
  int rel_y = raw_rel_y;
  int rel_visual_x = event->x - code_start_x;
  if (event->y < content_top)
    raw_rel_y = -1;
  if (event->y >= content_bottom)
    raw_rel_y = content_bottom - content_top;
  rel_y = raw_rel_y;
  if (rel_visual_x < 0)
    rel_visual_x = 0;
  int visible_rows = std::max(1, pane.h - tab_height);
  int max_scroll_offset =
      std::max(0, (int)buf.lines.size() - visible_rows);

  if (bstate == 32 && mouse_selecting) {
    if (raw_rel_y < 0) {
      int scroll_by = std::min(buf.scroll_offset, std::max(1, -raw_rel_y));
      buf.scroll_offset -= scroll_by;
    } else if (raw_rel_y >= visible_rows) {
      int remaining = max_scroll_offset - buf.scroll_offset;
      int scroll_by =
          std::min(remaining, std::max(1, raw_rel_y - visible_rows + 1));
      buf.scroll_offset += scroll_by;
    }
  }

  rel_y = std::clamp(rel_y, 0, visible_rows - 1);

  int click_y = rel_y + buf.scroll_offset;
  if (click_y < 0)
    click_y = 0;
  if (click_y >= (int)buf.lines.size())
    click_y = buf.lines.size() - 1;
  if (click_y < 0)
    return;
  const std::string &clicked_line = buf.lines[click_y];
  int line_len = clicked_line.length();
  int start_visual = logical_to_visual_col(clicked_line, buf.scroll_x, tab_size);
  int click_visual = start_visual + rel_visual_x;
  int click_x = visual_to_logical_col(clicked_line, click_visual, tab_size);
  click_x = std::clamp(click_x, 0, line_len);

  auto set_word_selection = [&](const Cursor &anchor_start,
                                const Cursor &anchor_end,
                                const Cursor &current_pos) {
    Cursor current_start = current_pos;
    Cursor current_end = current_pos;
    const std::string &line = buf.lines[current_pos.y];
    if (!line.empty()) {
      int pivot = std::min(current_pos.x, (int)line.length() - 1);
      if (pivot >= 0) {
        int cls = classify_char((unsigned char)line[pivot]);
        int start = pivot;
        int end = pivot + 1;
        while (start > 0 &&
               classify_char((unsigned char)line[start - 1]) == cls) {
          start--;
        }
        while (end < (int)line.length() &&
               classify_char((unsigned char)line[end]) == cls) {
          end++;
        }
        current_start = {start, current_pos.y};
        current_end = {end, current_pos.y};
      }
    }

    if (compare_cursor_pos(current_start, anchor_start) < 0) {
      buf.selection.start = current_start;
      buf.selection.end = anchor_end;
      buf.cursor = current_start;
    } else {
      buf.selection.start = anchor_start;
      buf.selection.end = current_end;
      buf.cursor = current_end;
    }
    buf.selection.active =
        !(buf.selection.start.x == buf.selection.end.x &&
          buf.selection.start.y == buf.selection.end.y);
  };

  auto set_line_selection = [&](const Cursor &anchor_start,
                                const Cursor &anchor_end,
                                const Cursor &current_pos) {
    Cursor current_start = {0, current_pos.y};
    Cursor current_end = {(int)buf.lines[current_pos.y].length(), current_pos.y};

    if (compare_cursor_pos(current_start, anchor_start) < 0) {
      buf.selection.start = current_start;
      buf.selection.end = anchor_end;
      buf.cursor = current_start;
    } else {
      buf.selection.start = anchor_start;
      buf.selection.end = current_end;
      buf.cursor = current_end;
    }
    buf.selection.active =
        !(buf.selection.start.x == buf.selection.end.x &&
          buf.selection.start.y == buf.selection.end.y);
  };

  auto find_word_span_at_or_near = [&](int line_y, int x, int &start,
                                       int &end) -> bool {
    if (line_y < 0 || line_y >= (int)buf.lines.size())
      return false;
    const std::string &line = buf.lines[line_y];
    if (line.empty())
      return false;

    int pivot = std::clamp(x, 0, (int)line.size() - 1);

    auto expand_span = [&](int p, int &s, int &e) -> bool {
      if (p < 0 || p >= (int)line.size())
        return false;
      int cls = classify_char((unsigned char)line[p]);
      if (cls == 0)
        return false; // skip whitespace runs for smart word select
      s = p;
      e = p + 1;
      while (s > 0 && classify_char((unsigned char)line[s - 1]) == cls)
        s--;
      while (e < (int)line.size() &&
             classify_char((unsigned char)line[e]) == cls)
        e++;
      return true;
    };

    if (expand_span(pivot, start, end))
      return true;
    if (pivot + 1 < (int)line.size() && expand_span(pivot + 1, start, end))
      return true;
    if (pivot - 1 >= 0 && expand_span(pivot - 1, start, end))
      return true;

    for (int d = 2; d < (int)line.size(); d++) {
      int right = pivot + d;
      int left = pivot - d;
      if (right < (int)line.size() && expand_span(right, start, end))
        return true;
      if (left >= 0 && expand_span(left, start, end))
        return true;
      if (right >= (int)line.size() && left < 0)
        break;
    }

    return false;
  };

  auto find_matching_bracket = [&](int line_y, int x, Cursor &open_pos,
                                   Cursor &close_pos) -> bool {
    if (line_y < 0 || line_y >= (int)buf.lines.size())
      return false;
    const std::string &line = buf.lines[line_y];
    if (x < 0 || x >= (int)line.size())
      return false;

    char open = 0, close = 0;
    bool is_open = false;
    if (!bracket_pair(line[x], open, close, is_open))
      return false;

    if (is_open) {
      int depth = 1;
      for (int y = line_y; y < (int)buf.lines.size(); y++) {
        int start_x = (y == line_y) ? x + 1 : 0;
        for (int cx = start_x; cx < (int)buf.lines[y].size(); cx++) {
          char ch = buf.lines[y][cx];
          if (ch == open) {
            depth++;
          } else if (ch == close) {
            depth--;
            if (depth == 0) {
              open_pos = {x, line_y};
              close_pos = {cx, y};
              return true;
            }
          }
        }
      }
    } else {
      int depth = 1;
      for (int y = line_y; y >= 0; y--) {
        int start_x = (y == line_y) ? x - 1 : (int)buf.lines[y].size() - 1;
        for (int cx = start_x; cx >= 0; cx--) {
          char ch = buf.lines[y][cx];
          if (ch == close) {
            depth++;
          } else if (ch == open) {
            depth--;
            if (depth == 0) {
              open_pos = {cx, y};
              close_pos = {x, line_y};
              return true;
            }
          }
        }
      }
    }
    return false;
  };

  if (bstate == 1) {
    focus_state = FOCUS_EDITOR;
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    hide_lsp_completion();
    auto now = std::chrono::steady_clock::now();
    long long now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count();
    bool same_click_cluster =
        (last_left_click_ms > 0) && (now_ms - last_left_click_ms <= 350) &&
        (last_left_click_pos.y == click_y) &&
        (std::abs(last_left_click_pos.x - click_x) <= 1);
    if (same_click_cluster) {
      last_left_click_count = std::min(3, last_left_click_count + 1);
    } else {
      last_left_click_count = 1;
    }
    last_left_click_ms = now_ms;
    last_left_click_pos = {click_x, click_y};

    if (last_left_click_count >= 3) {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_LINE;
      mouse_start = {0, click_y};
      mouse_anchor_end = {line_len, click_y};
      set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      needs_redraw = true;
    } else if (last_left_click_count == 2) {
      const std::string &line = buf.lines[click_y];
      if (line.empty()) {
        mouse_selecting = true;
        mouse_selection_mode = MOUSE_SELECT_LINE;
        mouse_start = {0, click_y};
        mouse_anchor_end = {0, click_y};
        buf.selection.start = mouse_start;
        buf.selection.end = mouse_anchor_end;
        buf.selection.active = false;
        buf.cursor.x = 0;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;
      } else {
        int pivot = std::min(click_x, (int)line.length() - 1);
        Cursor bracket_open{0, 0};
        Cursor bracket_close{0, 0};
        if (find_matching_bracket(click_y, pivot, bracket_open, bracket_close)) {
          mouse_selecting = false;
          mouse_selection_mode = MOUSE_SELECT_CHAR;
          mouse_start = bracket_open;
          mouse_anchor_end = {bracket_close.x + 1, bracket_close.y};
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_anchor_end;
          buf.selection.active = true;
          buf.cursor = mouse_anchor_end;
          buf.preferred_x = buf.cursor.x;
          ensure_cursor_visible();
          needs_redraw = true;
          return;
        }

        int start = 0;
        int end = 0;
        if (!find_word_span_at_or_near(click_y, click_x, start, end)) {
          start = 0;
          end = (int)line.length();
        }
        mouse_selecting = true;
        mouse_selection_mode = MOUSE_SELECT_WORD;
        mouse_start = {start, click_y};
        mouse_anchor_end = {end, click_y};
        buf.selection.start = mouse_start;
        buf.selection.end = mouse_anchor_end;
        buf.selection.active = true;
        buf.cursor = mouse_anchor_end;
        buf.preferred_x = buf.cursor.x;
      }
      needs_redraw = true;
    } else {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_CHAR;
      mouse_start = {click_x, click_y};
      mouse_anchor_end = mouse_start;
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.start = mouse_start;
      buf.selection.end = {click_x, click_y};
      buf.selection.active = false;
      needs_redraw = true;
    }
  } else if (bstate == 2) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    hide_lsp_completion();
    if (mouse_selecting) {
      const bool had_drag_range = !(buf.selection.start == buf.selection.end);

      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;

      if (mouse_selection_mode == MOUSE_SELECT_WORD) {
        set_word_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      } else if (mouse_selection_mode == MOUSE_SELECT_LINE) {
        set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      } else if (inside_pane || !had_drag_range) {
        buf.selection.end = {click_x, click_y};
        buf.selection.active =
            !(buf.selection.start.x == buf.selection.end.x &&
              buf.selection.start.y == buf.selection.end.y);
        if (!buf.selection.active) {
          buf.selection.start = buf.cursor;
          buf.selection.end = buf.cursor;
        }
      }
      mouse_selecting = false;
      needs_redraw = true;
    } else {
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;
      needs_redraw = true;
    }
  } else if (bstate == 32) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    hide_lsp_completion();
    if (mouse_selecting) {
      if (mouse_selection_mode == MOUSE_SELECT_WORD) {
        set_word_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      } else if (mouse_selection_mode == MOUSE_SELECT_LINE) {
        set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      } else {
        buf.cursor.x = click_x;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;
        buf.selection.end = {click_x, click_y};
        buf.selection.active =
            !(buf.selection.start.x == buf.selection.end.x &&
              buf.selection.start.y == buf.selection.end.y);
      }
      needs_redraw = true;
    }
  }

  clamp_cursor(pane.buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible();
}
