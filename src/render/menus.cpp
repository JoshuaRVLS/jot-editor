// Menu bar, dropdowns, and the right-click context menu.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_context_menu()
{
  if (!show_context_menu || context_menu_items.empty())
    return;

  int w = std::max(1, context_menu_w);
  int h = std::max(1, context_menu_h);
  int x = context_menu_x;
  int y = context_menu_y;

  if (x + w > ui->get_render_width())
    x = std::max(0, ui->get_render_width() - w);
  if (y + h > ui->get_height())
    y = std::max(0, ui->get_height() - h);

  // A registered Lua UI handler paints the menu from this state; the native
  // rect (already clamped) is passed through so mouse hits stay aligned.
  if (lua_api && lua_api->has_lua_ui_handler("context_menu"))
  {
    ContextMenuView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.selected = context_menu_selected;
    for (const auto &item : context_menu_items)
    {
      ContextMenuItemView iv;
      iv.label = item.label;
      iv.enabled = item.enabled;
      view.items.push_back(std::move(iv));
    }
    if (lua_api->emit_context_menu(view))
    {
      return;
    }
  }

  // Same panel surface convention as the popup / palette / quick-pick.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(context_menu_items.size());
  for (size_t i = 0; i < context_menu_items.size(); i++)
  {
    rows.push_back({context_menu_items[i].label,
                    (int)i == context_menu_selected,
                    context_menu_items[i].enabled});
  }
  ui_draw_selectable_rows(*ui,
                          x + 1,
                          y + 1,
                          std::max(1, w - 2),
                          std::max(0, h - 2),
                          rows,
                          {theme.fg_command,
                           panel_theme.bg_command,
                           theme.fg_selection,
                           theme.bg_selection,
                           theme.fg_comment,
                           panel_theme.bg_command});
}

std::vector<Editor::MenuBarMenu> Editor::build_menu_bar_model() const
{
  return {
      {"File",
       {{"New File", MENU_ACTION_NEW_FILE},
        {"Open File...", MENU_ACTION_OPEN_FINDER},
        {"Save", MENU_ACTION_SAVE},
        {"Save As...", MENU_ACTION_SAVE_AS},
        {"Close File", MENU_ACTION_CLOSE_FILE},
        {"Quit", MENU_ACTION_QUIT}}},
      {"Edit",
       {{"Undo", MENU_ACTION_UNDO},
        {"Redo", MENU_ACTION_REDO},
        {"Cut", MENU_ACTION_CUT},
        {"Copy", MENU_ACTION_COPY},
        {"Paste", MENU_ACTION_PASTE},
        {"Find", MENU_ACTION_COMMAND, "Toggle Search"},
        {"Format Document", MENU_ACTION_COMMAND, "Format Document"},
        {"Trim Trailing Whitespace", MENU_ACTION_COMMAND, "Trim Trailing Whitespace"}}},
      {"Selection",
       {{"Select All", MENU_ACTION_SELECT_ALL},
        {"Select Line", MENU_ACTION_SELECT_LINE},
        {"Duplicate Line", MENU_ACTION_DUPLICATE_LINE},
        {"Move Line Up", MENU_ACTION_MOVE_LINE_UP},
        {"Move Line Down", MENU_ACTION_MOVE_LINE_DOWN},
        {"Toggle Comment", MENU_ACTION_TOGGLE_COMMENT}}},
      {"View",
       {{"Command Palette", MENU_ACTION_COMMAND_PALETTE},
        {"Explorer", MENU_ACTION_TOGGLE_SIDEBAR},
        {"Toggle Minimap", MENU_ACTION_TOGGLE_MINIMAP},
        {"Color Theme", MENU_ACTION_THEME},
        {"Home", MENU_ACTION_HOME}}},
      {"Go",
       {{"Go to Line...", MENU_ACTION_COMMAND, ":line "},
        {"Go to Definition", MENU_ACTION_LSP_DEFINITION},
        {"Back", MENU_ACTION_LSP_BACK}}},
      {"Debug",
       {{"Start Debugging", MENU_ACTION_COMMAND, ":debug "},
        {"Continue", MENU_ACTION_DEBUG_CONTINUE},
        {"Pause", MENU_ACTION_DEBUG_PAUSE},
        {"Step Over", MENU_ACTION_DEBUG_STEP_OVER},
        {"Step Into", MENU_ACTION_DEBUG_STEP_IN},
        {"Step Out", MENU_ACTION_DEBUG_STEP_OUT},
        {"Stop", MENU_ACTION_DEBUG_STOP},
        {"Debug Panel", MENU_ACTION_TOGGLE_DEBUG_PANEL}}},
      {"Terminal",
       {{"Toggle Terminal", MENU_ACTION_TOGGLE_TERMINAL},
        {"New Terminal", MENU_ACTION_NEW_TERMINAL},
        {"Zoom Terminal", MENU_ACTION_TERMINAL_ZOOM},
        {"Run Task...", MENU_ACTION_TASKS},
        {"Rerun Last Task", MENU_ACTION_RERUN_TASK}}},
      {"Help",
       {{"Help", MENU_ACTION_HELP},
        {"Install Language Server...", MENU_ACTION_COMMAND, ":lspinstall "},
        {"Remove Language Server...", MENU_ACTION_COMMAND, ":lspremove "},
        {"Tree-sitter Status", MENU_ACTION_COMMAND, ":tsstatus"},
        {"LSP Status", MENU_ACTION_COMMAND, ":lspstatus"},
        {"Git Status", MENU_ACTION_COMMAND, ":gitstatus"}}},
  };
}

