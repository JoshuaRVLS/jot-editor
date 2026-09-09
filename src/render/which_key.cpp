// Which-key popup: pending key chord and the actions it can complete.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_which_key_panel()
{
  // Which-key style helper docked just above the status line: a pressed chord
  // ("Ctrl+T") prefixes longer Lua keymap sequences ("Ctrl+T N") — the panel
  // lists the next-chord options.
  if (!show_which_key)
  {
    return;
  }

  std::string path;
  std::vector<PluginKeymapChild> children;
  if (!lua_api || which_key_path.empty())
  {
    close_which_key();
    return;
  }
  for (size_t i = 0; i < which_key_path.size(); i++)
  {
    if (i > 0)
    {
      path += ' ';
    }
    path += which_key_path[i];
  }
  std::string crumb = path;
  children = lua_api->plugin_keymap_children(path, "editor");
  if (children.empty())
  {
    // Keymaps were reloaded/cleared while the helper was open.
    close_which_key();
    return;
  }

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  int w = std::clamp(screen_w - 8, 46, 92);
  if (screen_w < 56)
  {
    w = std::max(30, screen_w - 2);
  }
  const int max_rows = std::max(3, std::min((int)children.size(),
                                            std::max(3, screen_h - status_height - 8)));
  int h = max_rows + 3;
  int x = std::max(1, (screen_w - w) / 2);
  int y = std::max(1, screen_h - status_height - h - 1);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui,
                rect,
                {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border,
                 panel_theme.bg_command});

  // Title: breadcrumb path ("Ctrl+T") plus the optional group title.
  std::string title = " " + crumb;
  if (lua_api)
  {
    const std::string group_title =
        lua_api->plugin_keymap_group_title(which_key_path.back(), "editor");
    if (!group_title.empty())
    {
      title += "  ·  " + group_title;
    }
  }
  ui_draw_panel_title(*ui, rect, ui_truncate_cells(title, w - 2), theme.fg_command,
                      panel_theme.bg_command);

  // Hint on the right of the title row.
  const char *unit = "key";
  std::string hint = children.size() > 1
                         ? std::to_string(children.size()) + " " + unit + "s"
                         : "1 " + std::string(unit);
  ui->draw_text(std::max(x + 1, x + w - (int)hint.size() - 1),
                y,
                hint,
                theme.fg_comment,
                panel_theme.bg_command);

  // Divider.
  ui->fill_rect({x + 1, y + 1, std::max(1, w - 2), 1}, "─", theme.fg_panel_border,
                panel_theme.bg_command);

  int key_w = 0;
  for (const auto &child : children)
  {
    const std::string display = child.key == " " ? "Space" : child.key;
    int cells = 0;
    for (unsigned char c : display)
    {
      cells += c < 128 ? 1 : 2;
    }
    key_w = std::max(key_w, cells);
  }
  key_w = std::clamp(key_w + 3, 6, std::max(6, w / 3));
  int detail_w = std::max(0, w - key_w - 5);

  int selected = std::clamp(which_key_selected, 0, (int)children.size() - 1);
  int start_idx = std::max(0, selected - max_rows + 1);
  if (start_idx + max_rows > (int)children.size())
  {
    start_idx = std::max(0, (int)children.size() - max_rows);
  }

  int list_y = y + 2;
  for (int row = 0; row < max_rows; row++)
  {
    int idx = start_idx + row;
    if (idx < 0 || idx >= (int)children.size())
    {
      break;
    }
    const auto &child = children[(size_t)idx];
    bool is_selected = idx == selected;
    int fg = is_selected ? theme.fg_selection : theme.fg_command;
    int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;
    int row_y = list_y + row;

    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);
    if (is_selected)
    {
      ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
    }

    // Key in the accent color, detail beside it; subgroups get a marker.
    std::string key_text = child.key == " " ? "Space" : child.key;
    key_text = child.group ? "▸ " + key_text : key_text;
    ui->draw_text(x + 3 + (is_selected ? 1 : 0), row_y, ui_truncate_cells(key_text, key_w),
                  child.group ? theme.fg_keyword : fg, bg, !child.group);
    std::string detail = child.detail;
    std::string marker = child.group ? " …" : "";
    ui->draw_text(x + 3 + key_w, row_y, ui_truncate_cells(detail + marker, detail_w),
                  child.group ? theme.fg_comment : fg, bg);
  }

  std::string footer;
  if (which_key_path.size() > 1)
  {
    footer = "Esc close · Backspace up · Enter run";
  }
  else
  {
    footer = "Esc close · Backspace up · arrow keys to move";
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

