#include "editor.h"
#include <algorithm>
#include <cctype>
#include <functional>

int Editor::create_pane(int x, int y, int w, int h, int buffer_id) {
  SplitPane pane;
  pane.x = x;
  pane.y = y;
  pane.w = w;
  pane.h = h;
  pane.buffer_id = buffer_id == -1 ? (int)buffers.size() : buffer_id;
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

  int total_w = std::max(1, ui->get_width());
  int total_h = std::max(1, ui->get_height() - status_height);
  int max_sidebar = std::max(0, total_w - 20);
  int origin_x = show_sidebar ? std::min(sidebar_width, max_sidebar) : 0;
  int available_w = std::max(1, total_w - origin_x);
  int origin_y = 0;

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
    create_pane(0, 0, ui->get_width(), ui->get_height() - status_height,
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
  const int old_buffer = panes[old_pane].buffer_id;
  int new_buffer = old_buffer;
  if ((int)buffers.size() > 1) {
    int next = (old_buffer + 1) % (int)buffers.size();
    if (next == old_buffer) {
      next = 0;
    }
    new_buffer = next;
  } else {
    // If there is only one buffer, create a fresh untitled buffer so the new
    // pane feels independent by default.
    FileBuffer fb;
    fb.lines.push_back("");
    fb.cursor = {0, 0};
    fb.preferred_x = 0;
    fb.selection = {{0, 0}, {0, 0}, false};
    fb.scroll_offset = 0;
    fb.scroll_x = 0;
    fb.modified = false;
    fb.is_preview = false;
    buffers.push_back(fb);
    new_buffer = (int)buffers.size() - 1;
  }

  SplitPane new_pane{};
  new_pane.x = 0;
  new_pane.y = 0;
  new_pane.w = 1;
  new_pane.h = 1;
  new_pane.buffer_id = new_buffer;
  new_pane.active = false;
  new_pane.tab_buffer_ids.clear();
  new_pane.tab_buffer_ids.push_back(new_buffer);
  new_pane.tab_scroll_index = 0;
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

  int next = first_pane_in_node(pane_root);
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
  if (panes.size() < 2 || current_pane < 0 || current_pane >= (int)panes.size()) {
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
    int left = find_leaf(node.first, pane_index);
    if (left >= 0) {
      return left;
    }
    return find_leaf(node.second, pane_index);
  };

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0) {
    return false;
  }

  int parent = pane_tree[leaf].parent;
  if (parent < 0 || parent >= (int)pane_tree.size()) {
    return false;
  }

  std::function<bool(int, int)> contains_leaf = [&](int node_index,
                                                     int target_leaf) -> bool {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return false;
    }
    if (node_index == target_leaf) {
      return true;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return false;
    }
    return contains_leaf(node.first, target_leaf) ||
           contains_leaf(node.second, target_leaf);
  };

  int first_pane = -1;
  int second_pane = -1;
  {
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

    first_pane = first_pane_in_node(pane_tree[parent].first);
    second_pane = first_pane_in_node(pane_tree[parent].second);
  }

  if (first_pane < 0 || second_pane < 0) {
    return false;
  }

  const bool vertical = pane_tree[parent].vertical;
  int total_dim = vertical ? (panes[first_pane].w + panes[second_pane].w)
                           : (panes[first_pane].h + panes[second_pane].h);
  if (total_dim <= 1) {
    return false;
  }

  const int min_dim = vertical ? 12 : 3;
  int delta_px = delta;
  bool in_first = contains_leaf(pane_tree[parent].first, leaf);
  if (!in_first) {
    delta_px = -delta_px;
  }

  int first_dim = std::clamp((int)(pane_tree[parent].ratio * total_dim), 1,
                             std::max(1, total_dim - 1));
  int second_dim = total_dim - first_dim;
  int next_first = first_dim + delta_px;
  int next_second = second_dim - delta_px;
  if (next_first < min_dim || next_second < min_dim) {
    return false;
  }

  pane_tree[parent].ratio = (float)next_first / (float)std::max(1, total_dim);
  pane_layout_mode = vertical ? PANE_LAYOUT_VERTICAL : PANE_LAYOUT_HORIZONTAL;
  update_pane_layout();
  needs_redraw = true;
  return true;
}

bool Editor::resize_current_pane_direction(char dir, int delta) {
  if (panes.size() < 2) {
    return false;
  }

  char d = (char)std::tolower((unsigned char)dir);
  bool want_vertical = (d == 'l' || d == 'r');
  int signed_delta = std::abs(delta);
  if (d == 'l' || d == 'k') {
    signed_delta = -signed_delta;
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

  int leaf = find_leaf(pane_root, current_pane);
  if (leaf < 0) {
    return false;
  }

  int ancestor = pane_tree[leaf].parent;
  while (ancestor >= 0) {
    if (ancestor < (int)pane_tree.size() &&
        pane_tree[ancestor].vertical == want_vertical) {
      break;
    }
    ancestor = (ancestor < (int)pane_tree.size()) ? pane_tree[ancestor].parent : -1;
  }

  if (ancestor < 0 || ancestor >= (int)pane_tree.size()) {
    return false;
  }

  std::function<bool(int, int)> contains_leaf = [&](int node_index,
                                                     int target_leaf) -> bool {
    if (node_index < 0 || node_index >= (int)pane_tree.size()) {
      return false;
    }
    if (node_index == target_leaf) {
      return true;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf) {
      return false;
    }
    return contains_leaf(node.first, target_leaf) ||
           contains_leaf(node.second, target_leaf);
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

  int first_pane = first_pane_in_node(pane_tree[ancestor].first);
  int second_pane = first_pane_in_node(pane_tree[ancestor].second);
  if (first_pane < 0 || second_pane < 0) {
    return false;
  }

  int total_dim = want_vertical ? (panes[first_pane].w + panes[second_pane].w)
                                : (panes[first_pane].h + panes[second_pane].h);
  if (total_dim <= 1) {
    return false;
  }

  const int min_dim = want_vertical ? 12 : 3;
  int delta_px = signed_delta;
  bool in_first = contains_leaf(pane_tree[ancestor].first, leaf);
  if (!in_first) {
    delta_px = -delta_px;
  }

  int first_dim = std::clamp((int)(pane_tree[ancestor].ratio * total_dim), 1,
                             std::max(1, total_dim - 1));
  int second_dim = total_dim - first_dim;
  int next_first = first_dim + delta_px;
  int next_second = second_dim - delta_px;
  if (next_first < min_dim || next_second < min_dim) {
    return false;
  }

  pane_tree[ancestor].ratio =
      (float)next_first / (float)std::max(1, total_dim);
  pane_layout_mode = want_vertical ? PANE_LAYOUT_VERTICAL : PANE_LAYOUT_HORIZONTAL;
  update_pane_layout();
  needs_redraw = true;
  return true;
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
