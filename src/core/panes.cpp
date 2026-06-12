#include "editor.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>

namespace {
int pane_center_x(const SplitPane &pane) { return pane.x + pane.w / 2; }

int pane_center_y(const SplitPane &pane) { return pane.y + pane.h / 2; }

int overlap_amount(int a_start, int a_len, int b_start, int b_len) {
  int a_end = a_start + a_len;
  int b_end = b_start + b_len;
  return std::max(0, std::min(a_end, b_end) - std::max(a_start, b_start));
}

constexpr int kMinPaneWidth = 12;
constexpr int kMinPaneHeight = 3;
} // namespace

int Editor::max_sidebar_width() const {
  if (!ui) {
    return sidebar_width;
  }
  return std::max(min_sidebar_width(), ui->get_render_width() - 20);
}

int Editor::effective_sidebar_width() const {
  return std::clamp(sidebar_width, min_sidebar_width(), max_sidebar_width());
}

int Editor::effective_right_panel_width() const {
  if (!ui || !show_right_panel) {
    return 0;
  }
  int max_w = max_right_panel_width();
  if (max_w < min_right_panel_width()) {
    return std::max(0, max_w);
  }
  return std::clamp(right_panel_width, min_right_panel_width(), max_w);
}

int Editor::max_right_panel_width() const {
  if (!ui) {
    return std::max(min_right_panel_width(), right_panel_width);
  }
  int total_w = std::max(1, ui->get_render_width());
  int left_w = show_sidebar ? effective_sidebar_width() : 0;
  return std::max(0, total_w - left_w - kMinPaneWidth);
}

bool Editor::collapsed_sidebar_handle_hit_test(int x, int y) const {
  if (show_sidebar || show_home_menu || !ui || panes.empty()) {
    return false;
  }
  if (x != 0) {
    return false;
  }
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5,
                   std::max(5, ui->get_height() / 2));
  }
  int top = tab_height;
  int bottom = ui->get_height() - status_height - reserved_terminal_h;
  return y >= top && y < bottom;
}

bool Editor::sidebar_resize_hit_test(int x, int y) const {
  if (collapsed_sidebar_handle_hit_test(x, y)) {
    return true;
  }
  if (!show_sidebar || !ui) {
    return false;
  }
  int w = effective_sidebar_width();
  if (w < 2 || x != w - 1) {
    return false;
  }
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5,
                   std::max(5, ui->get_height() / 2));
  }
  int top = tab_height;
  int bottom = ui->get_height() - status_height - reserved_terminal_h;
  return y >= top && y < bottom;
}

int Editor::create_pane(int x, int y, int w, int h, int buffer_id) {
  SplitPane pane;
  pane.x = x;
  pane.y = y;
  pane.w = w;
  pane.h = h;
  pane.buffer_id = buffer_id == -1 ? std::max(0, (int)buffers.size() - 1) : buffer_id;
  pane.active = panes.empty();
  if (pane.buffer_id >= 0) {
    pane.tab_buffer_ids.push_back(pane.buffer_id);
  }
  panes.push_back(pane);

  if (panes.size() == 1) {
    pane_tree.clear();
    PaneTreeNode leaf;
    leaf.leaf = true;
    leaf.pane_index = 0;
    leaf.parent = -1;
    pane_tree.push_back(leaf);
    pane_root = 0;
  }

  return (int)panes.size() - 1;
}

