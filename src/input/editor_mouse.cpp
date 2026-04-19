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

static int compare_cursor_pos(const Cursor &a, const Cursor &b) {
  if (a.y != b.y)
    return (a.y < b.y) ? -1 : 1;
  if (a.x != b.x)
    return (a.x < b.x) ? -1 : 1;
  return 0;
}

static std::string ellipsize_middle(const std::string &s, int max_len) {
  if (max_len <= 0)
    return "";
  if ((int)s.size() <= max_len)
    return s;
  if (max_len <= 3)
    return s.substr(0, (size_t)max_len);
  int keep_left = (max_len - 3) / 2;
  int keep_right = max_len - 3 - keep_left;
  return s.substr(0, (size_t)keep_left) + "..." +
         s.substr(s.size() - (size_t)keep_right);
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
    if (buf.scroll_offset > 0) {
      buf.scroll_offset -= 3;
      if (buf.scroll_offset < 0)
        buf.scroll_offset = 0;
      needs_redraw = true;
    }
    return;
  }
  if (is_scroll_down) {
    if (buf.scroll_offset < (int)buf.lines.size() - pane.h + 1) {
      buf.scroll_offset += 3;
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

  if (show_home_menu) {
    if (handle_home_menu_mouse(event->x, event->y, is_click)) {
      return;
    }
    if (show_home_menu) {
      return;
    }
  }

  if (show_sidebar && is_click) {
    if (event->x < sidebar_width && event->y >= tab_height &&
        event->y < terminal.get_height() - status_height) {
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

  if (is_click && handle_integrated_terminal_mouse(event->x, event->y)) {
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

  if (is_click && event->y < tab_height) {
    int bar_x = show_sidebar ? std::min(sidebar_width, std::max(0, ui->get_width() - 20)) : 0;
    int bar_w = ui->get_width() - bar_x;
    if (bar_w > 0 && !buffers.empty()) {
      std::vector<std::string> base_names(buffers.size());
      std::unordered_map<std::string, int> base_count;
      for (int i = 0; i < (int)buffers.size(); i++) {
        std::string base = get_filename(buffers[i].filepath);
        if (base.empty())
          base = "[No Name]";
        base_names[i] = base;
        base_count[base]++;
      }

      std::vector<int> tab_widths(buffers.size(), 0);
      for (int i = 0; i < (int)buffers.size(); i++) {
        std::string name = base_names[i];
        if (base_count[name] > 1 && !buffers[i].filepath.empty()) {
          std::filesystem::path p(buffers[i].filepath);
          std::string parent = p.parent_path().filename().string();
          if (!parent.empty())
            name += " <" + parent + ">";
        }
        name = ellipsize_middle(name, 28);
        std::string disp = " " + name + (buffers[i].modified ? " * " : " ");
        tab_widths[i] = (int)disp.size() + 2;
      }

      int total_w = 0;
      for (int tw : tab_widths)
        total_w += tw;
      bool overflow = total_w > bar_w;
      int control_w = overflow ? 2 : 0;
      int tabs_start_x = bar_x + control_w;
      int tabs_end_x = bar_x + bar_w - control_w;
      int avail_w = std::max(0, tabs_end_x - tabs_start_x);

      if (!overflow) {
        tab_scroll_index = 0;
      } else {
        tab_scroll_index = std::clamp(tab_scroll_index, 0,
                                      std::max(0, (int)buffers.size() - 1));
        if (current_buffer < tab_scroll_index)
          tab_scroll_index = current_buffer;

        auto contains_current = [&](int start_idx) {
          int used = 0;
          for (int i = start_idx; i < (int)buffers.size(); i++) {
            if (used + tab_widths[i] > avail_w)
              break;
            if (i == current_buffer)
              return true;
            used += tab_widths[i];
          }
          return false;
        };
        while (tab_scroll_index < current_buffer &&
               !contains_current(tab_scroll_index)) {
          tab_scroll_index++;
        }
      }

      int tab_x = tabs_start_x;
      int last_visible_index = -1;
      for (int i = tab_scroll_index; i < (int)buffers.size(); i++) {
        if (tab_x + tab_widths[i] > tabs_end_x)
          break;
        last_visible_index = i;

        std::string name = base_names[i];
        if (base_count[name] > 1 && !buffers[i].filepath.empty()) {
          std::filesystem::path p(buffers[i].filepath);
          std::string parent = p.parent_path().filename().string();
          if (!parent.empty())
            name += " <" + parent + ">";
        }
        name = ellipsize_middle(name, 28);
        std::string disp = " " + name + (buffers[i].modified ? " * " : " ");
        int close_x = tab_x + (int)disp.length();

        if (event->x >= tab_x && event->x < tab_x + tab_widths[i]) {
          long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
          bool tab_double = (last_tab_click_ms > 0) &&
                            (now_ms - last_tab_click_ms <= 350) &&
                            (last_tab_clicked_index == i);
          last_tab_click_ms = now_ms;
          last_tab_clicked_index = i;

          if (event->x == close_x) {
            close_buffer_at(i);
          } else {
            current_buffer = i;
            get_pane().buffer_id = current_buffer;
            if (tab_double && buffers[i].is_preview) {
              buffers[i].is_preview = false;
              if (preview_buffer_index == i) {
                preview_buffer_index = -1;
              }
            }
          }
          needs_redraw = true;
          return;
        }
        tab_x += tab_widths[i];
      }

      if (overflow) {
        bool has_left = tab_scroll_index > 0;
        bool has_right = last_visible_index >= 0 &&
                         last_visible_index < (int)buffers.size() - 1;
        if (event->x == bar_x && has_left) {
          tab_scroll_index = std::max(0, tab_scroll_index - 1);
          needs_redraw = true;
          return;
        }
        if (event->x == bar_x + bar_w - 1 && has_right) {
          tab_scroll_index = std::min((int)buffers.size() - 1, tab_scroll_index + 1);
          needs_redraw = true;
          return;
        }
      }
    }
  }

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting)
    return;

  if (inside_pane && is_click)
    focus_state = FOCUS_EDITOR;

  int raw_rel_y = event->y - pane.y - 1;
  int rel_y = raw_rel_y;
  int rel_x = event->x - pane.x - 8;
  if (rel_x < 0)
    rel_x = 0;
  int visible_rows = std::max(1, pane.h - 2);
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
  int click_x = rel_x + buf.scroll_x;
  if (click_y < 0)
    click_y = 0;
  if (click_y >= (int)buf.lines.size())
    click_y = buf.lines.size() - 1;
  if (click_y < 0)
    return;
  if (click_x < 0)
    click_x = 0;
  int line_len = buf.lines[click_y].length();
  if (click_x > line_len)
    click_x = line_len;

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
      } else {
        int pivot = std::min(click_x, (int)line.length() - 1);
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
        mouse_selecting = true;
        mouse_selection_mode = MOUSE_SELECT_WORD;
        mouse_start = {start, click_y};
        mouse_anchor_end = {end, click_y};
        buf.selection.start = mouse_start;
        buf.selection.end = mouse_anchor_end;
        buf.selection.active = true;
        buf.cursor = mouse_anchor_end;
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
        buf.selection.end = {click_x, click_y};
        buf.selection.active =
            !(buf.selection.start.x == buf.selection.end.x &&
              buf.selection.start.y == buf.selection.end.y);
      }
      needs_redraw = true;
    }
  }

  clamp_cursor(pane.buffer_id);
}
