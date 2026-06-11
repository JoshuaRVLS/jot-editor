#include "editor.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace {
bool mouse_debug_enabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = std::getenv("JOT_MOUSE_DEBUG") != nullptr ? 1 : 0;
  }
  return cached == 1;
}

void mouse_debug_log(const char *fmt, ...) {
  if (!mouse_debug_enabled())
    return;
  const char *path = std::getenv("JOT_MOUSE_DEBUG");
  if (!path || !*path)
    path = "/tmp/jot-mouse.log";
  FILE *f = std::fopen(path, "a");
  if (!f)
    return;
  std::va_list ap;
  va_start(ap, fmt);
  std::fprintf(f, "[mouse] ");
  std::vfprintf(f, fmt, ap);
  std::fputc('\n', f);
  std::fclose(f);
  va_end(ap);
}

constexpr int kMouseDragThreshold = 1;
} // namespace

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

static bool cursor_in_selection(const Selection &selection, const Cursor &pos) {
  if (!selection.active)
    return false;
  Cursor start = selection.start;
  Cursor end = selection.end;
  if (compare_cursor_pos(end, start) < 0)
    std::swap(start, end);
  return compare_cursor_pos(start, pos) <= 0 && compare_cursor_pos(pos, end) <= 0;
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

static void flatten_nodes_for_mouse(std::vector<FileNode> &nodes,
                                    std::vector<FileNode *> &flat) {
  for (auto &node : nodes) {
    flat.push_back(&node);
    if (node.is_dir && node.expanded) {
      flatten_nodes_for_mouse(node.children, flat);
    }
  }
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
    if (buf.scroll_offset < (int)buf.line_count() - pane.h + 1) {
      buf.scroll_offset += wheel_step;
      if (buf.scroll_offset > (int)buf.line_count() - 1)
        buf.scroll_offset = (int)buf.line_count() - 1;
      needs_redraw = true;
    }
    return;
  }
}