void Editor::update_pane_layout() {
  if (panes.empty()) {
    pane_layout_mode = PANE_LAYOUT_SINGLE;
    return;
  }

  if (pane_root < 0 || pane_root >= (int)pane_tree.size()) {
    pane_tree.clear();
    PaneTreeNode leaf;
    leaf.leaf = true;
    leaf.pane_index = 0;
    leaf.parent = -1;
    pane_tree.push_back(leaf);
    pane_root = 0;
  }

  int total_w = std::max(1, ui->get_render_width());
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
  }
  int menu_h = 1;
  int total_h =
      std::max(1, ui->get_height() - status_height - reserved_terminal_h -
                      menu_h);
  int origin_x = show_sidebar ? effective_sidebar_width() : 0;
  int right_w = effective_right_panel_width();
  int available_w = std::max(1, total_w - origin_x - right_w);
  int origin_y = menu_h;

  std::function<void(int, int, int, int, int)> layout_node =
      [&](int node_index, int x, int y, int w, int h) {
        if (node_index < 0 || node_index >= (int)pane_tree.size()) {
          return;
        }

        PaneTreeNode &node = pane_tree[node_index];
        if (node.leaf) {
          if (node.pane_index < 0 || node.pane_index >= (int)panes.size()) {
            return;
          }
          panes[node.pane_index].x = x;
          panes[node.pane_index].y = y;
          panes[node.pane_index].w = std::max(1, w);
          panes[node.pane_index].h = std::max(1, h);
          return;
        }

        node.ratio = std::clamp(node.ratio, 0.1f, 0.9f);

        if (node.vertical) {
          int first_w = std::max(1, (int)(w * node.ratio));
          if (w >= 2) {
            first_w = std::min(first_w, w - 1);
          }
          int second_w = std::max(1, w - first_w);
          layout_node(node.first, x, y, first_w, h);
          layout_node(node.second, x + first_w, y, second_w, h);
        } else {
          int first_h = std::max(1, (int)(h * node.ratio));
          if (h >= 2) {
            first_h = std::min(first_h, h - 1);
          }
          int second_h = std::max(1, h - first_h);
          layout_node(node.first, x, y, w, first_h);
          layout_node(node.second, x, y + first_h, w, second_h);
        }
      };

  layout_node(pane_root, origin_x, origin_y, available_w, total_h);

  if (pane_root >= 0 && pane_root < (int)pane_tree.size() &&
      !pane_tree[pane_root].leaf) {
    pane_layout_mode = pane_tree[pane_root].vertical ? PANE_LAYOUT_VERTICAL
                                                     : PANE_LAYOUT_HORIZONTAL;
  } else {
    pane_layout_mode = PANE_LAYOUT_SINGLE;
  }

  if (current_pane < 0 || current_pane >= (int)panes.size()) {
    current_pane = 0;
  }
}

void Editor::split_pane_horizontal() { split_pane_down(); }

void Editor::split_pane_vertical() { split_pane_right(); }

void Editor::split_pane_left() { split_pane_direction(-1, 0); }

void Editor::split_pane_right() { split_pane_direction(1, 0); }

void Editor::split_pane_up() { split_pane_direction(0, -1); }

void Editor::split_pane_down() { split_pane_direction(0, 1); }

