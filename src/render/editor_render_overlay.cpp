#include "bracket.h"
#include "editor.h"
#include <cstdio>
#include <sstream>

void Editor::render_telescope() {
  int h = ui->get_height();
  int w = ui->get_width();
  if (w < 4 || h < 3)
    return;

  int modal_w = std::min(w - 4, std::max(56, w * 4 / 5));
  int modal_h = std::min(h - 2, std::max(12, h * 4 / 5));
  int x = std::max(0, (w - modal_w) / 2);
  int y = std::max(0, (h - modal_h) / 2);
  int list_w = std::max(24, modal_w / 2);
  int preview_w = modal_w - list_w - 1;
  int list_h = modal_h - 5;

  UIRect shadow = {x + 1, y + 1, modal_w, modal_h};
  ui->draw_rect(shadow, 8, 0);

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

  std::string footer = "Enter open  Left parent  Esc close";
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

