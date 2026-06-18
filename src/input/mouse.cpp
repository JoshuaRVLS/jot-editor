#include "editor.h"
#include "folding.h"
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

struct ScrollbarGeometry {
  bool visible = false;
  int x = 0;
  int y = 0;
  int h = 0;
  int thumb_y = 0;
  int thumb_h = 0;
  int max_scroll = 0;
};

struct TelescopeMouseLayout {
  bool valid = false;
  bool show_preview = false;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int inner_x = 0;
  int inner_y = 0;
  int inner_w = 0;
  int inner_h = 0;
  int body_y = 0;
  int body_h = 0;
  int list_x = 0;
  int list_y = 0;
  int list_w = 0;
  int list_h = 0;
  int preview_x = 0;
  int preview_y = 0;
  int preview_w = 0;
  int preview_h = 0;
};

TelescopeMouseLayout telescope_mouse_layout_for(UI *ui, int tab_height,
                                                int status_height,
                                                bool show_sidebar,
                                                int sidebar_w) {
  TelescopeMouseLayout layout;
  int h = ui->get_height();
  int w = ui->get_render_width();
  if (w < 4 || h < 5) {
    return layout;
  }
  int content_x = show_sidebar ? std::max(0, sidebar_w) : 0;
  int content_w = std::max(1, w - content_x);
  int top_bound = std::max(0, tab_height + 1);
  int bottom_bound = std::max(top_bound + 1, h - status_height - 1);
  int usable_h = std::max(1, bottom_bound - top_bound);

  layout.w = std::clamp(content_w - 2, std::min(content_w, 42), content_w);
  if (content_w >= 72) {
    layout.w = std::min(content_w - 2, std::max(72, content_w * 9 / 10));
  }
  layout.h = std::clamp(usable_h - 1, std::min(usable_h, 10), usable_h);
  if (usable_h >= 18) {
    layout.h = std::min(usable_h - 1, std::max(16, usable_h * 5 / 6));
  }

  layout.x = content_x + std::max(0, (content_w - layout.w + 1) / 2);
  layout.x = std::clamp(layout.x, content_x,
                        std::max(content_x, content_x + content_w - layout.w));
  layout.y = top_bound + std::max(0, (usable_h - layout.h) / 2);
  layout.inner_x = layout.x + 1;
  layout.inner_y = layout.y + 1;
  layout.inner_w = std::max(1, layout.w - 2);
  layout.inner_h = std::max(1, layout.h - 2);
  int footer_y = layout.y + layout.h - 2;
  layout.body_y = layout.inner_y + 4;
  layout.body_h = std::max(1, footer_y - layout.body_y);
  layout.show_preview = layout.inner_w >= 64 && layout.body_h >= 4;
  int list_panel_w =
      layout.show_preview ? std::max(26, layout.inner_w * 42 / 100)
                          : layout.inner_w;
  layout.list_x = layout.inner_x + 1;
  layout.list_y = layout.body_y;
  layout.list_w =
      layout.show_preview ? std::max(1, list_panel_w - 2)
                          : std::max(1, layout.inner_w - 2);
  layout.list_h = layout.body_h;
  if (layout.show_preview) {
    layout.preview_x = layout.inner_x + list_panel_w + 2;
    layout.preview_y = layout.body_y;
    layout.preview_w =
        std::max(1, layout.inner_x + layout.inner_w - layout.preview_x - 1);
    layout.preview_h = layout.body_h;
  }
  layout.valid = true;
  return layout;
}

ScrollbarGeometry scrollbar_geometry_for(const SplitPane &pane,
                                         const FileBuffer &buf,
                                         int tab_height,
                                         bool show_minimap,
                                         int minimap_width) {
  ScrollbarGeometry g;
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  if (draw_w < 3) {
    return g;
  }

  g.x = pane.x + draw_w - 1;
  g.y = pane.y + tab_height;
  g.h = std::max(0, pane.h - tab_height - 1);
  if (g.h <= 0) {
    return g;
  }

  const int total_lines =
      Folding::visible_line_count(buf.fold_ranges, (int)buf.line_count());
  const int visible_lines = std::max(1, g.h);
  g.max_scroll = std::max(0, total_lines - visible_lines);
  if (g.max_scroll <= 0) {
    return g;
  }

  g.thumb_h = std::max(1, (visible_lines * visible_lines) / total_lines);
  g.thumb_h = std::min(g.h, g.thumb_h);
  g.thumb_y = g.y;
  if (g.h > g.thumb_h) {
    int visible_scroll = 0;
    for (int line = 0; line < (int)buf.line_count() &&
                       line < buf.scroll_offset;
         line++) {
      if (!Folding::is_line_hidden(buf.fold_ranges, line)) {
        visible_scroll++;
      }
    }
    visible_scroll = std::clamp(visible_scroll, 0, g.max_scroll);
    g.thumb_y = g.y + (visible_scroll * (g.h - g.thumb_h)) / g.max_scroll;
  }
  g.visible = true;
  return g;
}