void Editor::split_pane_direction(int dx, int dy) {
  if (panes.empty()) {
    create_pane(0, 0, ui->get_render_width(), ui->get_height() - status_height,
                current_buffer);
  }

  if (current_pane < 0 || current_pane >= (int)panes.size()) {
    current_pane = 0;
  }

  std::function<int(int, int)> find_leaf = [&](int node_index,
                                                int pane_index) -> int {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return node.pane_index == pane_index ? node_index : -1;
    }
    int left = find_leaf(node.first, pane_index);
    if (left >= 0) {
      return left;
    }
    return find_leaf(node.second, pane_index);
  };

  int leaf_node = find_leaf(pane_root, current_pane);
  if (leaf_node < 0) {
    set_message("Pane split failed: invalid pane tree");
    return;
  }

  const int old_pane = pane_tree[leaf_node].pane_index;
  int new_buffer = panes[old_pane].buffer_id;
  if (new_buffer < 0 || new_buffer >= (int)buffers.size()) {
    FileBuffer fb;
    fb.lines.push_back("");
    fb.cursor = {0, 0};
    fb.preferred_x = 0;
    fb.selection = {{0, 0}, {0, 0}, false};
    fb.scroll_offset = 0;
    fb.scroll_x = 0;
    fb.modified = false;
    fb.is_preview = false;
    fb.is_placeholder = true;
    buffers.push_back(std::move(fb));
    new_buffer = (int)buffers.size() - 1;
  }

  SplitPane new_pane{};
  new_pane.x = 0;
  new_pane.y = 0;
  new_pane.w = 1;
  new_pane.h = 1;
  new_pane.buffer_id = new_buffer;
  new_pane.active = false;
  new_pane.tab_buffer_ids = panes[old_pane].tab_buffer_ids;
  if (std::find(new_pane.tab_buffer_ids.begin(),
                new_pane.tab_buffer_ids.end(),
                new_buffer) == new_pane.tab_buffer_ids.end()) {
    new_pane.tab_buffer_ids.push_back(new_buffer);
  }
  new_pane.tab_scroll_index = panes[old_pane].tab_scroll_index;
  panes.push_back(new_pane);
  int new_pane_index = (int)panes.size() - 1;

  PaneTreeNode old_leaf;
  old_leaf.leaf = true;
  old_leaf.pane_index = old_pane;
  old_leaf.parent = leaf_node;

  PaneTreeNode new_leaf;
  new_leaf.leaf = true;
  new_leaf.pane_index = new_pane_index;
  new_leaf.parent = leaf_node;

  int old_leaf_id = (int)pane_tree.size();
  pane_tree.push_back(old_leaf);
  int new_leaf_id = (int)pane_tree.size();
  pane_tree.push_back(new_leaf);

  PaneTreeNode &node = pane_tree[leaf_node];
  node.leaf = false;
  node.pane_index = -1;
  node.vertical = (dx != 0);
  node.ratio = 0.5f;

  bool new_first = (dx < 0 || dy < 0);
  if (new_first) {
    node.first = new_leaf_id;
    node.second = old_leaf_id;
  } else {
    node.first = old_leaf_id;
    node.second = new_leaf_id;
  }

  for (auto &pane : panes) {
    pane.active = false;
  }
  current_pane = new_pane_index;
  panes[current_pane].active = true;
  current_buffer = panes[current_pane].buffer_id;

  pane_layout_mode = node.vertical ? PANE_LAYOUT_VERTICAL : PANE_LAYOUT_HORIZONTAL;
  update_pane_layout();

  if (dx < 0) {
    message = "Split pane left";
  } else if (dx > 0) {
    message = "Split pane right";
  } else if (dy < 0) {
    message = "Split pane up";
  } else {
    message = "Split pane down";
  }
  needs_redraw = true;
}

