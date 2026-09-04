#include "editor.h"
#include "lua_bridge/api.h"
#include "ui/text.h"

#include <algorithm>

void Editor::render_plugin_panel()
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_PLUGIN || !ui)
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

  std::string title = active_plugin_panel.empty() ? " Plugin " : " " + active_plugin_panel + " ";
  ui->draw_text(panel_x + 1,
                panel_y,
                ui_truncate_cells(title, panel_w - 2),
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  int content_x = panel_x + 1;
  int content_y = panel_y + 2;
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(0, panel_h - 3);

  // Hand the model to a Lua UI handler when one is registered; it owns the
  // paint. Native fallback below stays byte-identical.
  SidePanelView view;
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = active_plugin_panel.empty() ? " Plugin " : " " + active_plugin_panel + " ";

  std::vector<std::string> lines;
  if (lua_api && !active_plugin_panel.empty())
  {
    lines = lua_api->plugin_panel_lines(active_plugin_panel);
  }

  if (lines.empty())
  {
    view.note = "No plugin panel content";
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(
        content_x, content_y, "No plugin panel content", theme.fg_comment, theme.bg_terminal);
    return;
  }

  for (int row = 0; row < content_h && row < (int)lines.size(); row++)
  {
    SidePanelRowView r;
    r.text = ui_truncate_cells(lines[(size_t)row], content_w);
    r.fg = theme.fg_terminal;
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
                  content_y + row,
                  view.rows[(size_t)row].text,
                  theme.fg_terminal,
                  theme.bg_terminal);
  }
}