bool Editor::open_context_menu_for_mouse(int x, int y) {
  if (show_home_menu || panes.empty()) {
    return false;
  }

  context_menu_target_buffer = -1;
  context_menu_target_pane = -1;
  context_menu_target_terminal = -1;
  context_menu_target_path.clear();
  context_menu_target_is_dir = false;

  if (show_integrated_terminal && !integrated_terminals.empty()) {
    int panel_h = std::clamp(integrated_terminal_height, 5,
                             std::max(5, ui->get_height() / 2));
    int panel_y =
        std::max(tab_height, ui->get_height() - status_height - panel_h);
    int panel_w = ui->get_render_width();
    int tab_y = panel_y + 1;
    if (x >= 0 && x < panel_w && y >= panel_y && y < panel_y + panel_h) {
      int target_terminal = current_integrated_terminal;
      bool on_tab = (y == tab_y || y == panel_y);
      if (on_tab) {
        int tab_x = 1;
        for (int i = 0; i < (int)integrated_terminals.size(); i++) {
          std::string base_label = integrated_terminals[i]->get_label().empty()
                                       ? "term " + std::to_string(i + 1)
                                       : integrated_terminals[i]->get_label();
          std::string label = " " + base_label + " ";
          int tab_w = (int)label.size() + 2;
          if (x >= tab_x && x < tab_x + tab_w) {
            target_terminal = i;
            break;
          }
          tab_x += tab_w;
          if (tab_x >= panel_w - 4)
            break;
        }
      }
      context_menu_target_terminal = target_terminal;
      std::vector<ContextMenuItem> items = {
          {"Focus Terminal", CONTEXT_ACTION_TERMINAL_FOCUS, target_terminal >= 0},
          {"New Terminal", CONTEXT_ACTION_TERMINAL_NEW, true},
          {"Reset Scroll", CONTEXT_ACTION_TERMINAL_RESET_SCROLL,
           target_terminal >= 0},
      };
      if (on_tab) {
        items.push_back({"Close Terminal", CONTEXT_ACTION_TERMINAL_CLOSE,
                         target_terminal >= 0});
      }
      open_context_menu(x, y, CONTEXT_MENU_TERMINAL, items);
      return true;
    }
  }

  if (show_sidebar) {
    int reserved_terminal_h = 0;
    if (show_integrated_terminal && !integrated_terminals.empty()) {
      reserved_terminal_h = std::clamp(integrated_terminal_height, 5,
                                       std::max(5, ui->get_height() / 2));
    }
    int content_bottom =
        terminal.get_height() - status_height - reserved_terminal_h;
    if (x < sidebar_width && y >= tab_height && y < content_bottom) {
      focus_state = FOCUS_SIDEBAR;
      std::vector<FileNode *> flat;
      flatten_nodes_for_mouse(file_tree, flat);
      int sidebar_row = y - tab_height - 1;
      int row = sidebar_row + file_tree_scroll;
      FileNode *node = nullptr;
      if (sidebar_row >= 0 && row >= 0 && row < (int)flat.size()) {
        node = flat[row];
        file_tree_selected = row;
        context_menu_target_path = node->path;
        context_menu_target_is_dir = node->is_dir;
      } else {
        context_menu_target_path = root_dir;
        context_menu_target_is_dir = true;
      }
      bool has_target = !context_menu_target_path.empty();
      std::string open_label =
          node && node->is_dir ? (node->expanded ? "Collapse" : "Expand") : "Open";
      std::vector<ContextMenuItem> items = {
          {open_label, CONTEXT_ACTION_SIDEBAR_OPEN, node != nullptr},
          {"New File", CONTEXT_ACTION_SIDEBAR_NEW_FILE, has_target},
          {"New Folder", CONTEXT_ACTION_SIDEBAR_NEW_FOLDER, has_target},
          {"Rename", CONTEXT_ACTION_SIDEBAR_RENAME, node != nullptr},
          {"Refresh", CONTEXT_ACTION_SIDEBAR_REFRESH, true},
          {"Copy Path", CONTEXT_ACTION_SIDEBAR_COPY_PATH, has_target},
      };
      open_context_menu(x, y, CONTEXT_MENU_SIDEBAR, items);
      return true;
    }
  }

  int pane_index = -1;
  for (int i = 0; i < (int)panes.size(); i++) {
    const auto &candidate = panes[i];
    if (x >= candidate.x && x < candidate.x + candidate.w &&
        y >= candidate.y && y < candidate.y + candidate.h) {
      pane_index = i;
      break;
    }
  }
  if (pane_index < 0) {
    return false;
  }

  if (pane_index != current_pane) {
    panes[current_pane].active = false;
    current_pane = pane_index;
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
  }
  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);
  context_menu_target_pane = current_pane;

  if (y == pane.y && !buffers.empty()) {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
    for (const auto &tab : tabs.segments) {
      if (x >= tab.x && x < tab.end_x) {
        context_menu_target_buffer = tab.buffer_id;
        std::vector<ContextMenuItem> items = {
            {"Save", CONTEXT_ACTION_SAVE_BUFFER, tab.buffer_id >= 0},
            {"Close Buffer", CONTEXT_ACTION_CLOSE_BUFFER, tab.buffer_id >= 0},
        };
        open_context_menu(x, y, CONTEXT_MENU_TAB, items);
        return true;
      }
    }
  }

  const int line_num_width = 7;
  const int code_start_x = pane.x + 1 + line_num_width;
  const int content_top = pane.y + tab_height;
  const int visible_rows = std::max(1, pane.h - tab_height - 1);
  if (y >= content_top && y < content_top + visible_rows) {
    int rel_y = std::clamp(y - content_top, 0, visible_rows - 1);
    int click_y = rel_y + buf.scroll_offset;
    if (click_y >= 0 && click_y < (int)buf.line_count()) {
      const std::string &clicked_line = buf.line(click_y);
      int rel_visual_x = std::max(0, x - code_start_x);
      int start_visual =
          logical_to_visual_col(clicked_line, buf.scroll_x, tab_size);
      int click_visual = start_visual + rel_visual_x;
      int click_x = visual_to_logical_col(clicked_line, click_visual, tab_size);
      click_x = std::clamp(click_x, 0, (int)clicked_line.length());
      Cursor clicked = {click_x, click_y};
      if (!cursor_in_selection(buf.selection, clicked)) {
        buf.cursor = clicked;
        buf.preferred_x = buf.cursor.x;
        buf.selection = {clicked, clicked, false};
        ensure_cursor_visible();
      }
      context_menu_target_buffer = pane.buffer_id;
      std::vector<ContextMenuItem> items = {
          {"Copy", CONTEXT_ACTION_COPY, true},
          {"Cut", CONTEXT_ACTION_CUT, true},
          {"Paste", CONTEXT_ACTION_PASTE, !clipboard.empty()},
      };
      focus_state = FOCUS_EDITOR;
      open_context_menu(x, y, CONTEXT_MENU_EDITOR, items);
      return true;
    }
  }

  return false;
}