int scrollbar_scroll_for_y(const ScrollbarGeometry &g, int y) {
  if (!g.visible || g.max_scroll <= 0) {
    return 0;
  }
  const int travel = std::max(1, g.h - g.thumb_h);
  const int rel = std::clamp(y - g.y - g.thumb_h / 2, 0, travel);
  return std::clamp((rel * g.max_scroll + travel / 2) / travel, 0,
                    g.max_scroll);
}

int buffer_line_for_visible_index(const FileBuffer &buf, int visible_index) {
  int target = std::max(0, visible_index);
  for (int line = 0; line < (int)buf.line_count(); line++) {
    if (Folding::is_line_hidden(buf.fold_ranges, line)) {
      continue;
    }
    if (target == 0) {
      return line;
    }
    target--;
  }
  return std::max(0, (int)buf.line_count() - 1);
}
} // namespace

// Local definition of MEVENT used by handle_mouse()
struct MEVENT {
  int x, y;
  int bstate;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

static bool is_word_char(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

static bool is_operator_char(unsigned char c) {
  switch (c) {
  case '=':
  case '!':
  case '<':
  case '>':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '&':
  case '|':
  case '^':
  case '~':
  case '?':
  case ':':
    return true;
  default:
    return false;
  }
}

static bool is_pathish_run_char(unsigned char c) {
  return !std::isspace(c) && c != '"' && c != '\'' && c != '`' && c != '<' &&
         c != '>' && c != '(' && c != ')' && c != '[' && c != ']' &&
         c != '{' && c != '}';
}

static bool is_identifier_chain_char(const std::string &line, int i) {
  if (i < 0 || i >= (int)line.size())
    return false;
  unsigned char c = (unsigned char)line[i];
  if (is_word_char(c))
    return true;
  if (c == '.' || c == '-') {
    return i > 0 && i + 1 < (int)line.size() &&
           is_word_char((unsigned char)line[i - 1]) &&
           is_word_char((unsigned char)line[i + 1]);
  }
  if (c == ':') {
    bool left_scope =
        i > 0 && line[i - 1] == ':' && i + 1 < (int)line.size() &&
        is_word_char((unsigned char)line[i + 1]);
    bool right_scope =
        i + 1 < (int)line.size() && line[i + 1] == ':' && i > 0 &&
        is_word_char((unsigned char)line[i - 1]);
    return left_scope || right_scope;
  }
  return false;
}

static bool find_smart_token_span(const std::string &line, int x, int &start,
                                  int &end) {
  if (line.empty())
    return false;

  int pivot = std::clamp(x, 0, (int)line.size() - 1);
  if (std::isspace((unsigned char)line[pivot])) {
    start = pivot;
    end = pivot + 1;
    while (start > 0 && std::isspace((unsigned char)line[start - 1]))
      start--;
    while (end < (int)line.size() &&
           std::isspace((unsigned char)line[end]))
      end++;
    return true;
  }

  int run_start = pivot;
  int run_end = pivot + 1;
  while (run_start > 0 &&
         is_pathish_run_char((unsigned char)line[run_start - 1]))
    run_start--;
  while (run_end < (int)line.size() &&
         is_pathish_run_char((unsigned char)line[run_end]))
    run_end++;

  bool path_like = false;
  for (int i = run_start; i < run_end; i++) {
    if (line[i] == '/' || line[i] == '\\') {
      path_like = true;
      break;
    }
  }
  if (!path_like && run_start < run_end && line[run_start] == '~')
    path_like = true;
  if (!path_like) {
    for (int i = run_start; i + 2 < run_end; i++) {
      if (line[i] == ':' && line[i + 1] == '/' && line[i + 2] == '/') {
        path_like = true;
        break;
      }
    }
  }
  if (path_like) {
    start = run_start;
    end = run_end;
    while (end > start && (line[end - 1] == ',' || line[end - 1] == ';'))
      end--;
    return start < end;
  }

  if (is_identifier_chain_char(line, pivot) ||
      (pivot > 0 && is_identifier_chain_char(line, pivot - 1))) {
    int p = is_identifier_chain_char(line, pivot) ? pivot : pivot - 1;
    start = p;
    end = p + 1;
    while (start > 0 && is_identifier_chain_char(line, start - 1))
      start--;
    while (end < (int)line.size() && is_identifier_chain_char(line, end))
      end++;
    while (start < end && !is_word_char((unsigned char)line[start]))
      start++;
    while (end > start && !is_word_char((unsigned char)line[end - 1]))
      end--;
    return start < end;
  }

  if (is_operator_char((unsigned char)line[pivot])) {
    start = pivot;
    end = pivot + 1;
    while (start > 0 && is_operator_char((unsigned char)line[start - 1]))
      start--;
    while (end < (int)line.size() &&
           is_operator_char((unsigned char)line[end]))
      end++;
    return true;
  }

  start = pivot;
  end = pivot + 1;
  return true;
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
  if (is_click || is_scroll_up || is_scroll_down) {
    clear_debugger_breakpoint_hover();
  }

  if (is_click && handle_menu_bar_mouse(x, y, true, false)) {
    return;
  }

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

  if (show_tree_sitter_status_modal) {
    if (is_scroll_up) {
      tree_sitter_status_scroll = std::max(0, tree_sitter_status_scroll - 3);
      needs_redraw = true;
      return;
    }
    if (is_scroll_down) {
      tree_sitter_status_scroll += 3;
      needs_redraw = true;
      return;
    }
    if (is_click) {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54) {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16) {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = x >= modal_x && x < modal_x + modal_w &&
                    y >= modal_y && y < modal_y + modal_h;
      if (!inside) {
        show_tree_sitter_status_modal = false;
      }
      needs_redraw = true;
      return;
    }
    return;
  }

  if (is_click && begin_right_panel_resize_drag(x, y)) {
    return;
  }
  
  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT_DIFF && ui) {
    int panel_w = effective_sidebar_width();
    int panel_x = std::max(0, ui->get_render_width() - panel_w);
    int panel_y = 1;
    int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
    bool inside = x >= panel_x && x < panel_x + panel_w && y >= panel_y && y < panel_y + panel_h;
    if (inside) {
      if (is_scroll_up) {
        scroll_git_diff_panel(-3);
      } else if (is_scroll_down) {
        scroll_git_diff_panel(3);
      } else if (is_click) {
        needs_redraw = true;
      }
      return;
    }
  }

  if (handle_debugger_mouse(x, y, is_click)) {
    return;
  }

  if ((is_scroll_up || is_scroll_down) &&
      handle_integrated_terminal_scroll(x, y, is_scroll_up, is_scroll_down)) {
    return;
  }

  if ((is_scroll_up || is_scroll_down) && !panes.empty()) {
    int pane_index = -1;
    for (int i = 0; i < (int)panes.size(); i++) {
      const auto &candidate = panes[i];
      if (x >= candidate.x && x < candidate.x + candidate.w &&
          y == candidate.y) {
        pane_index = i;
        break;
      }
    }
    if (pane_index >= 0) {
      if (pane_index != current_pane) {
        panes[current_pane].active = false;
        current_pane = pane_index;
        panes[current_pane].active = true;
        current_buffer = panes[current_pane].buffer_id;
      }
      auto &pane = get_pane(current_pane);
      int draw_w = std::max(1, pane.w);
      if (show_minimap && draw_w > 20) {
        draw_w = std::max(1, draw_w - minimap_width);
      }
      FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
      if (x >= tabs.x && x < tabs.x + tabs.w) {
        if (scroll_local_tabs(pane, is_scroll_up ? -1 : 1)) {
          needs_redraw = true;
        }
        return;
      }
    }
  }

  if (show_sidebar) {
    int sidebar_w = effective_sidebar_width();
    if (x < sidebar_w) {
      if (is_scroll_up) {
        if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
          if (git_sidebar_scroll > 0)
            git_sidebar_scroll--;
        } else if (file_tree_scroll > 0) {
          file_tree_scroll--;
        }
        needs_redraw = true;
      } else if (is_scroll_down) {
        if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
          git_sidebar_scroll++;
          int reserved_terminal_h = 0;
          if (show_integrated_terminal && !integrated_terminals.empty()) {
            reserved_terminal_h =
                std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
          }
          int view_h = std::max(1, ui->get_height() - status_height -
                                       tab_height - reserved_terminal_h - 2);
          int max_scroll = std::max(
              0, (int)build_git_sidebar_rows().size() - view_h);
          git_sidebar_scroll = std::clamp(git_sidebar_scroll, 0, max_scroll);
        } else {
          file_tree_scroll++;
        }
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
  refresh_folds(buf);
  refresh_folds(buf);
  int visible_rows = std::max(1, pane.h - tab_height - 1);
  int max_scroll_offset =
      std::max(0, Folding::visible_line_count(buf.fold_ranges,
                                              (int)buf.line_count()) -
                      visible_rows);

  if (is_scroll_up) {
    int wheel_step = std::max(1, std::min(5, visible_rows / 6));
    for (int i = 0; i < wheel_step && buf.scroll_offset > 0; i++) {
      int prev = Folding::previous_visible_line(buf.fold_ranges,
                                                buf.scroll_offset);
      if (prev == buf.scroll_offset) {
        break;
      }
      buf.scroll_offset = prev;
      needs_redraw = true;
    }
    return;
  }
  if (is_scroll_down) {
    int wheel_step = std::max(1, std::min(5, visible_rows / 6));
    int current_visible = 0;
    for (int line = 0; line < (int)buf.line_count() &&
                       line < buf.scroll_offset;
         line++) {
      if (!Folding::is_line_hidden(buf.fold_ranges, line)) {
        current_visible++;
      }
    }
    int target_visible = std::min(max_scroll_offset,
                                  current_visible + wheel_step);
    int next = buffer_line_for_visible_index(buf, target_visible);
    if (next != buf.scroll_offset) {
      buf.scroll_offset = next;
      needs_redraw = true;
    }
    return;
  }
}

bool Editor::handle_telescope_mouse(int x, int y, bool is_click,
                                    bool is_double_click, bool is_scroll_up,
                                    bool is_scroll_down) {
  if (!telescope.is_active()) {
    return false;
  }

  TelescopeMouseLayout layout = telescope_mouse_layout_for(
      ui, tab_height, status_height, show_sidebar,
      show_sidebar ? effective_sidebar_width() : 0);
  if (!layout.valid) {
    return false;
  }

  bool inside = x >= layout.x && x < layout.x + layout.w && y >= layout.y &&
                y < layout.y + layout.h;
  if (!inside) {
    return true;
  }

  bool in_list = x >= layout.list_x - 1 &&
                 x < layout.list_x + layout.list_w + 1 &&
                 y >= layout.list_y && y < layout.list_y + layout.list_h;
  bool in_preview = layout.show_preview && x >= layout.preview_x &&
                    x < layout.preview_x + layout.preview_w &&
                    y >= layout.preview_y &&
                    y < layout.preview_y + layout.preview_h;

  if (is_scroll_up || is_scroll_down) {
    int delta = is_scroll_down ? 3 : -3;
    if (in_preview) {
      int line_start_y = layout.preview_y + 4;
      int preview_lines_h =
          std::max(1, layout.preview_y + layout.preview_h - line_start_y);
      telescope.scroll_preview(delta, preview_lines_h);
    } else if (in_list || inside) {
      telescope.move_by(delta);
      telescope.ensure_selected_visible(layout.list_h);
    }
    needs_redraw = true;
    return true;
  }

  if (is_click && in_list) {
    int target = telescope.get_list_scroll_offset() + (y - layout.list_y);
    if (target >= 0 && target < telescope.get_result_count()) {
      telescope.select_index(target);
      telescope.ensure_selected_visible(layout.list_h);

      if (is_double_click) {
        accept_telescope_selection();
      }
      needs_redraw = true;
    }
    return true;
  }

  if (is_click) {
    needs_redraw = true;
  }
  return true;
}

bool Editor::open_context_menu_for_mouse(int x, int y) {
  if (show_home_menu || panes.empty()) {
    return false;
  }

  context_menu_target_buffer = -1;
  context_menu_target_pane = -1;
  context_menu_target_terminal = -1;
  context_menu_target_line = -1;
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
    int sidebar_w = effective_sidebar_width();
    if (x < sidebar_w && y >= tab_height && y < content_bottom) {
      focus_state = FOCUS_SIDEBAR;
      if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
        std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
        int sidebar_row = y - tab_height - 1;
        int row = sidebar_row + git_sidebar_scroll;
        if (sidebar_row >= 0 && row >= 0 && row < (int)git_rows.size()) {
          git_sidebar_selected = row;
          context_menu_target_path = git_rows[(size_t)row].path;
          context_menu_target_is_dir = false;
        }
        bool has_target = !context_menu_target_path.empty();
        bool has_repo = has_git_repo();
        std::vector<ContextMenuItem> items = {
            {"Open", CONTEXT_ACTION_SIDEBAR_OPEN, has_target},
            {"Git Stage", CONTEXT_ACTION_GIT_STAGE, has_target && has_repo},
            {"Git Unstage", CONTEXT_ACTION_GIT_UNSTAGE, has_target && has_repo},
            {"Git Diff", CONTEXT_ACTION_GIT_DIFF, has_target && has_repo},
            {"Git Diff Staged", CONTEXT_ACTION_GIT_DIFF_STAGED,
             has_target && has_repo},
            {"Git Stage All", CONTEXT_ACTION_GIT_STAGE_ALL, has_repo},
            {"Refresh", CONTEXT_ACTION_GIT_REFRESH, true},
            {"Copy Path", CONTEXT_ACTION_SIDEBAR_COPY_PATH, has_target},
        };
        open_context_menu(x, y, CONTEXT_MENU_SIDEBAR, items);
      } else {
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
        bool git_target = has_target && has_git_repo();
        std::vector<ContextMenuItem> items = {
            {open_label, CONTEXT_ACTION_SIDEBAR_OPEN, node != nullptr},
            {"New File", CONTEXT_ACTION_SIDEBAR_NEW_FILE, has_target},
            {"New Folder", CONTEXT_ACTION_SIDEBAR_NEW_FOLDER, has_target},
            {"Rename", CONTEXT_ACTION_SIDEBAR_RENAME, node != nullptr},
            {"Git Stage", CONTEXT_ACTION_GIT_STAGE, git_target},
            {"Git Unstage", CONTEXT_ACTION_GIT_UNSTAGE, git_target},
            {"Git Diff", CONTEXT_ACTION_GIT_DIFF, git_target},
            {"Git Diff Staged", CONTEXT_ACTION_GIT_DIFF_STAGED, git_target},
            {"Refresh", CONTEXT_ACTION_SIDEBAR_REFRESH, true},
            {"Copy Path", CONTEXT_ACTION_SIDEBAR_COPY_PATH, has_target},
        };
        open_context_menu(x, y, CONTEXT_MENU_SIDEBAR, items);
      }
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

  const int line_num_width = 8;
  const int code_start_x = pane.x + 1 + line_num_width;
  const int content_top = pane.y + tab_height;
  const int visible_rows = std::max(1, pane.h - tab_height - 1);
  if (y >= content_top && y < content_top + visible_rows) {
    int rel_y = std::clamp(y - content_top, 0, visible_rows - 1);
    refresh_folds(buf);
    int click_y = buffer_line_for_visible_row(buf, buf.scroll_offset, rel_y);
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
      context_menu_target_line = click_y;
      std::vector<ContextMenuItem> items = {
          {"Copy", CONTEXT_ACTION_COPY, true},
          {"Cut", CONTEXT_ACTION_CUT, true},
          {"Paste", CONTEXT_ACTION_PASTE, !clipboard.empty()},
      };
      if (Folding::fold_at_or_before_line(buf.fold_ranges, click_y) >= 0) {
        items.push_back({"Toggle Fold", CONTEXT_ACTION_TOGGLE_FOLD, true});
      }
      focus_state = FOCUS_EDITOR;
      open_context_menu(x, y, CONTEXT_MENU_EDITOR, items);
      return true;
    }
  }

  return false;
}

bool Editor::handle_menu_bar_mouse(int x, int y, bool is_click,
                                   bool is_motion) {
  if (!ui) {
    return false;
  }

  auto hit_menu_label = [&](int mx) -> int {
    for (const auto &segment : menu_bar_segments) {
      if (mx >= segment.x && mx < segment.end_x) {
        return segment.menu_index;
      }
    }
    return -1;
  };

  if (y == 0) {
    int hit = hit_menu_label(x);
    if (hit >= 0) {
      if (is_click) {
        if (show_menu_bar_dropdown && menu_bar_active == hit) {
          close_menu_bar();
        } else {
          open_menu_bar(hit);
        }
      } else if (is_motion && show_menu_bar_dropdown &&
                 menu_bar_active != hit) {
        open_menu_bar(hit);
      }
      return true;
    }
    if (is_click && show_menu_bar_dropdown) {
      close_menu_bar();
      return true;
    }
    return y == 0;
  }

  if (!show_menu_bar_dropdown) {
    return false;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size()) {
    close_menu_bar();
    return true;
  }

  const auto &menu = menus[menu_bar_active];
  int label_x = 0;
  for (const auto &segment : menu_bar_segments) {
    if (segment.menu_index == menu_bar_active) {
      label_x = segment.x;
      break;
    }
  }
  int max_label = 0;
  for (const auto &item : menu.items) {
    max_label = std::max(max_label, (int)item.label.size());
  }
  int menu_w = std::max(18, max_label + 4);
  int menu_h = std::min((int)menu.items.size() + 2,
                        std::max(1, ui->get_height() - 1 - status_height));
  int menu_x =
      std::clamp(label_x, 0, std::max(0, ui->get_render_width() - menu_w));
  int menu_y = 1;

  bool inside = x >= menu_x && x < menu_x + menu_w && y >= menu_y &&
                y < menu_y + menu_h;
  if (!inside) {
    if (is_click) {
      close_menu_bar();
      return true;
    }
    return false;
  }

  int row = y - menu_y - 1;
  if (row >= 0 && row < (int)menu.items.size() && menu.items[row].enabled) {
    menu_bar_selected = row;
    needs_redraw = true;
    if (is_click) {
      execute_menu_bar_item(menu_bar_active, row);
    }
  }
  return true;
}

void Editor::handle_mouse(void *event_ptr) {
  MEVENT *event = (MEVENT *)event_ptr;
  if (panes.empty())
    return;

  int bstate = event->bstate;

  bool is_click = (bstate == 1);
  bool is_click_release = (bstate == 2);
  bool is_right_click = (bstate == 3);
  bool is_middle_click = (bstate == 4);
  bool is_motion = (bstate == 32);
  bool is_primary_click = is_click || is_click_release;

  if (is_click || is_click_release || is_right_click || is_middle_click ||
      is_motion) {
    if (!is_motion || mouse_selecting || mouse_drag_started) {
      cancel_lsp_mouse_hover();
    }
    if (!is_motion || mouse_selecting || mouse_drag_started) {
      clear_debugger_breakpoint_hover();
    }
  }

  if ((is_click || is_motion || is_right_click) &&
      handle_menu_bar_mouse(event->x, event->y, is_click, is_motion)) {
    if (is_motion) {
      clear_debugger_breakpoint_hover();
    }
    return;
  }

  if (show_context_menu && (is_click || is_motion)) {
    if (handle_context_menu_mouse(event->x, event->y, is_click)) {
      if (is_motion) {
        clear_debugger_breakpoint_hover();
      }
      return;
    }
    if (is_motion) {
      clear_debugger_breakpoint_hover();
      return;
    }
  }

  if (show_tree_sitter_status_modal &&
      (is_click || is_click_release || is_right_click || is_middle_click ||
       is_motion)) {
    if (is_click || is_right_click || is_middle_click) {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54) {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16) {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = event->x >= modal_x && event->x < modal_x + modal_w &&
                    event->y >= modal_y && event->y < modal_y + modal_h;
      if (!inside || is_right_click || is_middle_click) {
        show_tree_sitter_status_modal = false;
      }
      needs_redraw = true;
    }
    return;
  }

  if (is_right_click) {
    if (right_panel_resize_dragging) {
      end_right_panel_resize_drag();
      return;
    }
    if (sidebar_resize_dragging) {
      end_sidebar_resize_drag();
      return;
    }
    if (scrollbar_dragging) {
      scrollbar_dragging = false;
      scrollbar_drag_pane = -1;
    }
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

  if (right_panel_resize_dragging) {
    if (is_motion || is_click) {
      update_right_panel_resize_drag(event->x);
      return;
    }
    if (is_click_release) {
      end_right_panel_resize_drag();
      return;
    }
    return;
  }

  if (sidebar_resize_dragging) {
    if (is_motion || is_click) {
      update_sidebar_resize_drag(event->x);
      return;
    }
    if (is_click_release) {
      end_sidebar_resize_drag();
      return;
    }
    return;
  }

  if (scrollbar_dragging) {
    if (is_click_release) {
      scrollbar_dragging = false;
      scrollbar_drag_pane = -1;
      needs_redraw = true;
      return;
    }
    if (is_motion || is_click) {
      if (scrollbar_drag_pane >= 0 &&
          scrollbar_drag_pane < (int)panes.size()) {
        auto &drag_pane = panes[scrollbar_drag_pane];
        auto &drag_buf = get_buffer(drag_pane.buffer_id);
        const int travel =
            std::max(1, scrollbar_drag_track_h - scrollbar_drag_thumb_h);
        const int delta_y = event->y - scrollbar_drag_start_y;
        const int scaled = delta_y * scrollbar_drag_max_scroll;
        const int scroll_delta =
            scaled >= 0 ? (scaled + travel / 2) / travel
                        : (scaled - travel / 2) / travel;
        int start_visible = 0;
        for (int line = 0; line < (int)drag_buf.line_count() &&
                           line < scrollbar_drag_start_scroll;
             line++) {
          if (!Folding::is_line_hidden(drag_buf.fold_ranges, line)) {
            start_visible++;
          }
        }
        int visible_target =
            std::clamp(start_visible + scroll_delta, 0,
                       scrollbar_drag_max_scroll);
        drag_buf.scroll_offset =
            buffer_line_for_visible_index(drag_buf, visible_target);
        current_pane = scrollbar_drag_pane;
        current_buffer = drag_pane.buffer_id;
        for (int i = 0; i < (int)panes.size(); i++) {
          panes[i].active = (i == current_pane);
        }
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
      return;
    }
  }

  if (is_click && begin_sidebar_resize_drag(event->x, event->y)) {
    return;
  }

  if (is_click && begin_right_panel_resize_drag(event->x, event->y)) {
    return;
  }

  if ((is_click || is_click_release || is_motion) &&
      handle_debugger_mouse(event->x, event->y, is_click)) {
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
    int sidebar_w = effective_sidebar_width();
    if (event->x < sidebar_w && event->y >= tab_height &&
        event->y < content_bottom) {
      focus_state = FOCUS_SIDEBAR;
      long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
      int sidebar_scroll = active_sidebar_view == SIDEBAR_VIEW_GIT
                               ? git_sidebar_scroll
                               : file_tree_scroll;
      int sidebar_row = event->y - tab_height - 1 + sidebar_scroll;
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

  if ((is_click || is_middle_click) && target_pane != current_pane) {
    panes[current_pane].active = false;
    current_pane = target_pane;
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
  }

  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);
  refresh_folds(buf);

  if (is_click) {
    ScrollbarGeometry scrollbar =
        scrollbar_geometry_for(pane, buf, tab_height, show_minimap,
                               minimap_width);
    if (scrollbar.visible && event->x == scrollbar.x &&
        event->y >= scrollbar.y && event->y < scrollbar.y + scrollbar.h) {
      if (event->y < scrollbar.thumb_y ||
          event->y >= scrollbar.thumb_y + scrollbar.thumb_h) {
        buf.scroll_offset = buffer_line_for_visible_index(
            buf, scrollbar_scroll_for_y(scrollbar, event->y));
        scrollbar =
            scrollbar_geometry_for(pane, buf, tab_height, show_minimap,
                                   minimap_width);
      }
      scrollbar_dragging = true;
      scrollbar_drag_pane = current_pane;
      scrollbar_drag_start_y = event->y;
      scrollbar_drag_start_scroll = buf.scroll_offset;
      scrollbar_drag_track_y = scrollbar.y;
      scrollbar_drag_track_h = scrollbar.h;
      scrollbar_drag_thumb_h = scrollbar.thumb_h;
      scrollbar_drag_max_scroll = scrollbar.max_scroll;
      focus_state = FOCUS_EDITOR;
      mouse_selecting = false;
      mouse_drag_started = false;
      needs_redraw = true;
      return;
    }
  }

  if ((is_click || is_middle_click) && event->y == pane.y && !buffers.empty()) {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    int tabs_x = pane.x + 1;
    int tabs_w = std::max(1, draw_w - 2);
    if (event->x >= tabs_x && event->x < tabs_x + tabs_w) {
      FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
      int page = std::max(1, (int)tabs.segments.size());
      if (tabs.scroll_left_x >= 0 && event->x >= tabs.scroll_left_x &&
          event->x < tabs.scroll_left_end_x) {
        if (scroll_local_tabs(pane, -page)) {
          needs_redraw = true;
        }
        focus_state = FOCUS_EDITOR;
        return;
      }
      if (tabs.scroll_right_x >= 0 && event->x >= tabs.scroll_right_x &&
          event->x < tabs.scroll_right_end_x) {
        if (scroll_local_tabs(pane, page)) {
          needs_redraw = true;
        }
        focus_state = FOCUS_EDITOR;
        return;
      }
      for (const auto &tab : tabs.segments) {
        if (is_middle_click && event->x >= tab.x && event->x < tab.end_x) {
          close_buffer_at(tab.buffer_id);
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }

        if (is_click && event->x == tab.close_x) {
          close_buffer_at(tab.buffer_id);
          focus_state = FOCUS_EDITOR;
          needs_redraw = true;
          return;
        }

        if (is_click && event->x >= tab.x && event->x < tab.close_x) {
          switch_to_local_tab(tab.tab_index);
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
      int h = std::max(1, pane.h - tab_height - 1);
      if (event->y >= pane.y + tab_height &&
          event->y < pane.y + tab_height + h) {
        int rel_y = event->y - (pane.y + tab_height);
        refresh_folds(buf);
        int total_lines =
            Folding::visible_line_count(buf.fold_ranges, (int)buf.line_count());
        if (total_lines > 0) {
          float ratio = (float)h / total_lines;
          if (ratio > 1.0f)
            ratio = 1.0f;
          int target_visible = (int)(rel_y / ratio);
          int max_scroll_offset = std::max(0, total_lines - h);
          target_visible = std::clamp(target_visible, 0, max_scroll_offset);
          buf.scroll_offset = buffer_line_for_visible_index(buf, target_visible);
          needs_redraw = true;
          return;
        }
      }
    }
  }

  // Global top tab bar is disabled; pane-local headers are rendered per pane.

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w &&
                      event->y >= pane.y && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting) {
    if (is_motion) {
      clear_debugger_breakpoint_hover();
    }
    return;
  }

  if (inside_pane && is_click)
    focus_state = FOCUS_EDITOR;

  const int line_num_width = 8;
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
      std::max(0, Folding::visible_line_count(buf.fold_ranges,
                                              (int)buf.line_count()) -
                      visible_rows);

  if (bstate == 32 && mouse_selecting) {
    if (raw_rel_y < 0) {
      int scroll_by = std::min(buf.scroll_offset, std::max(1, -raw_rel_y));
      buf.scroll_offset -= scroll_by;
    } else if (raw_rel_y >= visible_rows) {
      int current_visible = 0;
      for (int line = 0; line < (int)buf.line_count() &&
                         line < buf.scroll_offset;
           line++) {
        if (!Folding::is_line_hidden(buf.fold_ranges, line)) {
          current_visible++;
        }
      }
      int scroll_by =
          std::min(max_scroll_offset - current_visible,
                   std::max(1, raw_rel_y - visible_rows + 1));
      buf.scroll_offset =
          buffer_line_for_visible_index(buf, current_visible + scroll_by);
    }
  }

  rel_y = std::clamp(rel_y, 0, visible_rows - 1);

  int click_y = buffer_line_for_visible_row(buf, buf.scroll_offset, rel_y);
  if (click_y < 0)
    click_y = 0;
  if (click_y >= (int)buf.line_count())
    click_y = buf.line_count() - 1;
  if (click_y < 0)
    return;
  if (is_motion && !mouse_selecting && !mouse_drag_started && inside_pane &&
      event->y >= content_top && event->y < content_bottom &&
      event->x == pane.x + 1 && !buf.filepath.empty()) {
    update_debugger_breakpoint_hover(current_pane, pane.buffer_id, click_y);
    cancel_lsp_mouse_hover();
    return;
  } else if (is_motion) {
    clear_debugger_breakpoint_hover();
  }
  if (is_click && event->x == pane.x + 1 && !buf.filepath.empty() &&
      toggle_debugger_breakpoint(buf.filepath, click_y)) {
    clear_debugger_breakpoint_hover();
    focus_state = FOCUS_EDITOR;
    return;
  }
  if (is_click && event->x == pane.x + 2 &&
      toggle_fold_at_line(buf, click_y)) {
    focus_state = FOCUS_EDITOR;
    return;
  }
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
      int start = 0;
      int end = 0;
      if (find_smart_token_span(line, current_pos.x, start, end)) {
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
        true;
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
    if (find_smart_token_span(line, pivot, start, end))
      return true;
    if (pivot + 1 < (int)line.size() &&
        find_smart_token_span(line, pivot + 1, start, end))
      return true;
    if (pivot - 1 >= 0 && find_smart_token_span(line, pivot - 1, start, end))
      return true;

    for (int d = 2; d < (int)line.size(); d++) {
      int right = pivot + d;
      int left = pivot - d;
      if (right < (int)line.size() &&
          find_smart_token_span(line, right, start, end))
        return true;
      if (left >= 0 && find_smart_token_span(line, left, start, end))
        return true;
      if (right >= (int)line.size() && left < 0)
        break;
    }

    return false;
  };

  auto word_span_at_exact = [&](int line_y, int x, int &start,
                                int &end) -> bool {
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
    if (line.empty())
      return false;
    int pivot = std::clamp(x, 0, std::max(0, (int)line.size() - 1));
    if (!find_smart_token_span(line, pivot, start, end))
      return false;
    if (x < start || x >= end)
      return false;
    for (int i = start; i < end && i < (int)line.size(); i++) {
      if (is_word_char((unsigned char)line[i]))
        return true;
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

  int hover_token_start = -1;
  int hover_token_end = -1;
  if (is_motion && !mouse_selecting && !mouse_drag_started && inside_pane &&
      event->y >= content_top && event->y < content_bottom &&
      event->x >= code_start_x &&
      word_span_at_exact(click_y, click_x, hover_token_start,
                         hover_token_end)) {
    request_lsp_hover_at(current_pane, pane.buffer_id, {click_x, click_y},
                         hover_token_start, hover_token_end, event->x,
                         event->y);
    return;
  } else if (is_motion && !mouse_selecting && !mouse_drag_started) {
    cancel_lsp_mouse_hover();
    return;
  }

  if (is_click && event->ctrl && inside_pane && event->y >= content_top &&
      event->y < content_bottom && event->x >= code_start_x) {
    int token_start = -1;
    int token_end = -1;
    if (word_span_at_exact(click_y, click_x, token_start, token_end)) {
      focus_state = FOCUS_EDITOR;
      buf.cursor.x = std::clamp(click_x, token_start, token_end);
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;
      buf.selection.start = buf.cursor;
      buf.selection.end = buf.cursor;
      buf.selection.active = false;
      mouse_selecting = false;
      mouse_drag_started = false;
      request_lsp_definition();
      needs_redraw = true;
      return;
    }
  }

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
        buf.selection.active = true;
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
              mouse_selection_mode == MOUSE_SELECT_LINE ||
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
