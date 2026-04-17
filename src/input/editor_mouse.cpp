#include "editor.h"
#include <algorithm>
#include <chrono>
#include <cctype>

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

void Editor::handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                                bool is_scroll_down) {
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
        handle_sidebar_mouse(x, y, is_click);
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

  if (bstate == 3) {
    show_context_menu = true;
    context_menu_x = event->x;
    context_menu_y = event->y;
    context_menu_selected = 0;
    needs_redraw = true;
    return;
  }

  bool is_click = (bstate == 1);

  if (show_sidebar && is_click) {
    if (event->x < sidebar_width && event->y >= tab_height &&
        event->y < terminal.get_height() - status_height) {
      focus_state = FOCUS_SIDEBAR;
      handle_sidebar_mouse(event->x, event->y, true);
      needs_redraw = true;
      return;
    }
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
    int tab_x = show_sidebar ? sidebar_width : 0;
    for (int i = 0; i < (int)buffers.size(); i++) {
      std::string name = get_filename(buffers[i].filepath);
      if (name.empty())
        name = "[No Name]";
      if (buffers[i].modified)
        name += "+";
      std::string disp = " " + name + " ";
      int w = disp.length() + 1;
      if (event->x >= tab_x && event->x < tab_x + w) {
        current_buffer = i;
        get_pane().buffer_id = current_buffer;
        needs_redraw = true;
        return;
      }
      tab_x += w;
      if (tab_x >= ui->get_width())
        break;
    }
  }

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting && bstate != 3)
    return;

  if (inside_pane && is_click)
    focus_state = FOCUS_EDITOR;

  int rel_y = event->y - pane.y - 1;
  int rel_x = event->x - pane.x - 7;
  if (rel_y < 0)
    rel_y = 0;
  if (rel_x < 0)
    rel_x = 0;

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

  if (bstate == 3) {
    show_context_menu = true;
    context_menu_x = event->x;
    context_menu_y = event->y;
    context_menu_selected = 0;
    needs_redraw = true;
    return;
  }

  if (bstate == 1) {
    if (show_context_menu) {
      show_context_menu = false;
      needs_redraw = true;
    }
    focus_state = FOCUS_EDITOR;
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    auto now = std::chrono::steady_clock::now();
    long long now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count();
    bool is_double_click =
        (last_left_click_ms > 0) && (now_ms - last_left_click_ms <= 350) &&
        (last_left_click_pos.x == click_x) && (last_left_click_pos.y == click_y);
    last_left_click_ms = now_ms;
    last_left_click_pos = {click_x, click_y};

    if (is_double_click) {
      const std::string &line = buf.lines[click_y];
      if (line.empty()) {
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
        buf.selection.start = {start, click_y};
        buf.selection.end = {end, click_y};
        buf.selection.active = true;
        buf.cursor.x = end;
        buf.cursor.y = click_y;
      }
      mouse_selecting = false;
      needs_redraw = true;
    } else {
      mouse_selecting = true;
      mouse_start = {click_x, click_y};
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.start = mouse_start;
      buf.selection.end = {click_x, click_y};
      buf.selection.active = true;
      needs_redraw = true;
    }
  } else if (bstate == 2) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    if (mouse_selecting) {
      const bool had_drag_range = !(buf.selection.start == buf.selection.end);

      buf.cursor.x = click_x;
      buf.cursor.y = click_y;

      if (inside_pane || !had_drag_range) {
        buf.selection.end = {click_x, click_y};
      }
      buf.selection.active =
          !(buf.selection.start.x == buf.selection.end.x &&
            buf.selection.start.y == buf.selection.end.y);
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
    if (mouse_selecting) {
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.selection.end = {click_x, click_y};
      if (buf.cursor.y < buf.scroll_offset)
        buf.scroll_offset = std::max(0, buf.cursor.y - 2);
      if (buf.cursor.y >= buf.scroll_offset + pane.h - 2)
        buf.scroll_offset = buf.cursor.y - pane.h + 3;
      needs_redraw = true;
    }
  }

  clamp_cursor(pane.buffer_id);
}
