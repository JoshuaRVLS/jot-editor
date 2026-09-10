// Quick-pick list rendering (Lua-driven pickers with match highlighting).
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include "render/ui_internal.h"

using namespace ui_internal;
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_quick_pick()
{
  if (!show_quick_pick)
  {
    return;
  }

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();

  // Modal overlay: dim the editor underneath, matching the palette / popup /
  // LSP manager treatment, then draw the panel on top.
  ui->dim_rect({0, 0, screen_w, screen_h});

  int w = std::min(std::max(56, screen_w - 10), 112);
  int h = std::min(std::max(12, screen_h - 8), 26);
  if (screen_w < 62)
  {
    w = std::max(22, screen_w - 2);
  }
  if (screen_h < 16)
  {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 3);

  // Lua UI handler takes over rendering when registered.
  if (lua_api && lua_api->has_lua_ui_handler("quick_pick"))
  {
    QuickPickView view;
    view.title = quick_pick_title;
    view.query = quick_pick_query;
    view.selected = quick_pick_selected;
    view.all_count = (int)quick_pick_all_items.size();
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.items.reserve(quick_pick_items.size());
    for (const auto &item : quick_pick_items)
    {
      QuickPickItemView v;
      v.label = item.label;
      v.detail = item.detail;
      v.preview = item.preview;
      v.severity = item.severity;
      view.items.push_back(std::move(v));
    }
    if (lua_api->emit_quick_pick(view))
    {
      return;
    }
  }

  // Same panel surface convention as the palette / popup / LSP manager.
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
  std::string title = quick_pick_title.empty() ? " Quick Pick" : " " + quick_pick_title;
  ui_draw_panel_title(
      *ui, rect, ui_truncate_cells(title, w - 2), theme.fg_command, panel_theme.bg_command);

  std::string count =
      std::to_string(quick_pick_items.size()) + "/" + std::to_string(quick_pick_all_items.size());
  ui->draw_text(std::max(x + 1, x + w - (int)count.size() - 1),
                y,
                count,
                theme.fg_comment,
                panel_theme.bg_command);

  int input_y = y + 1;
  std::string query = "> " + quick_pick_query;
  ui->draw_text(x + 1,
                input_y,
                ui_truncate_cells(query, w - 2),
                theme.fg_selection,
                theme.bg_selection,
                true);

  // Divider between the input and the list (same treatment as the palette).
  ui->fill_rect(
      {x + 1, y + 2, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  int list_y = y + 3;
  int list_h = std::max(0, h - 5);
  int selected = std::clamp(quick_pick_selected, 0, std::max(0, (int)quick_pick_items.size() - 1));
  // Follow-window: the window only moves when the selection leaves it, so
  // hovering a visible row never shifts the list under the pointer.
  int start_idx = quick_pick_scroll;
  if (selected < start_idx)
  {
    start_idx = selected;
  }
  else if (selected >= start_idx + list_h)
  {
    start_idx = selected - list_h + 1;
  }
  start_idx = std::clamp(start_idx, 0, std::max(0, (int)quick_pick_items.size() - list_h));
  quick_pick_scroll = start_idx;

  if (quick_pick_items.empty())
  {
    std::string empty = quick_pick_query.empty() ? "Type to filter or search"
                                                 : "No matches for \"" + quick_pick_query + "\"";
    ui->draw_text(
        x + 2, list_y, ui_truncate_cells(empty, w - 4), theme.fg_comment, panel_theme.bg_command);
  }

  for (int row = 0; row < list_h; row++)
  {
    int idx = start_idx + row;
    if (idx < 0 || idx >= (int)quick_pick_items.size())
    {
      break;
    }
    const auto &item = quick_pick_items[(size_t)idx];
    bool is_selected = idx == selected;
    int fg = is_selected ? theme.fg_selection : theme.fg_command;
    int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;
    int row_y = list_y + row;
    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);

    // Accent bar on the selected row, matching the palette.
    if (is_selected)
    {
      ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
    }

    int detail_w = w >= 72 ? std::max(16, w / 3) : 0;
    int label_w = std::max(8, w - detail_w - 5);
    std::string label = ui_truncate_cells(item.label, label_w - 2);

    // Highlight the query substring inside the label when it appears
    // verbatim (case-insensitive); fuzzy scatter is left to the palette.
    std::vector<int> match;
    if (!quick_pick_query.empty() && !label.empty())
    {
      std::string hay = label;
      std::string needle = quick_pick_query;
      for (char &c : hay)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      for (char &c : needle)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      size_t pos = hay.find(needle);
      if (pos != std::string::npos && needle.size() == quick_pick_query.size())
      {
        for (size_t k = 0; k < needle.size(); k++)
        {
          match.push_back((int)(pos + k));
        }
      }
    }

    int text_x = x + 2 + (is_selected ? 1 : 0);
    int match_fg = is_selected ? theme.fg_selection : theme.fg_keyword;
    ui_draw_marked_text(*ui, text_x, row_y, label, match, fg, bg, match_fg);
    if (detail_w > 0 && !item.detail.empty())
    {
      ui->draw_text(x + w - detail_w - 1,
                    row_y,
                    ui_truncate_cells(item.detail, detail_w),
                    theme.fg_comment,
                    bg);
    }
  }

  std::string footer = "Enter open   Esc close   Up/Down move   PgUp/PgDn page";
  if (selected >= 0 && selected < (int)quick_pick_items.size()
      && !quick_pick_items[(size_t)selected].preview.empty())
  {
    footer = quick_pick_items[(size_t)selected].preview;
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