void Editor::render_menu_bar()
{
  if (!kTopBarVisible)
  {
    return;
  }
  int w = ui ? ui->get_render_width() : 0;
  if (w <= 0)
  {
    return;
  }

  UIRect row = {0, 0, w, 1};
  ui->fill_rect(row, " ", theme.fg_status, theme.bg_status);
  menu_bar_segments.clear();

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  int x = 0;
  for (int i = 0; i < (int)menus.size(); i++)
  {
    std::string label = " " + menus[i].label + " ";
    int label_w = (int)label.size();
    if (x + label_w > w)
    {
      break;
    }
    bool active = show_menu_bar_dropdown && i == menu_bar_active;
    int fg = active ? theme.fg_selection : theme.fg_status;
    int bg = active ? theme.bg_selection : theme.bg_status;
    ui->draw_text(x, 0, label, fg, bg, active);
    menu_bar_segments.push_back({i, x, x + label_w});
    x += label_w;
  }
}

void Editor::render_menu_dropdown()
{
  if (!kTopBarVisible || !show_menu_bar_dropdown || !ui)
  {
    return;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size())
  {
    return;
  }

  const auto &menu = menus[menu_bar_active];
  if (menu.items.empty())
  {
    return;
  }

  int label_x = 0;
  for (const auto &segment : menu_bar_segments)
  {
    if (segment.menu_index == menu_bar_active)
    {
      label_x = segment.x;
      break;
    }
  }

  int max_label = 0;
  for (const auto &item : menu.items)
  {
    max_label = std::max(max_label, (int)item.label.size());
  }

  int w = std::max(18, max_label + 4);
  int h = (int)menu.items.size() + 2;
  int x = std::clamp(label_x, 0, std::max(0, ui->get_render_width() - w));
  int y = topbar_height();
  int max_h = std::max(1, ui->get_height() - y - status_height);
  h = std::min(h, max_h);

  // A registered Lua UI handler paints the dropdown from this state; the rect
  // stays native so the bar highlight and mouse hover keep their hit regions.
  if (lua_api && lua_api->has_lua_ui_handler("menu_dropdown"))
  {
    MenuDropdownView view;
    view.menu_label = menu.label;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.selected = menu_bar_selected;
    for (const auto &item : menu.items)
    {
      MenuItemView iv;
      iv.label = item.label;
      iv.enabled = item.enabled;
      view.items.push_back(std::move(iv));
    }
    if (lua_api->emit_menu_dropdown(view))
    {
      return;
    }
  }

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(menu.items.size());
  for (int i = 0; i < (int)menu.items.size(); i++)
  {
    rows.push_back(
        {menu.items[(size_t)i].label, i == menu_bar_selected, menu.items[(size_t)i].enabled});
  }
  ui_draw_selectable_rows(*ui,
                          x + 1,
                          y + 1,
                          std::max(1, w - 2),
                          std::max(0, h - 2),
                          rows,
                          {theme.fg_command,
                           panel_theme.bg_command,
                           theme.fg_selection,
                           theme.bg_selection,
                           theme.fg_comment,
                           panel_theme.bg_command});
}

