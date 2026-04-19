#include "bracket.h"
#include "editor.h"
#include <cstdio>
#include <sstream>

namespace {
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

std::string completion_kind_icon(int kind) {
  switch (kind) {
  case 2:
    return "󰈙 "; // Method
  case 3:
    return "󰆧 "; // Function
  case 4:
    return "󰏫 "; // Constructor
  case 5:
    return " "; // Field
  case 6:
    return " "; // Variable
  case 7:
    return " "; // Class
  case 8:
    return " "; // Interface
  case 9:
    return " "; // Module
  case 10:
    return " "; // Property
  case 12:
    return " "; // Value
  case 14:
    return " "; // Keyword
  default:
    return "󰘍 ";
  }
}
} // namespace

void Editor::render_lsp_completion() {
  if (!lsp_completion_visible || lsp_completion_items.empty() ||
      panes.empty()) {
    return;
  }

  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);
  if (!lsp_completion_filepath.empty() &&
      buf.filepath != lsp_completion_filepath) {
    hide_lsp_completion();
    return;
  }
  if (buf.cursor.x != lsp_completion_anchor.x ||
      buf.cursor.y != lsp_completion_anchor.y) {
    hide_lsp_completion();
    return;
  }

  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int line_num_width = 7;
  int visible_h = std::max(1, pane.h - tab_height);
  int visible_w = std::max(12, draw_w - 2 - line_num_width);
  int max_items = std::min(8, (int)lsp_completion_items.size());
  int selected = std::clamp(lsp_completion_selected, 0,
                            (int)lsp_completion_items.size() - 1);
  int start_idx = std::max(0, selected - max_items + 1);
  if (selected < start_idx) {
    start_idx = selected;
  }
  if (start_idx + max_items > (int)lsp_completion_items.size()) {
    start_idx = std::max(0, (int)lsp_completion_items.size() - max_items);
  }

  int longest = 12;
  for (int i = start_idx; i < start_idx + max_items; i++) {
    if (i < 0 || i >= (int)lsp_completion_items.size()) {
      continue;
    }
    int len = (int)lsp_completion_items[i].label.size();
    if (!lsp_completion_items[i].detail.empty()) {
      len += 3 + std::min(24, (int)lsp_completion_items[i].detail.size());
    }
    longest = std::max(longest, len);
  }

  int box_w = std::clamp(longest + 6, 20, std::min(visible_w, 72));
  int box_h = max_items;

  int safe_cursor_y = std::clamp(buf.cursor.y, 0, (int)buf.lines.size() - 1);
  const std::string &line = buf.lines[safe_cursor_y];
  int cursor_visual = compute_visual_column(line, buf.cursor.x, tab_size);
  int scroll_visual = compute_visual_column(line, buf.scroll_x, tab_size);
  int cursor_x = pane.x + 1 + line_num_width + (cursor_visual - scroll_visual);
  int cursor_y = pane.y + tab_height + (buf.cursor.y - buf.scroll_offset);

  int min_x = pane.x + 1 + line_num_width;
  int max_x = pane.x + draw_w - box_w - 1;
  if (max_x < min_x) {
    max_x = min_x;
  }

  int min_y = pane.y + tab_height;
  int max_y = pane.y + visible_h - box_h;
  if (max_y < min_y) {
    max_y = min_y;
  }

  auto clamp_box_x = [&](int x) { return std::clamp(x, min_x, max_x); };
  auto clamp_box_y = [&](int y) { return std::clamp(y, min_y, max_y); };
  auto border_hits_cursor = [&](int x, int y) {
    int left = x - 1;
    int right = x + box_w;
    int top = y - 1;
    int bottom = y + box_h;
    return cursor_x >= left && cursor_x <= right && cursor_y >= top &&
           cursor_y <= bottom;
  };

  // Try multiple placements and pick the first where full bordered popup
  // doesn't touch the cursor cell.
  std::vector<std::pair<int, int>> candidates = {
      {cursor_x + 2, cursor_y + 2},
      {cursor_x + 2, cursor_y - box_h - 2},
      {cursor_x - box_w - 2, cursor_y + 2},
      {cursor_x - box_w - 2, cursor_y - box_h - 2},
      {cursor_x + 2, cursor_y},
      {cursor_x - box_w - 2, cursor_y},
      {cursor_x, cursor_y + 2},
      {cursor_x, cursor_y - box_h - 2},
  };

  int box_x = clamp_box_x(cursor_x + 2);
  int box_y = clamp_box_y(cursor_y + 2);
  for (const auto &cand : candidates) {
    int cx = clamp_box_x(cand.first);
    int cy = clamp_box_y(cand.second);
    if (!border_hits_cursor(cx, cy)) {
      box_x = cx;
      box_y = cy;
      break;
    }
  }

  UIRect rect = {box_x, box_y, box_w, box_h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  UIRect border_rect = {box_x - 1, box_y - 1, box_w + 2, box_h + 2};
  ui->draw_border(border_rect, theme.fg_panel_border, theme.bg_command);

  for (int row = 0; row < max_items; row++) {
    int item_idx = start_idx + row;
    if (item_idx < 0 || item_idx >= (int)lsp_completion_items.size()) {
      break;
    }

    const auto &item = lsp_completion_items[item_idx];
    bool selected_row = (item_idx == selected);
    int fg = selected_row ? theme.fg_selection : theme.fg_command;
    int bg = selected_row ? theme.bg_selection : theme.bg_command;
    std::string icon = completion_kind_icon(item.kind);
    std::string text = " " + icon + item.label;

    if (!item.detail.empty()) {
      std::string detail = item.detail.substr(0, 24);
      text += "  " + detail;
    }
    if ((int)text.size() > box_w - 2) {
      text = text.substr(0, std::max(0, box_w - 5)) + "...";
    }

    UIRect row_rect = {box_x, box_y + row, box_w, 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    ui->draw_text(box_x + 1, box_y + row, text, fg, bg, selected_row);
  }
}

void Editor::render_telescope() {
  int h = ui->get_height();
  int w = ui->get_width();
  if (w < 4 || h < 3)
    return;

  int content_x = 0;
  int content_w = w;
  if (show_sidebar) {
    int sidebar_w = std::min(sidebar_width, std::max(0, w - 20));
    content_x = std::max(0, sidebar_w);
    content_w = std::max(1, w - content_x);
  }

  // Compact quick-open style overlay so it doesn't take over the whole editor.
  int modal_w =
      std::min(std::max(1, content_w - 6), std::max(48, content_w * 2 / 3));
  int modal_h = std::min(h - 6, std::max(10, h / 2));
  // Round to nearest cell so odd widths don't bias left.
  int x = content_x + std::max(0, (content_w - modal_w + 1) / 2);
  x = std::clamp(x, content_x,
                 std::max(content_x, content_x + content_w - modal_w));
  int top_bound = std::max(0, tab_height + 1);
  int bottom_bound = std::max(top_bound + 1, h - status_height - 1);
  int usable_h = std::max(1, bottom_bound - top_bound);
  int y = top_bound + std::max(0, (usable_h - modal_h) / 2);
  int list_w = std::max(24, modal_w * 2 / 5);
  int preview_w = modal_w - list_w - 1;
  int list_h = modal_h - 5;

  UIRect rect = {x, y, modal_w, modal_h};
  ui->fill_rect(rect, " ", theme.fg_telescope, theme.bg_telescope);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_telescope);

  std::string title = " Find Files ";
  std::string root = telescope.get_root_dir();
  if ((int)root.length() > modal_w - 18) {
    root = "..." + root.substr(root.length() - (modal_w - 21));
  }
  ui->draw_text(x + 2, y, title, theme.fg_telescope, theme.bg_telescope, true);
  ui->draw_text(x + 2, y + 1, root, theme.fg_comment, theme.bg_telescope);

  std::string query = " > " + telescope.get_query();
  if ((int)query.length() > modal_w - 4) {
    query = query.substr(0, modal_w - 7) + "...";
  }
  ui->draw_text(x + 2, y + 2, query, theme.fg_telescope, theme.bg_telescope);
  ui->draw_text(x + list_w + 1, y + 1, " Preview ", theme.fg_telescope,
                theme.bg_telescope, true);
  ui->draw_text(x + list_w, y + 1, "|", theme.fg_panel_border,
                theme.bg_telescope);

  const auto &results = telescope.get_results();
  int selected = telescope.get_selected_index();
  int start_idx = std::max(0, selected - (list_h / 2));
  int end_idx = std::min((int)results.size(), start_idx + list_h);
  if (end_idx - start_idx < list_h) {
    start_idx = std::max(0, end_idx - list_h);
  }

  if (results.empty()) {
    ui->draw_text(x + 2, y + 4, "No files match the current query.",
                  theme.fg_comment, theme.bg_telescope);
  }

  for (int i = start_idx; i < end_idx; i++) {
    int row_y = y + 4 + (i - start_idx);
    int fg = theme.fg_telescope, bg = theme.bg_telescope;

    if (i == selected) {
      fg = theme.fg_telescope_selected;
      bg = theme.bg_telescope_selected;
    }

    std::string icon = results[i].is_directory ? "[D] " : "[F] ";
    std::string name = results[i].name;
    if ((int)name.length() > list_w - 10) {
      name = name.substr(0, list_w - 13) + "...";
    }

    ui->draw_text(x + 2, row_y, icon + name, fg, bg);
  }

  if (!results.empty() && selected >= 0 && selected < (int)results.size()) {
    auto preview_lines = telescope.get_preview_lines();
    int preview_x = x + list_w + 2;
    int preview_h = modal_h - 4;

    std::string path = telescope.get_selected_path();
    if (!path.empty()) {
      std::string path_display = path;
      if ((int)path_display.length() > preview_w - 2) {
        path_display =
            "..." + path_display.substr(path_display.length() - preview_w + 5);
      }
      ui->draw_text(preview_x, y + 2, path_display, theme.fg_telescope_preview,
                    theme.bg_telescope_preview);

      for (size_t i = 0;
           i < preview_lines.size() && i < (size_t)(preview_h - 2); i++) {
        std::string line = preview_lines[i];
        if ((int)line.length() > preview_w - 2) {
          line = line.substr(0, preview_w - 5) + "...";
        }
        ui->draw_text(preview_x, y + 4 + (int)i, line,
                      theme.fg_telescope_preview, theme.bg_telescope_preview);
      }
    }
  }

  std::string footer = "Enter open  Backspace parent  Ctrl+U clear  Esc close";
  if ((int)footer.length() > modal_w - 4) {
    footer = footer.substr(0, modal_w - 7) + "...";
  }
  ui->draw_text(x + 2, y + modal_h - 2, footer, theme.fg_comment,
                theme.bg_telescope);
}

void Editor::render_image_viewer() {
  int w = ui->get_width();
  int h = ui->get_height();
  if (w < 4 || h <= status_height + tab_height)
    return;
  int img_w = w / 2;
  int img_h = h - status_height - tab_height;

  image_viewer.render(w - img_w, tab_height, img_w, img_h,
                      theme.fg_image_border, theme.bg_image_border);
}
