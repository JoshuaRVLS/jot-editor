// Settings menu rendering: a quick-pick style centered panel listing every
// config key (defaults + Lua-registered) with its current value. Bools
// toggle on Enter; ints/strings open an inline input row at the panel's
// bottom. Painted through the normal cell grid, so it works identically in
// the terminal and GUI frontends (:settings / Ctrl+,).
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include "render/ui_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::string value_display(const SettingsEntry &e)
{
  // Booleans render as on/off for readability; everything else shows the
  // raw stored value.
  if (e.type == SettingsEntry::Type::Bool)
    return e.value == "true" ? "on" : "off";
  return e.value.empty() ? "(empty)" : e.value;
}

std::string truncate(const std::string &s, int max_len)
{
  if (max_len <= 0)
    return "";
  if (ui_cell_count(s) <= max_len)
    return s;
  if (max_len <= 3)
    return ui_take_cells(s, max_len);
  return ui_take_cells(s, max_len - 3) + "...";
}
} // namespace

void Editor::render_settings_menu()
{
  if (!show_settings_menu)
    return;

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();

  // Modal overlay: dim the editor underneath, same as quick pick / palette.
  ui->dim_rect({0, 0, screen_w, screen_h});

  const int w = std::min(std::max(60, screen_w - 12), 96);
  const int max_rows = std::min(18, std::max(8, screen_h - 10));
  const int h = std::min(std::max((int)settings_entries.size() + 5, 9), max_rows);
  const int x = std::max(0, (screen_w - w) / 2);
  const int y = std::max(1, (screen_h - h) / 3);

  settings_panel_x = x;
  settings_panel_y = y;
  settings_panel_w = w;
  settings_panel_h = h;

  settings_selected = std::clamp(settings_selected, 0,
                                 std::max(0, (int)settings_entries.size() - 1));
  const int list_y = y + 2;
  const int list_h = std::max(1, h - 4);
  const int max_scroll = std::max(0, (int)settings_entries.size() - list_h);
  settings_scroll = std::clamp(settings_scroll, 0, max_scroll);
  // Keep the selection visible: the input handler moves settings_selected;
  // the window follows it here on the next frame (up before down, so a
  // selection at the very bottom stays fully visible).
  if (settings_selected < settings_scroll)
    settings_scroll = settings_selected;
  if (settings_selected >= settings_scroll + list_h)
    settings_scroll = settings_selected - list_h + 1;

  // Invalidate hit rects first; only visible rows get fresh rects below,
  // so mouse hit-testing can never match a scrolled-away row's stale rect.
  for (SettingsEntry &e : settings_entries)
    e.row_y = -1;
  for (int row = 0; row < list_h; row++)
  {
    const int idx = settings_scroll + row;
    if (idx < 0 || idx >= (int)settings_entries.size())
      break;
    SettingsEntry &e = settings_entries[(size_t)idx];
    e.row_x = x + 1;
    e.row_y = list_y + row;
    e.row_w = w - 2;
  }

  // Lua UI handler takes over rendering when registered (the panel then
  // paints as a float on top of the modal scrim, like every other modal
  // surface). The layout + hit rects above are shared by both paths, so
  // mouse clicks and cursor placement stay exact.
  if (lua_api && lua_api->has_lua_ui_handler("settings"))
  {
    SettingsView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.selected = settings_selected;
    view.scroll = settings_scroll;
    view.all_count = (int)settings_entries.size();
    view.items.reserve((size_t)list_h);
    for (int row = 0; row < list_h; row++)
    {
      const int idx = settings_scroll + row;
      if (idx < 0 || idx >= (int)settings_entries.size())
        break;
      const SettingsEntry &e = settings_entries[(size_t)idx];
      SettingsItemView item;
      item.label = e.label;
      item.value = value_display(e);
      item.type = e.type == SettingsEntry::Type::Bool
                      ? "bool"
                      : (e.type == SettingsEntry::Type::Int ? "int" : "string");
      item.selected = idx == settings_selected;
      item.editing = e.editing;
      item.edit_input = e.edit_input;
      view.items.push_back(std::move(item));
    }
    lua_api->emit_settings(view);
    return;
  }

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui, rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
  ui_draw_panel_title(*ui, rect, ui_truncate_cells(" Settings", w - 2), theme.fg_command,
                      panel_theme.bg_command);

  ui->draw_text(std::max(x + 1, x + w - 12),
                y,
                std::to_string(settings_entries.size()) + " keys",
                theme.fg_comment,
                panel_theme.bg_command);

  // Divider between the title and the list.
  ui->fill_rect(
      {x + 1, y + 1, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  const int key_w = std::max(18, (int)(w * 0.55));
  const int val_x = x + 1 + key_w;

  for (int row = 0; row < list_h; row++)
  {
    const int idx = settings_scroll + row;
    if (idx < 0 || idx >= (int)settings_entries.size())
      break;
    SettingsEntry &e = settings_entries[(size_t)idx];

    const bool selected = idx == settings_selected;
    const int fg = selected ? theme.fg_selection : theme.fg_command;
    const int bg = selected ? theme.bg_selection : panel_theme.bg_command;
    ui->fill_rect({e.row_x, e.row_y, e.row_w, 1}, " ", fg, bg);
    if (selected)
      // Nerd Font chevron (nf-fa-chevron-right) marks the selected row.
      ui->draw_text(e.row_x, e.row_y, "\uF054", theme.fg_selection, bg);

    ui->draw_text(e.row_x + 1, e.row_y, truncate(e.label, key_w - 3), fg, bg);

    if (selected && e.editing)
    {
      // Inline edit: show the input text on the selected row with a "> "
      // prompt (the text cursor is placed by place_settings_cursor).
      std::string input = "> " + e.edit_input;
      ui->draw_text(val_x, e.row_y, truncate(input, w - key_w - 4), theme.fg_selection, bg);
    }
    else
    {
      std::string val = value_display(e);
      const int val_fg = selected ? theme.fg_selection : theme.fg_keyword;
      ui->draw_text(val_x, e.row_y, truncate(val, w - key_w - 4), val_fg, bg);
    }
  }

  // The footer row keeps its background but no longer spells out the bindings.
  ui->fill_rect({rect.x + 1, rect.y + rect.h - 1, std::max(1, rect.w - 2), 1},
                " ",
                theme.fg_comment,
                panel_theme.bg_command);
}

void Editor::place_settings_cursor()
{
  if (!show_settings_menu || settings_entries.empty())
    return;
  const int idx = std::clamp(settings_selected, 0, (int)settings_entries.size() - 1);
  const SettingsEntry &e = settings_entries[(size_t)idx];
  if (!e.editing)
  {
    ui->hide_cursor();
    return;
  }
  const int key_w = std::max(18, (int)(settings_panel_w * 0.55));
  const int val_x = settings_panel_x + 1 + key_w;
  // "> " prompt, then the text cursor at the end of the input.
  const int cx = val_x + 2 + (int)ui_cell_count(e.edit_input);
  ui->set_cursor(cx, e.row_y);
}