#include "editor.h"
#include "lua_bridge/api.h"
#include "ui/text.h"
#include <algorithm>
#include <filesystem>

namespace
{
  std::pair<int, int> git_diff_status_colors(const Theme &theme, const std::string &status)
  {
    if (status.find('U') != std::string::npos || status == "AA" || status == "DD")
      return {theme.fg_git_conflict, theme.bg_git_conflict};
    if (status.find('D') != std::string::npos)
      return {theme.fg_git_deleted, theme.bg_git_deleted};
    if (status.find('R') != std::string::npos)
      return {theme.fg_git_renamed, theme.bg_git_renamed};
    if (status.find('A') != std::string::npos || status.find('?') != std::string::npos)
      return {theme.fg_git_added, theme.bg_git_added};
    return {theme.fg_git_modified, theme.bg_git_modified};
  }

  int diff_line_color(const Theme &theme, const std::string &line)
  {
    if (line.rfind("@@", 0) == 0)
    {
      return theme.fg_keyword;
    }
    if (line.rfind("diff --git", 0) == 0 || line.rfind("index ", 0) == 0)
    {
      return theme.fg_function;
    }
    if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0)
    {
      return theme.fg_comment;
    }
    if (!line.empty() && line[0] == '+')
    {
      return theme.fg_string;
    }
    if (!line.empty() && line[0] == '-')
    {
      return theme.fg_status_error;
    }
    return theme.fg_terminal;
  }
} // namespace

void Editor::render_git_diff_panel()
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_GIT_DIFF || !ui)
  {
    return;
  }
  int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return;
  }

  int panel_x = std::max(0, ui->get_render_width() - panel_w);
  int panel_y = topbar_height();
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  std::string title = git_diff_panel.staged ? " Git Diff: staged " : " Git Diff: unstaged";
  ui->draw_text(panel_x + 1,
                panel_y,
                title,
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  int content_x = panel_x + 1;
  int content_y = panel_y + 2;
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(1, panel_h - 3);

  // Hand the model to a Lua UI handler when one is registered; it owns the
  // paint. Native fallback below stays byte-identical.
  SidePanelView view;
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = git_diff_panel.staged ? " Git Diff: staged " : " Git Diff: unstaged";

  std::string header = git_diff_panel.path.empty() ? "(repo)" : git_diff_panel.path;
  std::string count = std::to_string(git_diff_panel.lines.size()) + " lines";
  std::error_code status_ec;
  std::filesystem::path status_path = git_diff_panel.path;
  if (!git_root.empty() && !status_path.is_absolute())
  {
    status_path = std::filesystem::path(git_root) / status_path;
  }
  const std::string normalized_path =
      std::filesystem::absolute(status_path, status_ec).lexically_normal().string();
  auto status_it = git_file_status.find(normalized_path);
  int header_fg = theme.fg_status_info;
  int header_bg = theme.bg_terminal;
  if (status_it != git_file_status.end())
  {
    auto colors = git_diff_status_colors(theme, status_it->second);
    header_fg = colors.first;
    header_bg = colors.second;
  }
  view.header = ui_truncate_cells(header + " " + count, content_w);
  view.header_fg = header_fg;

  int body_y = content_y + 1;
  int body_h = std::max(0, content_h - 1);

  if (git_diff_panel.lines.empty())
  {
    view.note = "No Diff";
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(content_x, content_y, "No Diff", theme.fg_comment, theme.bg_terminal);
    return;
  }

  int max_scroll = std::max(0, (int)git_diff_panel.lines.size() - body_h);
  git_diff_panel.scroll = std::clamp(git_diff_panel.scroll, 0, max_scroll);

  for (int row = 0; row < body_h; row++)
  {
    int line_index = git_diff_panel.scroll + row;
    if (line_index >= (int)git_diff_panel.lines.size())
    {
      break;
    }

    const std::string &line = git_diff_panel.lines[line_index];
    SidePanelRowView r;
    r.text = line;
    r.fg = diff_line_color(theme, line);
    r.bg = theme.bg_terminal;
    view.rows.push_back(std::move(r));
  }
  if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
  {
    return;
  }
  for (int row = 0; row < (int)view.rows.size(); row++)
  {
    ui->draw_text(content_x,
                  body_y + row,
                  ui_truncate_cells(view.rows[(size_t)row].text, content_w),
                  view.rows[(size_t)row].fg,
                  theme.bg_terminal);
  }
  return;
}
