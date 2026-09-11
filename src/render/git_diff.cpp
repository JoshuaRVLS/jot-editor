// Git diff panel renderer (src/render/git_diff.cpp).
//
// Paints the per-file diff in the right dock: a file icon (per-language
// brand color) + path header with right-aligned +/- change stats, then the
// diff body with tinted added/deleted rows (green/red backgrounds), accent
// bold hunk headers, dim meta lines and plain context. Hands the model to
// the Lua side_panel handler (mode "git_diff") when one is registered; the
// native fallback stays byte-identical.

#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "ui/text.h"
#include <algorithm>
#include <filesystem>

namespace
{
  // Nerd Fonts glyphs (classic FontAwesome codepoints, present in every
  // Nerd Fonts build).
  constexpr const char *kDiffGlyph = "\uF1C9"; // file-code-o

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

  // Row styling for one raw diff line: hunk headers in the accent color,
  // added/deleted lines on green/red tint backgrounds, meta lines (file
  // headers, index hashes, mode changes) dimmed, context plain.
  struct DiffRowStyle
  {
    const char *kind;
    int fg;
    int bg;
    bool bold;
  };
  DiffRowStyle diff_row_style(const Theme &theme, const std::string &line)
  {
    if (line.rfind("@@", 0) == 0)
    {
      return {"diff_hunk", theme.fg_keyword, theme.bg_terminal, true};
    }
    if (line.rfind("diff --git", 0) == 0 || line.rfind("index ", 0) == 0
        || line.rfind("new file mode", 0) == 0 || line.rfind("deleted file mode", 0) == 0
        || line.rfind("similarity index", 0) == 0 || line.rfind("rename from", 0) == 0
        || line.rfind("rename to", 0) == 0 || line.rfind("Binary files", 0) == 0
        || line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0)
    {
      return {"diff_meta", theme.fg_comment, theme.bg_terminal, false};
    }
    if (!line.empty() && line[0] == '+')
    {
      return {"diff_add", theme.fg_git_added, theme.bg_git_added, false};
    }
    if (!line.empty() && line[0] == '-')
    {
      return {"diff_del", theme.fg_git_deleted, theme.bg_git_deleted, false};
    }
    return {"diff_context", theme.fg_terminal, theme.bg_terminal, false};
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

  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = topbar_height();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  const bool focused = focus_state == FOCUS_RIGHT_PANEL;
  const int border_fg = focused ? theme.fg_active_border : theme.fg_panel_border;
  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, border_fg, theme.bg_terminal, right_dock_edges(panel),
                       theme.bg_status);

  const std::string title = std::string(" \uF1C9 Git Diff: ")
                            + (git_diff_panel.staged ? "staged " : "unstaged");
  ui->draw_text(panel_x + 1,
                panel_y,
                title,
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  const int content_x = panel_x + 1;
  const int content_y = panel_y + 3;
  const int content_w = std::max(1, panel_w - 2);
  const int content_h = std::max(1, panel_h - 4);

  SidePanelView view;
  view.mode = "git_diff";
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = title;
  build_right_panel_tab_strip_view(view);
  render_right_panel_tab_strip(panel_x, panel_y, panel_w);

  // Header: per-language file icon (brand color) + path (status color) and
  // right-aligned change stats (+N -M · L lines).
  std::string header = git_diff_panel.path.empty() ? "(repo)" : git_diff_panel.path;
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
  if (status_it != git_file_status.end())
  {
    header_fg = git_diff_status_colors(theme, status_it->second).first;
  }
  const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(header);
  view.header_icon = fti.glyph;
  view.header_icon_fg = fti.color;
  view.header = header;
  view.header_fg = header_fg;

  int added = 0;
  int removed = 0;
  for (const auto &line : git_diff_panel.lines)
  {
    if (line.rfind("+++", 0) != 0 && !line.empty() && line[0] == '+')
    {
      added++;
    }
    else if (line.rfind("---", 0) != 0 && !line.empty() && line[0] == '-')
    {
      removed++;
    }
  }
  view.header_detail = "+" + std::to_string(added) + " -" + std::to_string(removed) + " \xc2\xb7 "
                       + std::to_string(git_diff_panel.lines.size()) + " lines";
  view.header_detail_fg = theme.fg_comment;

  const int body_y = content_y + 1;
  const int body_h = std::max(0, content_h - 1);

  if (git_diff_panel.lines.empty())
  {
    view.note = "No changes in this file";
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(content_x, content_y, view.note, view.note_fg, theme.bg_terminal);
    return;
  }

  const int max_scroll = std::max(0, (int)git_diff_panel.lines.size() - body_h);
  git_diff_panel.scroll = std::clamp(git_diff_panel.scroll, 0, max_scroll);

  for (int row = 0; row < body_h; row++)
  {
    const int line_index = git_diff_panel.scroll + row;
    if (line_index >= (int)git_diff_panel.lines.size())
    {
      break;
    }

    const std::string &line = git_diff_panel.lines[line_index];
    const DiffRowStyle style = diff_row_style(theme, line);
    SidePanelRowView r;
    r.text = line;
    r.kind = style.kind;
    r.fg = style.fg;
    r.bg = style.bg;
    r.bold = style.bold;
    view.rows.push_back(std::move(r));
  }
  if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
  {
    return;
  }

  // Native fallback: header icon + path + right-aligned stats, then rows
  // with full-width tint backgrounds for added/deleted lines.
  int hx = content_x;
  ui->draw_text(hx,
                content_y,
                view.header_icon,
                view.header_icon_fg >= 0 ? view.header_icon_fg : view.header_fg,
                theme.bg_terminal);
  hx += (int)ui_cell_count(view.header_icon) + 1;
  const int detail_w = std::min((int)ui_cell_count(view.header_detail), std::max(1, content_w - 6));
  const int path_w = std::max(1, content_w - (hx - content_x) - detail_w - 1);
  ui->draw_text(hx,
                content_y,
                ui_truncate_cells(view.header, path_w),
                view.header_fg,
                theme.bg_terminal,
                true);
  ui->draw_text(content_x + content_w - detail_w,
                content_y,
                view.header_detail,
                view.header_detail_fg,
                theme.bg_terminal);

  for (int row = 0; row < (int)view.rows.size(); row++)
  {
    const SidePanelRowView &r = view.rows[(size_t)row];
    if (r.bg != theme.bg_terminal)
    {
      ui->fill_rect({content_x, body_y + row, content_w, 1}, " ", r.fg, r.bg);
    }
    ui->draw_text(content_x,
                  body_y + row,
                  ui_truncate_cells(r.text, content_w),
                  r.fg,
                  r.bg);
  }
}