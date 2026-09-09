// Command palette rendering and its terminal-cursor placement.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include "render/ui_internal.h"

using namespace ui_internal;
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  struct PaletteLayout
  {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
  };

  // Shared geometry for the command palette panel, so the renderer and the
  // terminal-cursor placement always agree on where the input row is.
  PaletteLayout command_palette_layout(int screen_w, int screen_h, size_t result_count)
  {
    const int max_items = std::min(8, (int)result_count);
    int w = std::min(std::max(64, screen_w - 12), 116);
    int h = 5 + max_items;
    if (screen_w < 66)
    {
      w = std::max(36, screen_w - 2);
    }
    h = std::clamp(h, 7, std::max(7, screen_h - 4));
    return {std::max(0, (screen_w - w) / 2), std::max(1, (screen_h - h) / 3), w, h};
  }

} // namespace

void Editor::render_command_palette()
{
  if (!show_command_palette)
    return;

  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();

  // Modal overlay: dim the editor underneath, matching the popup / LSP
  // manager / telescope treatment, then draw the panel on top.
  ui->dim_rect({0, 0, screen_w, screen_h});

  // Layout mirrors quick-pick: a centered floating panel instead of a
  // bottom-docked strip.
  const PaletteLayout layout =
      command_palette_layout(screen_w, screen_h, command_palette_results.size());
  const int w = layout.w;
  const int h = layout.h;
  const int x = layout.x;
  const int y = layout.y;

  // A registered Lua UI handler (jot.ui.handler("command_palette", fn))
  // renders the whole surface from the state below; the native render is
  // skipped when it returns true.
  if (lua_api && lua_api->has_lua_ui_handler("command_palette"))
  {
    PaletteView view;
    view.query = command_palette_query;
    view.selected = command_palette_selected;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.screen_w = screen_w;
    view.screen_h = screen_h;
    view.results.reserve(command_palette_results.size());
    for (const auto &r : command_palette_results)
    {
      PaletteItemView item;
      item.label = r.label;
      item.category = r.category;
      item.detail = r.detail;
      item.match = r.match;
      view.results.push_back(std::move(item));
    }
    if (lua_api->emit_command_palette(view))
    {
      return;
    }
  }

  // Panel surface uses the theme's panel-background slot (bg_panel_border),
  // the same convention as the popup and LSP manager.
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
  ui_draw_panel_title(*ui, rect, " Command Palette", theme.fg_command, panel_theme.bg_command);

  std::string count = std::to_string(command_palette_results.size())
                      + (command_palette_results.size() == 1 ? " result" : " results");
  ui->draw_text(std::max(x + 1, x + w - (int)count.size() - 1),
                y,
                count,
                theme.fg_comment,
                panel_theme.bg_command);

  // Input row.
  int input_y = y + 1;
  UIRect input_rect = {x + 1, input_y, std::max(1, w - 2), 1};
  ui->fill_rect(input_rect, " ", theme.fg_selection, theme.bg_selection);
  std::string query = command_palette_query;
  if (query.empty() || query[0] != ':')
  {
    query = ":" + query;
  }
  ui->draw_text(x + 1,
                input_y,
                ui_truncate_cells(query, w - 3),
                theme.fg_selection,
                theme.bg_selection,
                true);

  // Divider between the input and the list.
  int div_y = y + 2;
  ui->fill_rect(
      {x + 1, div_y, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  const int max_items = std::min(8, (int)command_palette_results.size());
  int list_y = y + 3;
  if (!command_palette_results.empty())
  {
    int selected = std::clamp(command_palette_selected, 0, (int)command_palette_results.size() - 1);
    int start_idx = std::max(0, selected - max_items + 1);
    if (start_idx + max_items > (int)command_palette_results.size())
    {
      start_idx = std::max(0, (int)command_palette_results.size() - max_items);
    }

    for (int row = 0; row < max_items; row++)
    {
      int idx = start_idx + row;
      if (idx < 0 || idx >= (int)command_palette_results.size())
      {
        break;
      }
      int row_y = list_y + row;
      bool is_selected = (idx == selected);
      int fg = is_selected ? theme.fg_selection : theme.fg_command;
      int bg = is_selected ? theme.bg_selection : panel_theme.bg_command;

      UIRect row_rect = {x + 1, row_y, std::max(1, w - 2), 1};
      ui->fill_rect(row_rect, " ", fg, bg);

      // Accent bar on the selected row.
      if (is_selected)
      {
        ui->draw_text(x + 1, row_y, "▎", theme.fg_selection, bg);
      }

      const auto &suggestion = command_palette_results[idx];
      std::string category = suggestion.category;
      if ((int)category.size() > 12)
      {
        category = category.substr(0, 9) + "...";
      }

      int cat_w = std::min(14, std::max(8, w / 6));
      int detail_w = std::max(0, w / 3);
      int label_w = std::max(8, w - cat_w - detail_w - 5);

      // Truncate the label first, then drop match offsets that fell off the
      // truncated tail so highlighting never points outside the drawn text.
      int label_max = std::max(0, label_w - 2);
      std::string label = ui_truncate_cells(suggestion.label, label_max);
      std::vector<int> match;
      for (int m : suggestion.match)
      {
        if (m >= 0 && m < (int)label.size())
        {
          match.push_back(m);
        }
      }
      std::string detail = suggestion.detail;
      detail = ui_truncate_cells(detail, detail_w);

      int text_x = x + 2 + (is_selected ? 1 : 0);
      // Matched characters pop in the theme accent color (plain rows) or
      // stay bold black-on-cyan (selected rows) while the rest stays plain,
      // so the emphasis is visible in both states.
      int match_fg = is_selected ? theme.fg_selection : theme.fg_keyword;
      ui_draw_marked_text(*ui, text_x, row_y, label, match, fg, bg, match_fg);
      if (w > 40)
      {
        ui->draw_text(
            x + 1 + std::max(0, w - cat_w - detail_w - 2), row_y, category, theme.fg_comment, bg);
        ui->draw_text(x + 1 + std::max(0, w - detail_w - 2), row_y, detail, theme.fg_comment, bg);
      }
    }
  }
  else
  {
    std::string empty = command_palette_query.empty()
                            ? "Type a command or search..."
                            : "No matches for \"" + command_palette_query + "\"";
    ui->draw_text(
        x + 2, list_y, ui_truncate_cells(empty, w - 4), theme.fg_comment, panel_theme.bg_command);
  }

  ui_draw_footer(*ui,
                 rect,
                 "Enter run   Tab complete   Esc close   Up/Down move   "
                 "PgUp/PgDn page",
                 theme.fg_comment,
                 panel_theme.bg_command);
}

void Editor::place_command_palette_cursor()
{
  if (!show_command_palette)
    return;
  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();
  const PaletteLayout layout =
      command_palette_layout(screen_w, screen_h, command_palette_results.size());
  // Mirrors render_command_palette: the input row is one below the panel
  // title, the query is drawn with a leading ":" at x + 1, truncated to the
  // input width.
  std::string query = command_palette_query;
  if (query.empty() || query[0] != ':')
  {
    query = ":" + query;
  }
  const std::string drawn = ui_truncate_cells(query, std::max(0, layout.w - 3));
  ui->set_cursor(layout.x + 1 + ui_cell_count(drawn), layout.y + 1);
}

