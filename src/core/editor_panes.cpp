#include "editor.h"

int Editor::create_pane(int x, int y, int w, int h, int buffer_id) {
  SplitPane pane;
  pane.x = x;
  pane.y = y;
  pane.w = w;
  pane.h = h;
  pane.buffer_id = buffer_id == -1 ? buffers.size() : buffer_id;
  pane.active = panes.empty();
  panes.push_back(pane);
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

  if (pane_layout_mode == PANE_LAYOUT_HORIZONTAL) {
    int pane_h = std::max(1, total_h / (int)panes.size());
    int y = origin_y;
    for (int i = 0; i < (int)panes.size(); i++) {
      int h = (i == (int)panes.size() - 1) ? (origin_y + total_h - y) : pane_h;
      panes[i].x = origin_x;
      panes[i].y = y;
      panes[i].w = available_w;
      panes[i].h = std::max(1, h);
      y += pane_h;
    }
  } else {
    int pane_w = std::max(1, available_w / (int)panes.size());
    int x = origin_x;
    for (int i = 0; i < (int)panes.size(); i++) {
      int w = (i == (int)panes.size() - 1) ? (origin_x + available_w - x) : pane_w;
      panes[i].x = x;
      panes[i].y = origin_y;
      panes[i].w = std::max(1, w);
      panes[i].h = total_h;
      x += pane_w;
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
