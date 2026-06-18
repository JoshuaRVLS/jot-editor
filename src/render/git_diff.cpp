#include "editor.h"
#include "ui/text.h"
#include <algorithm>

namespace {
  int diff_line_color(const Theme &theme, const std::string &line) {
    if (line.rfind("@@", 0) == 0) {
      return theme.fg_keyword;
    }
    if(line.rfind("diff --git", 0) == 0 || line.rfind("index ", 0) == 0) {
      return theme.fg_function;
    }
    if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) {
      return theme.fg_comment;
    }
    if (!line.empty() && line[0] == '+') {
      return theme.fg_string;
    }
    if (!line.empty() && line[0] == '-') {
      return theme.fg_status_error;
    }
    return theme.fg_terminal;
  }
} // namespace

void Editor::render_git_diff_panel() {
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_GIT_DIFF || !ui) {
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
  
  std::string title = git_diff_panel.staged ? " Git Diff: staged " : " Git Diff: unstaged";
  ui->draw_text(panel_x + 1, panel_y, title, theme.fg_terminal_tab_focused, theme.bg_terminal_tab_focused, true);
  
  int content_x = panel_x + 1;
  int content_y = panel_y + 2;
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(1, panel_h - 3);
  
  std::string header = git_diff_panel.path.empty() ? "(repo)" : git_diff_panel.path;
  std::string count = std::to_string(git_diff_panel.lines.size()) + " lines";
  ui->draw_text(content_x, content_y, ui_truncate_cells(header + " " + count, content_w), theme.fg_status_info, theme.bg_terminal, true);
  
  int body_y = content_y + 1;
  int body_h = std::max(0, content_h - 1);
  
  if (git_diff_panel.lines.empty()) {
    ui->draw_text(content_x, content_y, "No Diff", theme.fg_comment, theme.bg_terminal);
    return;
  }
  
  int max_scroll = std::max(0, (int)git_diff_panel.lines.size() - body_h);
  git_diff_panel.scroll = std::clamp(git_diff_panel.scroll, 0, max_scroll);
  
  for (int row = 0; row < body_h; row++) {
    int line_index = git_diff_panel.scroll + row;
    if (line_index >= (int)git_diff_panel.lines.size()) {
      break;
    }
    
    const std::string &line = git_diff_panel.lines[line_index];
    int fg = diff_line_color(theme, line);
    ui->draw_text(content_x, body_y + row, ui_truncate_cells(line, content_w), fg, theme.bg_terminal);
  }
  return;
}


