void Editor::handle_mouse(void *event_ptr) {
  MEVENT *event = (MEVENT *)event_ptr;
  if (panes.empty())
    return;

  int bstate = event->bstate;

  if (bstate == 4)
    return;

  bool is_click = (bstate == 1);
  bool is_click_release = (bstate == 2);
  bool is_right_click = (bstate == 3);
  bool is_primary_click = is_click || is_click_release;

  if (show_context_menu && is_click) {
    if (handle_context_menu_mouse(event->x, event->y, true)) {
      return;
    }
  }

  if (is_right_click) {
    if (mouse_selecting) {
      mouse_selecting = false;
      mouse_drag_started = false;
    }
    if (!open_context_menu_for_mouse(event->x, event->y)) {
      close_context_menu();
    }
    return;
  }

  if (pane_resize_dragging) {
    if (bstate == 32) {
      update_pane_resize_drag(event->x, event->y);
      return;
    }
    if (is_click_release) {
      end_pane_resize_drag();
      return;
    }
    return;
  }

  if (is_click && begin_pane_resize_drag(event->x, event->y)) {
    return;
  }

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
      FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
      for (const auto &tab : tabs.segments) {
        if (event->x == tab.close_x) {
          close_buffer_at(tab.buffer_id);
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }

        if (event->x >= tab.x && event->x < tab.close_x) {
          pane.buffer_id = tab.buffer_id;
          current_buffer = tab.buffer_id;
          if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                        tab.buffer_id) == pane.tab_buffer_ids.end()) {
            pane.tab_buffer_ids.push_back(tab.buffer_id);
          }
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }
      }
    }
  }

  if (show_minimap && is_click) {
    if (event->x >= pane.x + pane.w - minimap_width &&
        event->x < pane.x + pane.w) {
      if (event->y >= pane.y + tab_height && event->y < pane.y + pane.h) {
        int rel_y = event->y - (pane.y + tab_height);
        int h = pane.h - tab_height;
        int total_lines = buf.line_count();
        if (total_lines > 0) {
          float ratio = (float)h / total_lines;
          if (ratio > 1.0f)
            ratio = 1.0f;
          int target_line = (int)(rel_y / ratio);
          buf.scroll_offset = target_line;
          if (buf.scroll_offset < 0)
            buf.scroll_offset = 0;
          if (buf.scroll_offset > (int)buf.line_count() - 1)
            buf.scroll_offset = (int)buf.line_count() - 1;
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
  const int content_bottom = pane.y + pane.h - 1;

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
  int visible_rows = std::max(1, pane.h - tab_height - 1);
  int max_scroll_offset =
      std::max(0, (int)buf.line_count() - visible_rows);

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
  if (click_y >= (int)buf.line_count())
    click_y = buf.line_count() - 1;
  if (click_y < 0)
    return;
  const std::string &clicked_line = buf.line(click_y);
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
    const std::string &line = buf.line(current_pos.y);
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
    Cursor current_end = {(int)buf.line(current_pos.y).length(), current_pos.y};

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
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
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
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
    if (x < 0 || x >= (int)line.size())
      return false;

    char open = 0, close = 0;
    bool is_open = false;
    if (!bracket_pair(line[x], open, close, is_open))
      return false;

    if (is_open) {
      int depth = 1;
      for (int y = line_y; y < (int)buf.line_count(); y++) {
        int start_x = (y == line_y) ? x + 1 : 0;
        for (int cx = start_x; cx < (int)buf.line(y).size(); cx++) {
          char ch = buf.line(y)[cx];
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
        int start_x = (y == line_y) ? x - 1 : (int)buf.line(y).size() - 1;
        for (int cx = start_x; cx >= 0; cx--) {
          char ch = buf.line(y)[cx];
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

    mouse_press_screen_x = event->x;
    mouse_press_screen_y = event->y;
    mouse_press_buf_x = click_x;
    mouse_press_buf_y = click_y;
    mouse_drag_started = false;

    if (last_left_click_count >= 3) {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_LINE;
      mouse_start = {0, click_y};
      mouse_anchor_end = {line_len, click_y};
      set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      needs_redraw = true;
      mouse_debug_log("press count=3 buf=(%d,%d) mode=LINE", click_x, click_y);
    } else if (last_left_click_count == 2) {
      const std::string &line = buf.line(click_y);
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
          mouse_debug_log("press count=2 bracket buf=(%d,%d)", click_x, click_y);
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
      mouse_debug_log("press count=2 word buf=(%d,%d) mode=WORD", click_x, click_y);
    } else {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_CHAR;
      mouse_start = {click_x, click_y};
      mouse_anchor_end = mouse_start;
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;
      buf.selection.start = mouse_start;
      buf.selection.end = {click_x, click_y};
      buf.selection.active = false;
      needs_redraw = true;
      mouse_debug_log("press count=1 buf=(%d,%d) mode=CHAR", click_x, click_y);
    }
  } else if (bstate == 2) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    hide_lsp_completion();

    if (mouse_selecting) {
      if (mouse_drag_started) {
        buf.cursor.x = click_x;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;

        if (mouse_selection_mode == MOUSE_SELECT_WORD) {
          set_word_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
        } else if (mouse_selection_mode == MOUSE_SELECT_LINE) {
          set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
        } else {
          buf.selection.end = {click_x, click_y};
          buf.selection.active =
              !(buf.selection.start.x == buf.selection.end.x &&
                buf.selection.start.y == buf.selection.end.y);
        }
        mouse_debug_log("release with-drag buf=(%d,%d) mode=%d",
                        click_x, click_y, (int)mouse_selection_mode);
      } else {
        buf.cursor.x = mouse_press_buf_x;
        buf.cursor.y = mouse_press_buf_y;
        buf.preferred_x = buf.cursor.x;
        if (mouse_selection_mode == MOUSE_SELECT_CHAR) {
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_start;
          buf.selection.active = false;
        } else if (mouse_selection_mode == MOUSE_SELECT_WORD ||
                   mouse_selection_mode == MOUSE_SELECT_LINE) {
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_anchor_end;
          buf.selection.active =
              !(buf.selection.start.x == buf.selection.end.x &&
                buf.selection.start.y == buf.selection.end.y);
        }
        mouse_debug_log("release no-drag restoring press buf=(%d,%d)",
                        buf.cursor.x, buf.cursor.y);
      }
      mouse_selecting = false;
      mouse_drag_started = false;
      needs_redraw = true;
    } else {
      mouse_debug_log("release stray (mouse_selecting=false) buf=(%d,%d)",
                      click_x, click_y);
    }
  } else if (bstate == 32) {
    idle_frame_count = 0;
    cursor_visible = true;
    cursor_blink_frame = 0;
    hide_lsp_completion();
    if (mouse_selecting) {
      int dx = event->x - mouse_press_screen_x;
      int dy = event->y - mouse_press_screen_y;
      if (!mouse_drag_started) {
        if (std::abs(dx) > kMouseDragThreshold ||
            std::abs(dy) > kMouseDragThreshold) {
          mouse_drag_started = true;
        } else {
          mouse_debug_log("motion under-threshold (dx=%d dy=%d) ignored",
                          dx, dy);
          return;
        }
      }

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
      mouse_debug_log("motion drag buf=(%d,%d) drag_started=1",
                      click_x, click_y);
    }
  }

  clamp_cursor(pane.buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible(bstate != 32);
}