void Editor::close_pane() {
  if (panes.size() <= 1) {
    message = "Can't close the last pane";
    return;
  }

  if (current_pane < 0 || current_pane >= (int)panes.size()) {
    current_pane = 0;
  }

  std::function<int(int, int)> find_leaf = [&](int node_index,
                                                int pane_index) -> int {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return node.pane_index == pane_index ? node_index : -1;
    }
    int left = find_leaf(node.first, pane_index);
    if (left >= 0) {
      return left;
    }
    return find_leaf(node.second, pane_index);
  };

  std::function<int(int)> first_pane_in_node = [&](int node_index) -> int {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return (node.pane_index >= 0 && node.pane_index < (int)panes.size())
                 ? node.pane_index
                 : -1;
    }
    int first = first_pane_in_node(node.first);
    if (first >= 0) {
      return first;
    }
    return first_pane_in_node(node.second);
  };

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0) {
    message = "Pane close failed: invalid pane tree";
    return;
  }

  int parent = pane_tree[leaf].parent;
  if (parent < 0 || parent >= (int)pane_tree.size()) {
    message = "Pane close failed: invalid parent";
    return;
  }

  int sibling = (pane_tree[parent].first == leaf) ? pane_tree[parent].second
                                                   : pane_tree[parent].first;
  int preferred_next = first_pane_in_node(sibling);
  int grand = pane_tree[parent].parent;

  if (grand >= 0 && grand < (int)pane_tree.size()) {
    if (pane_tree[grand].first == parent) {
      pane_tree[grand].first = sibling;
    } else {
      pane_tree[grand].second = sibling;
    }
    if (sibling >= 0 && sibling < (int)pane_tree.size()) {
      pane_tree[sibling].parent = grand;
    }
  } else {
    pane_root = sibling;
    if (pane_root >= 0 && pane_root < (int)pane_tree.size()) {
      pane_tree[pane_root].parent = -1;
    }
  }

  int removed = current_pane;
  panes.erase(panes.begin() + removed);

  for (auto &node : pane_tree) {
    if (node.leaf) {
      if (node.pane_index == removed) {
        node.pane_index = -1;
      } else if (node.pane_index > removed) {
        node.pane_index--;
      }
    }
  }

  int next = preferred_next;
  if (next > removed) {
    next--;
  }
  if (next < 0 || next >= (int)panes.size()) {
    next = first_pane_in_node(pane_root);
  }
  if (next < 0 && !panes.empty()) {
    next = 0;
  }

  for (auto &pane : panes) {
    pane.active = false;
  }

  current_pane = std::clamp(next, 0, std::max(0, (int)panes.size() - 1));
  panes[current_pane].active = true;
  current_buffer = panes[current_pane].buffer_id;

  update_pane_layout();
  message = "Pane closed";
  needs_redraw = true;
}

bool Editor::resize_current_pane(int delta) {
  if (panes.size() < 2 || current_pane < 0 ||
      current_pane >= (int)panes.size()) {
    return false;
  }

  std::function<int(int, int)> find_leaf = [&](int node_index,
                                                int pane_index) -> int {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return -1;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return node.pane_index == pane_index ? node_index : -1;
    }
    int first = find_leaf(node.first, pane_index);
    return first >= 0 ? first : find_leaf(node.second, pane_index);
  };

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0) {
    return false;
  }
  int parent = pane_tree[leaf].parent;
  if (parent < 0 || parent >= (int)pane_tree.size()) {
    return false;
  }
  return adjust_pane_split_ratio(parent, delta);
}

bool Editor::resize_current_pane_direction(char dir, int delta) {
  if (panes.size() < 2 || current_pane < 0 ||
      current_pane >= (int)panes.size()) {
    return false;
  }

  char d = (char)std::tolower((unsigned char)dir);
  if (d != 'h' && d != 'j' && d != 'k' && d != 'l') {
    return false;
  }

  const SplitPane &pane = panes[(size_t)current_pane];
  int x = (d == 'h') ? pane.x : (d == 'l') ? pane.x + pane.w - 1
                                           : pane.x + pane.w / 2;
  int y = (d == 'k') ? pane.y : (d == 'j') ? pane.y + pane.h - 1
                                           : pane.y + pane.h / 2;
  int node = pane_split_at_position(x, y);
  if (node < 0) {
    return false;
  }

  int signed_delta = std::max(1, std::abs(delta));
  if (d == 'h' || d == 'k') {
    signed_delta = -signed_delta;
  }
  return adjust_pane_split_ratio(node, signed_delta);
}

