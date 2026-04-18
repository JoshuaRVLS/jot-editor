#include "editor.h"
#include <numeric>

namespace {
void normalize_pane_weights(std::vector<float> &weights) {
  if (weights.empty()) {
    return;
  }

  float total = std::accumulate(weights.begin(), weights.end(), 0.0f);
  if (total <= 0.0f) {
    float equal = 1.0f / (float)weights.size();
    for (float &weight : weights) {
      weight = equal;
    }
    return;
  }

  for (float &weight : weights) {
    weight /= total;
  }
}
} // namespace

int Editor::create_pane(int x, int y, int w, int h, int buffer_id) {
  SplitPane pane;
  pane.x = x;
  pane.y = y;
  pane.w = w;
  pane.h = h;
  pane.buffer_id = buffer_id == -1 ? buffers.size() : buffer_id;
  pane.active = panes.empty();
  panes.push_back(pane);
  pane_weights.push_back(1.0f);
  normalize_pane_weights(pane_weights);
  return panes.size() - 1;
}

void Editor::update_pane_layout() {
  if (panes.empty())
    return;

  int total_w = std::max(1, ui->get_width());
  int total_h = std::max(1, ui->get_height() - status_height - tab_height);
  int max_sidebar = std::max(0, total_w - 20);
  int origin_x = show_sidebar ? std::min(sidebar_width, max_sidebar) : 0;
  int available_w = std::max(1, total_w - origin_x);
  int origin_y = tab_height;

  if (panes.size() == 1) {
    pane_layout_mode = PANE_LAYOUT_SINGLE;
  }

  if (pane_weights.size() != panes.size()) {
    pane_weights.assign(panes.size(), 1.0f);
  }
  normalize_pane_weights(pane_weights);

  if (pane_layout_mode == PANE_LAYOUT_HORIZONTAL) {
    int y = origin_y;
    for (int i = 0; i < (int)panes.size(); i++) {
      int remaining = origin_y + total_h - y;
      int h = remaining;
      if (i != (int)panes.size() - 1) {
        h = std::max(1, (int)(total_h * pane_weights[i]));
        h = std::min(h, std::max(1, remaining - ((int)panes.size() - i - 1)));
      }
      panes[i].x = origin_x;
      panes[i].y = y;
      panes[i].w = available_w;
      panes[i].h = std::max(1, h);
      y += h;
    }
  } else {
    int x = origin_x;
    for (int i = 0; i < (int)panes.size(); i++) {
      int remaining = origin_x + available_w - x;
      int w = remaining;
      if (i != (int)panes.size() - 1) {
        w = std::max(1, (int)(available_w * pane_weights[i]));
        w = std::min(w, std::max(1, remaining - ((int)panes.size() - i - 1)));
      }
      panes[i].x = x;
      panes[i].y = origin_y;
      panes[i].w = std::max(1, w);
      panes[i].h = total_h;
      x += w;
    }
  }
}

void Editor::split_pane_horizontal() {
  if (panes.empty())
    create_pane(0, tab_height, ui->get_width(),
                ui->get_height() - status_height - tab_height, current_buffer);

  pane_layout_mode = PANE_LAYOUT_HORIZONTAL;
  int buffer_id = get_pane().buffer_id;
  for (auto &pane : panes)
    pane.active = false;
  current_pane = create_pane(0, 0, 0, 0, buffer_id);
  panes[current_pane].active = true;
  current_buffer = panes[current_pane].buffer_id;
  pane_weights.assign(panes.size(), 1.0f);
  normalize_pane_weights(pane_weights);
  update_pane_layout();
  message = "Split pane horizontally";
  needs_redraw = true;
}

void Editor::split_pane_vertical() {
  if (panes.empty())
    create_pane(0, tab_height, ui->get_width(),
                ui->get_height() - status_height - tab_height, current_buffer);

  pane_layout_mode = PANE_LAYOUT_VERTICAL;
  int buffer_id = get_pane().buffer_id;
  for (auto &pane : panes)
    pane.active = false;
  current_pane = create_pane(0, 0, 0, 0, buffer_id);
  panes[current_pane].active = true;
  current_buffer = panes[current_pane].buffer_id;
  pane_weights.assign(panes.size(), 1.0f);
  normalize_pane_weights(pane_weights);
  update_pane_layout();
  message = "Split pane vertically";
  needs_redraw = true;
}

void Editor::close_pane() {
  if (panes.size() <= 1) {
    message = "Can't close the last pane";
    return;
  }

  panes.erase(panes.begin() + current_pane);
  if (current_pane >= 0 && current_pane < (int)pane_weights.size()) {
    pane_weights.erase(pane_weights.begin() + current_pane);
  }
  if (current_pane >= (int)panes.size())
    current_pane = (int)panes.size() - 1;
  for (int i = 0; i < (int)panes.size(); i++) {
    panes[i].active = (i == current_pane);
  }
  current_buffer = panes[current_pane].buffer_id;
  if (panes.size() == 1)
    pane_layout_mode = PANE_LAYOUT_SINGLE;
  update_pane_layout();
  message = "Pane closed";
  needs_redraw = true;
}

bool Editor::resize_current_pane(int delta) {
  if (panes.size() < 2 || current_pane < 0 || current_pane >= (int)panes.size()) {
    return false;
  }

  if (pane_weights.size() != panes.size()) {
    pane_weights.assign(panes.size(), 1.0f);
    normalize_pane_weights(pane_weights);
  }

  int total_dim = 0;
  int min_dim = 8;
  if (pane_layout_mode == PANE_LAYOUT_VERTICAL) {
    total_dim = std::max(1, ui->get_width() - (show_sidebar ? sidebar_width : 0));
    min_dim = 16;
  } else if (pane_layout_mode == PANE_LAYOUT_HORIZONTAL) {
    total_dim = std::max(1, ui->get_height() - status_height - tab_height);
    min_dim = 4;
  } else {
    return false;
  }

  int neighbor = (current_pane < (int)panes.size() - 1) ? current_pane + 1
                                                         : current_pane - 1;
  if (neighbor < 0 || neighbor >= (int)panes.size()) {
    return false;
  }

  float delta_weight = (float)delta / (float)std::max(1, total_dim);
  float min_weight = (float)min_dim / (float)std::max(1, total_dim);
  float new_current = pane_weights[current_pane] + delta_weight;
  float new_neighbor = pane_weights[neighbor] - delta_weight;

  if (new_current < min_weight || new_neighbor < min_weight) {
    return false;
  }

  pane_weights[current_pane] = new_current;
  pane_weights[neighbor] = new_neighbor;
  normalize_pane_weights(pane_weights);
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::next_pane() {
  if (panes.size() > 1) {
    panes[current_pane].active = false;
    current_pane = (current_pane + 1) % panes.size();
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
    message = "Switched pane";
    needs_redraw = true;
  }
}

void Editor::prev_pane() {
  if (panes.size() > 1) {
    panes[current_pane].active = false;
    current_pane = (current_pane - 1 + panes.size()) % panes.size();
    panes[current_pane].active = true;
    current_buffer = panes[current_pane].buffer_id;
    message = "Switched pane";
    needs_redraw = true;
  }
}
