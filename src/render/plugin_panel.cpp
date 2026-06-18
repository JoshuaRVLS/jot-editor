#include "editor.h"
#include "python_bridge/api.h"
#include "ui/text.h"

#include <algorithm>

void Editor::render_plugin_panel() {
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_PLUGIN || !ui) {
    return;
  }

  int panel_w = effective_right_panel_width();
  if (panel_w <= 0) {
    return;
  }

  int panel_x = std::max(0, ui->get_render_width() - panel_w);
  int panel_y = 1;
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  std::string title = active_plugin_panel.empty() ? " Plugin "
                                                  : " " + active_plugin_panel + " ";
  ui->draw_text(panel_x + 1, panel_y, ui_truncate_cells(title, panel_w - 2),
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused, true);

  int content_x = panel_x + 1;
  int content_y = panel_y + 2;
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(0, panel_h - 3);

  std::vector<std::string> lines;
  if (python_api && !active_plugin_panel.empty()) {
    lines = python_api->plugin_panel_lines(active_plugin_panel);
  }

  if (lines.empty()) {
    ui->draw_text(content_x, content_y, "No plugin panel content",
                  theme.fg_comment, theme.bg_terminal);
    return;
  }

  for (int row = 0; row < content_h && row < (int)lines.size(); row++) {
    ui->draw_text(content_x, content_y + row,
                  ui_truncate_cells(lines[(size_t)row], content_w),
                  theme.fg_terminal, theme.bg_terminal);
  }
}