int Editor::pane_split_at_position(int x, int y) const {
  if (pane_root < 0 || pane_root >= (int)pane_tree.size()) {
    return -1;
  }

  int best = -1;
  int best_score = std::numeric_limits<int>::max();

  std::function<void(int, int, int, int, int)> visit =
      [&](int node_index, int nx, int ny, int nw, int nh) {
        if (node_index < 0 || node_index >= (int)pane_tree.size() || nw <= 1 ||
            nh <= 1) {
          return;
        }
        const PaneTreeNode &node = pane_tree[node_index];
        if (node.leaf) {
          return;
        }

        float ratio = std::clamp(node.ratio, 0.1f, 0.9f);
        if (node.vertical) {
          int first_w = std::max(1, (int)(nw * ratio));
          if (nw >= 2) {
            first_w = std::min(first_w, nw - 1);
          }
          int border_x = nx + first_w;
          if ((x == border_x || x == border_x - 1) && y >= ny &&
              y < ny + nh) {
            int score = nh;
            if (score < best_score) {
              best_score = score;
              best = node_index;
            }
          }
          int second_w = std::max(1, nw - first_w);
          visit(node.first, nx, ny, first_w, nh);
          visit(node.second, nx + first_w, ny, second_w, nh);
        } else {
          int first_h = std::max(1, (int)(nh * ratio));
          if (nh >= 2) {
            first_h = std::min(first_h, nh - 1);
          }
          int border_y = ny + first_h;
          if ((y == border_y || y == border_y - 1) && x >= nx &&
              x < nx + nw) {
            int score = nw;
            if (score < best_score) {
              best_score = score;
              best = node_index;
            }
          }
          int second_h = std::max(1, nh - first_h);
          visit(node.first, nx, ny, nw, first_h);
          visit(node.second, nx, ny + first_h, nw, second_h);
        }
      };

  int total_w = std::max(1, ui->get_render_width());
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
  }
  int menu_h = 1;
  int total_h =
      std::max(1, ui->get_height() - status_height - reserved_terminal_h -
                      menu_h);
  int origin_x = show_sidebar ? effective_sidebar_width() : 0;
  int right_w = effective_right_panel_width();
  int available_w = std::max(1, total_w - origin_x - right_w);
  visit(pane_root, origin_x, menu_h, available_w, total_h);
  return best;
}

bool Editor::begin_sidebar_resize_drag(int x, int y) {
  if (!sidebar_resize_hit_test(x, y)) {
    return false;
  }
  bool opening_from_collapsed = collapsed_sidebar_handle_hit_test(x, y);
  sidebar_resize_dragging = true;
  sidebar_resize_opening = opening_from_collapsed;
  sidebar_resize_start_x = x;
  int open_width = effective_sidebar_width();
  sidebar_resize_start_width =
      opening_from_collapsed ? open_width : effective_sidebar_width();
  if (opening_from_collapsed) {
    if (file_tree.empty()) {
      load_file_tree(root_dir);
    }
    show_sidebar = true;
    sidebar_width = open_width;
    update_pane_layout();
  }
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_SIDEBAR;
  set_message("Resizing file tree");
  needs_redraw = true;
  return true;
}

