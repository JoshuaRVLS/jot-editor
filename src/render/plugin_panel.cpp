#include "editor.h"
#include "lua/api.h"
#include "render/pane_edges.h"
#include "ui/text.h"

#include <algorithm>

// The plugin panel's row area: the dock minus its border, the tab strip and
// the title row. The renderer and the mouse hit-test both read this, so a
// row's pixels and its clickable extent cannot drift apart.
TerminalBox Editor::plugin_panel_body() const
{
  TerminalBox body;
  if (!ui)
  {
    return body;
  }
  const int panel_w = effective_right_panel_width();
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = side_column_top();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  body.x = panel_x + 1;
  // The tab strip owns the dock's first interior row (the native strip and the
  // Lua side_panel kit both paint it there), so the panel's own rows start on
  // the second one and run to the panel's last row, which is content.
  body.y = panel_y + 2;
  body.w = std::max(1, panel_w - 2);
  body.h = std::max(0, panel_h - 2);
  return body;
}

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
  int panel_y = side_column_top();
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal, pane_layout::kNoEdges);

  std::string title = active_plugin_panel.empty() ? " Plugin " : " " + active_plugin_panel + " ";
  ui->draw_text(panel_x + 1,
                panel_y,
                ui_truncate_cells(title, panel_w - 2),
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  const TerminalBox body = plugin_panel_body();
  // Hand the model to a Lua UI handler when one is registered; it owns the
  // paint. Native fallback below stays byte-identical.
  SidePanelView view;
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = active_plugin_panel.empty() ? " Plugin " : " " + active_plugin_panel + " ";
  build_right_panel_tab_strip_view(view);
  render_right_panel_tab_strip(panel_x, panel_y, panel_w);

  std::vector<PluginPanelRow> rows;
  if (lua_api && !active_plugin_panel.empty())
  {
    rows = lua_api->plugin_panel_rows(active_plugin_panel);
  }

  if (rows.empty())
  {
    view.note = "No plugin panel content";
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(body.x, body.y, "No plugin panel content", theme.fg_comment, theme.bg_terminal);
    return;
  }

  // The window follows the selection the way the other lists do: a panel
  // longer than the dock keeps its highlighted row on screen.
  const int max_scroll = std::max(0, (int)rows.size() - body.h);
  plugin_panel_scroll = std::clamp(plugin_panel_scroll, 0, max_scroll);

  for (int row = 0; row < body.h; row++)
  {
    const int index = plugin_panel_scroll + row;
    if (index >= (int)rows.size())
    {
      break;
    }
    const PluginPanelRow &source = rows[(size_t)index];
    SidePanelRowView r;
    r.text = source.text;
    r.detail = source.detail;
    r.kind = source.kind;
    r.icon = source.icon;
    r.icon_fg = source.icon_fg;
    r.fg = source.fg >= 0 ? source.fg : theme.fg_terminal;
    r.bg = theme.bg_terminal;
    r.bold = source.bold;
    r.selected = source.selected;
    // Hover is the actionable row the pointer is on and that a click would
    // run; a selected row keeps its own paint.
    r.hovered = !source.selected && index == plugin_panel_hover_row;
    view.rows.push_back(std::move(r));
  }
  if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
  {
    return;
  }
  for (int row = 0; row < (int)view.rows.size(); row++)
  {
    const SidePanelRowView &r = view.rows[(size_t)row];
    const int row_fg = r.selected ? theme.fg_selection : r.fg;
    const int row_bg = r.selected ? theme.bg_selection : (r.hovered ? theme.bg_selection : r.bg);
    int draw_x = body.x;
    if (r.selected)
    {
      ui->draw_text(draw_x, body.y + row, "\u258C", theme.fg_selection, row_bg, true);
      draw_x += 1;
    }
    if (!r.icon.empty())
    {
      ui->draw_text(draw_x, body.y + row, r.icon, r.icon_fg >= 0 ? r.icon_fg : row_fg, row_bg);
      draw_x += (int)ui_cell_count(r.icon) + 1;
    }
    std::string text = ui_truncate_cells(r.text, body.w - (draw_x - body.x));
    ui->draw_text(draw_x, body.y + row, text, row_fg, row_bg, r.bold);
  }
}

bool Editor::handle_plugin_panel_mouse(int x, int y, bool is_click)
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_PLUGIN || !ui)
  {
    return false;
  }
  const int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return false;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = side_column_top();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  if (x < panel_x || x >= panel_x + panel_w || y < panel_y || y >= panel_y + panel_h)
  {
    if (plugin_panel_hover_row != -1)
    {
      plugin_panel_hover_row = -1;
      needs_redraw = true;
    }
    return false;
  }
  std::vector<PluginPanelRow> rows;
  if (lua_api && !active_plugin_panel.empty())
  {
    rows = lua_api->plugin_panel_rows(active_plugin_panel);
  }
  const TerminalBox body = plugin_panel_body();
  const int row = y - body.y;
  const int index = plugin_panel_scroll + row;
  const bool on_row = row >= 0 && row < body.h && index >= 0 && index < (int)rows.size();
  // Only a row that carries an action is a button: headers and console lines
  // stay flat, so the pointer highlight always means "this runs something".
  const bool actionable = on_row && !rows[(size_t)index].action.empty();
  const int hover = actionable ? index : -1;
  if (!is_click)
  {
    if (plugin_panel_hover_row != hover)
    {
      plugin_panel_hover_row = hover;
      needs_redraw = true;
    }
    return true;
  }
  focus_state = FOCUS_RIGHT_PANEL;
  needs_redraw = true;
  if (!actionable)
  {
    return true;
  }
  const std::string action = rows[(size_t)index].action;
  if (!lua_api)
  {
    return true;
  }
  lua_api->plugin_panel_rows(active_plugin_panel, action, index);
  return true;
}