bool Editor::update_sidebar_resize_drag(int x) {
  if (!sidebar_resize_dragging) {
    return false;
  }
  int requested_width = sidebar_resize_start_width + (x - sidebar_resize_start_x);
  if (!sidebar_resize_opening && requested_width <= sidebar_close_threshold()) {
    sidebar_resize_dragging = false;
    sidebar_resize_opening = false;
    sidebar_resize_start_x = 0;
    sidebar_resize_start_width = min_sidebar_width();
    sidebar_width = min_sidebar_width();
    show_sidebar = false;
    if (focus_state == FOCUS_SIDEBAR) {
      focus_state = FOCUS_EDITOR;
    }
    update_pane_layout();
    set_message("File tree closed");
    needs_redraw = true;
    return true;
  }

  int next_width =
      std::clamp(requested_width, min_sidebar_width(), max_sidebar_width());
  if (sidebar_width == next_width) {
    return false;
  }
  sidebar_resize_opening = false;
  sidebar_width = next_width;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::end_sidebar_resize_drag() {
  if (!sidebar_resize_dragging) {
    return;
  }
  sidebar_resize_dragging = false;
  sidebar_resize_opening = false;
  sidebar_resize_start_x = 0;
  sidebar_resize_start_width = effective_sidebar_width();
  sidebar_width = effective_sidebar_width();
  set_message("File tree resized");
  needs_redraw = true;
}

bool Editor::right_panel_resize_hit_test(int x, int y) const {
  if (!show_right_panel || !ui) {
    return false;
  }
  int w = effective_right_panel_width();
  if (w < min_right_panel_width()) {
    return false;
  }
  int handle_x = ui->get_render_width() - w;
  if (x != handle_x) {
    return false;
  }
  int top = 1;
  int bottom = ui->get_height() - status_height;
  return y >= top && y < bottom;
}

bool Editor::begin_right_panel_resize_drag(int x, int y) {
  if (!right_panel_resize_hit_test(x, y)) {
    return false;
  }
  right_panel_resize_dragging = true;
  right_panel_resize_start_x = x;
  right_panel_resize_start_width = effective_right_panel_width();
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_EDITOR;
  set_message("Resizing right panel");
  needs_redraw = true;
  return true;
}

bool Editor::update_right_panel_resize_drag(int x) {
  if (!right_panel_resize_dragging) {
    return false;
  }
  int requested_width =
      right_panel_resize_start_width + (right_panel_resize_start_x - x);
  int max_w = max_right_panel_width();
  int min_w = std::min(min_right_panel_width(), max_w);
  int next_width = std::clamp(requested_width, min_w, max_w);
  if (right_panel_width == next_width) {
    return false;
  }
  right_panel_width = next_width;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::end_right_panel_resize_drag() {
  if (!right_panel_resize_dragging) {
    return;
  }
  right_panel_resize_dragging = false;
  right_panel_resize_start_x = 0;
  right_panel_resize_start_width = effective_right_panel_width();
  right_panel_width = effective_right_panel_width();
  set_message("Right panel resized");
  needs_redraw = true;
}

bool Editor::adjust_pane_split_ratio(int node_index, int delta,
                                     bool clamp_only) {
  if (node_index < 0 || node_index >= (int)pane_tree.size() ||
      pane_tree[node_index].leaf) {
    return false;
  }

  PaneTreeNode &node = pane_tree[node_index];
  int first_pane = -1;
  int second_pane = -1;
  std::function<int(int)> first_pane_in_node = [&](int child) -> int {
    if (child < 0 || child >= (int)pane_tree.size()) {
      return -1;
    }
    const PaneTreeNode &n = pane_tree[child];
    if (n.leaf) {
      return (n.pane_index >= 0 && n.pane_index < (int)panes.size())
                 ? n.pane_index
                 : -1;
    }
    int first = first_pane_in_node(n.first);
    return first >= 0 ? first : first_pane_in_node(n.second);
  };

  first_pane = first_pane_in_node(node.first);
  second_pane = first_pane_in_node(node.second);
  if (first_pane < 0 || second_pane < 0) {
    return false;
  }

  int total_dim = node.vertical ? (panes[first_pane].w + panes[second_pane].w)
                                : (panes[first_pane].h + panes[second_pane].h);
  const int min_dim = node.vertical ? kMinPaneWidth : kMinPaneHeight;
  if (total_dim < min_dim * 2) {
    return false;
  }

  int first_dim = std::clamp((int)(node.ratio * total_dim), min_dim,
                             total_dim - min_dim);
  int next_first =
      std::clamp(first_dim + delta, min_dim, total_dim - min_dim);
  if (!clamp_only && next_first == first_dim) {
    return false;
  }

  node.ratio = (float)next_first / (float)std::max(1, total_dim);
  pane_layout_mode = node.vertical ? PANE_LAYOUT_VERTICAL : PANE_LAYOUT_HORIZONTAL;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

bool Editor::begin_pane_resize_drag(int x, int y) {
  int node = pane_split_at_position(x, y);
  if (node < 0 || node >= (int)pane_tree.size()) {
    return false;
  }
  pane_resize_dragging = true;
  pane_resize_node = node;
  pane_resize_vertical = pane_tree[node].vertical;
  pane_resize_start_pos = pane_resize_vertical ? x : y;
  pane_resize_start_ratio = pane_tree[node].ratio;
  mouse_selecting = false;
  mouse_drag_started = false;
  focus_state = FOCUS_EDITOR;
  set_message("Resizing pane");
  needs_redraw = true;
  return true;
}

bool Editor::update_pane_resize_drag(int x, int y) {
  if (!pane_resize_dragging || pane_resize_node < 0 ||
      pane_resize_node >= (int)pane_tree.size()) {
    return false;
  }
  int pos = pane_resize_vertical ? x : y;
  int delta = pos - pane_resize_start_pos;
  pane_tree[pane_resize_node].ratio = pane_resize_start_ratio;
  return adjust_pane_split_ratio(pane_resize_node, delta, true);
}

void Editor::end_pane_resize_drag() {
  if (!pane_resize_dragging) {
    return;
  }
  pane_resize_dragging = false;
  pane_resize_node = -1;
  pane_resize_vertical = false;
  pane_resize_start_pos = 0;
  pane_resize_start_ratio = 0.5f;
  set_message("Pane resized");
  needs_redraw = true;
}

void Editor::next_pane() {
  if (panes.size() > 1) {
    panes[current_pane].active = false;
    current_pane = (current_pane + 1) % (int)panes.size();
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
    message = "Switched pane";
    needs_redraw = true;
  }
}

void Editor::prev_pane() {
  if (panes.size() > 1) {
    panes[current_pane].active = false;
    current_pane = (current_pane - 1 + (int)panes.size()) % (int)panes.size();
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
    message = "Switched pane";
    needs_redraw = true;
  }
}

bool Editor::focus_pane_direction(char dir) {
  if (panes.size() < 2 || current_pane < 0 ||
      current_pane >= (int)panes.size()) {
    return false;
  }

  char d = (char)std::tolower((unsigned char)dir);
  const SplitPane &current = panes[(size_t)current_pane];
  int current_cx = pane_center_x(current);
  int current_cy = pane_center_y(current);
  int best = -1;
  int best_score = std::numeric_limits<int>::max();

  for (int i = 0; i < (int)panes.size(); i++) {
    if (i == current_pane) {
      continue;
    }
    const SplitPane &candidate = panes[(size_t)i];
    int candidate_cx = pane_center_x(candidate);
    int candidate_cy = pane_center_y(candidate);
    int primary = 0;
    int overlap = 0;
    int secondary = 0;

    if (d == 'h' || d == 'l') {
      if (d == 'h') {
        if (candidate_cx >= current_cx) {
          continue;
        }
        primary = current.x - (candidate.x + candidate.w);
      } else {
        if (candidate_cx <= current_cx) {
          continue;
        }
        primary = candidate.x - (current.x + current.w);
      }
      primary = std::max(0, primary);
      overlap = overlap_amount(current.y, current.h, candidate.y, candidate.h);
      secondary = std::abs(candidate_cy - current_cy);
    } else if (d == 'k' || d == 'j') {
      if (d == 'k') {
        if (candidate_cy >= current_cy) {
          continue;
        }
        primary = current.y - (candidate.y + candidate.h);
      } else {
        if (candidate_cy <= current_cy) {
          continue;
        }
        primary = candidate.y - (current.y + current.h);
      }
      primary = std::max(0, primary);
      overlap = overlap_amount(current.x, current.w, candidate.x, candidate.w);
      secondary = std::abs(candidate_cx - current_cx);
    } else {
      return false;
    }

    int score = primary * 1000 + secondary - overlap * 10;
    if (score < best_score) {
      best_score = score;
      best = i;
    }
  }

  if (best < 0) {
    return false;
  }

  panes[current_pane].active = false;
  current_pane = best;
  panes[current_pane].active = true;
  current_buffer = panes[current_pane].buffer_id;
  focus_state = FOCUS_EDITOR;
  message = "Focused pane";
  needs_redraw = true;
  return true;
}
