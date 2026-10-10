// Command palette rendering and its terminal-cursor placement.
//
// The palette is Helix's command menu, bottom-anchored: the prompt row is the
// last row of the screen (the statusline slot), the results grid sits directly
// above it filled column by column, and the selected row's explanation box
// floats over the space above the grid. build_command_palette_layout() is the
// single source of that geometry, shared with the mouse hit-test and the Lua
// view.
#include "core/editor.h"
#include "lua/api.h"
#include "render/ui_internal.h"
#include "ui/components.h"
#include "ui/text.h"

using namespace ui_internal;
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // Content rows the explanation box reserves (description, aliases, args). The
  // grid keeps this much headroom even when the selected row's box is shorter,
  // so moving the selection never shifts the grid.
  constexpr int kPaletteHelpRows = 4;
  // The description wraps at this many cells (or the screen, on a narrow one).
  constexpr int kPaletteHelpWrap = 76;
  // A grid column never gets narrower than this, and a label is cut to the
  // column minus one leading and one trailing cell.
  constexpr int kPaletteMinCell = 12;

  // Greedy wrap at cell boundaries: the widest prefix that fits, backed up to
  // its last space, with a hard cut for a word wider than the line. Runs out of
  // lines by ellipsizing the last one.
  std::vector<std::string> wrap_cells(const std::string &text, int max_cells, int max_lines)
  {
    std::vector<std::string> lines;
    if (text.empty() || max_cells <= 0 || max_lines <= 0)
    {
      return lines;
    }
    std::string rest = text;
    while (!rest.empty() && (int)lines.size() < max_lines)
    {
      if (ui_cell_count(rest) <= max_cells)
      {
        lines.push_back(rest);
        return lines;
      }
      std::string head = ui_take_cells(rest, max_cells);
      const size_t brk = head.find_last_of(' ');
      if (brk == std::string::npos || brk == 0)
      {
        lines.push_back(head);
        rest.erase(0, head.size());
      }
      else
      {
        lines.push_back(head.substr(0, brk));
        rest.erase(0, brk + 1);
      }
      while (!rest.empty() && rest[0] == ' ')
      {
        rest.erase(0, 1);
      }
    }
    if (!rest.empty() && !lines.empty())
    {
      lines.back() = ui_take_cells(lines.back(), std::max(1, max_cells - 1)) + "\u2026";
    }
    return lines;
  }
} // namespace

CommandPaletteLayout Editor::build_command_palette_layout()
{
  CommandPaletteLayout layout;
  if (!ui)
  {
    return layout;
  }
  const int screen_w = std::max(1, ui->get_render_width());
  const int screen_h = std::max(2, ui->get_height());
  layout.screen_w = screen_w;
  layout.screen_h = screen_h;
  layout.x = 0;
  layout.w = screen_w;
  layout.prompt_y = screen_h - 1;
  layout.result_count = (int)command_palette_results.size();

  // The explanation box for the selected row: curated help when there is any,
  // the one-line detail otherwise (so every command answers "what does this
  // do"). The box is content-sized and caps its content rows.
  std::vector<std::string> help_lines;
  int help_for = -1;
  if (layout.result_count > 0)
  {
    const int sel = std::clamp(command_palette_selected, 0, layout.result_count - 1);
    const CommandPaletteSuggestion &selected = command_palette_results[(size_t)sel];
    const std::string &description = selected.help.empty() ? selected.detail : selected.help;
    if (!description.empty() || !selected.aliases.empty() || !selected.args.empty())
    {
      help_for = sel;
      const int extra = (selected.aliases.empty() ? 0 : 1) + (selected.args.empty() ? 0 : 1);
      const int desc_rows = std::max(1, kPaletteHelpRows - extra);
      const int wrap_w = std::max(12, std::min(kPaletteHelpWrap, screen_w - 6));
      const std::vector<std::string> desc = wrap_cells(description, wrap_w, desc_rows);
      help_lines.insert(help_lines.end(), desc.begin(), desc.end());
      if (!selected.aliases.empty())
      {
        help_lines.push_back("Aliases: " + selected.aliases);
      }
      if (!selected.args.empty())
      {
        help_lines.push_back("Args: " + selected.args);
      }
    }
  }

  // The grid: uniform columns as wide as the widest label needs, filled column
  // by column. Long labels (a path) widen the column until one fills the row.
  int max_label = 1;
  for (const auto &result : command_palette_results)
  {
    max_label = std::max(max_label, ui_cell_count(result.label));
  }
  layout.grid_col_w = std::clamp(max_label + 2, kPaletteMinCell, screen_w);
  layout.grid_cols = std::max(1, screen_w / layout.grid_col_w);
  layout.rows_per_col = layout.result_count == 0
                            ? 0
                            : (layout.result_count + layout.grid_cols - 1) / layout.grid_cols;

  // The grid keeps the help box's headroom while results are up, so switching
  // rows never resizes it; the window scrolls instead.
  const int help_reserve = help_for >= 0 ? kPaletteHelpRows : 0;
  const int max_grid_rows = std::max(1, layout.prompt_y - help_reserve);
  layout.grid_rows = layout.result_count == 0 ? 0 : std::min(layout.rows_per_col, max_grid_rows);
  layout.grid_x = 0;
  layout.grid_y = layout.prompt_y - std::max(1, layout.grid_rows);

  // Follow window: only move when the selected row leaves it, so a visible row
  // never shifts under the pointer.
  if (layout.grid_rows > 0 && layout.rows_per_col > 0)
  {
    const int sel = std::clamp(command_palette_selected, 0, layout.result_count - 1);
    const int sel_row = sel % layout.rows_per_col;
    int first = std::clamp(command_palette_scroll, 0, std::max(0, layout.rows_per_col - 1));
    if (sel_row < first)
    {
      first = sel_row;
    }
    else if (sel_row >= first + layout.grid_rows)
    {
      first = sel_row - layout.grid_rows + 1;
    }
    first = std::clamp(first, 0, std::max(0, layout.rows_per_col - layout.grid_rows));
    command_palette_scroll = first;
    layout.grid_first_row = first;
  }

  // The box sits directly on the grid's top, rising as far as the text needs.
  // On a screen too short for it, the tail rows go first (aliases, args).
  if (help_for >= 0)
  {
    if (layout.grid_y < 2 + (int)help_lines.size())
    {
      const int keep = std::max(0, layout.grid_y - 2);
      help_lines.resize(std::min((size_t)keep, help_lines.size()));
      if (help_lines.empty())
      {
        help_for = -1;
      }
    }
    if (help_for >= 0)
    {
      int max_line = 1;
      for (const std::string &line : help_lines)
      {
        max_line = std::max(max_line, ui_cell_count(line));
      }
      layout.help_w = std::min(screen_w, std::max(kPaletteMinCell, max_line + 4));
      layout.help_h = (int)help_lines.size() + 2;
      layout.help_x = 0;
      layout.help_y = std::max(0, layout.grid_y - layout.help_h);
      layout.help_for = help_for;
      layout.help_lines = std::move(help_lines);
    }
  }

  layout.y = layout.help_for >= 0 ? layout.help_y : layout.grid_y;
  layout.h = layout.prompt_y - layout.y + 1;
  return layout;
}

void Editor::render_command_palette()
{
  if (!show_command_palette)
  {
    return;
  }

  const CommandPaletteLayout layout = build_command_palette_layout();
  const int x = layout.x;
  const int y = layout.y;
  const int w = layout.w;

  // A registered Lua UI handler (jot.ui.handler("command_palette", fn))
  // renders the whole surface from the state below; the native render is
  // skipped when it returns true.
  if (lua_api && lua_api->has_lua_ui_handler("command_palette"))
  {
    PaletteView view;
    view.query = command_palette_query;
    view.selected = command_palette_selected;
    view.x = layout.x;
    view.y = layout.y;
    view.w = layout.w;
    view.h = layout.h;
    view.screen_w = layout.screen_w;
    view.screen_h = layout.screen_h;
    view.prompt_y = layout.prompt_y;
    view.grid_x = layout.grid_x;
    view.grid_y = layout.grid_y;
    view.grid_cols = layout.grid_cols;
    view.grid_col_w = layout.grid_col_w;
    view.grid_rows = layout.grid_rows;
    view.grid_first_row = layout.grid_first_row;
    view.rows_per_col = layout.rows_per_col;
    view.help_x = layout.help_x;
    view.help_y = layout.help_y;
    view.help_w = layout.help_w;
    view.help_h = layout.help_h;
    view.help_for = layout.help_for;
    view.help_lines = layout.help_lines;
    view.help_fg = theme.fg_command;
    view.help_bg = theme.bg_command;
    view.help_border_fg = theme.fg_panel_border;
    view.results.reserve(command_palette_results.size());
    for (const auto &r : command_palette_results)
    {
      PaletteItemView item;
      item.label = r.label;
      item.category = r.category;
      item.detail = r.detail;
      item.help = r.help;
      item.aliases = r.aliases;
      item.args = r.args;
      item.match = r.match;
      view.results.push_back(std::move(item));
    }
    if (lua_api->emit_command_palette(view))
    {
      return;
    }
  }

  // The explanation box: a content-sized frame over the space the grid left
  // free, so the selected command says what it does, its aliases and its
  // argument shape.
  if (layout.help_for >= 0 && !layout.help_lines.empty())
  {
    const UIRect box = {layout.help_x, layout.help_y, layout.help_w, layout.help_h};
    ui->fill_rect(box, " ", theme.fg_command, theme.bg_command);
    ui->draw_border(box, theme.fg_panel_border, theme.bg_command);
    for (int i = 0; i < (int)layout.help_lines.size(); i++)
    {
      ui->draw_text(layout.help_x + 2,
                    layout.help_y + 1 + i,
                    ui_truncate_cells(layout.help_lines[(size_t)i], std::max(0, layout.help_w - 4)),
                    theme.fg_command,
                    theme.bg_command);
    }
  }

  // The prompt row IS the statusline row (the palette renders after the
  // statusline, so it paints over it while open). Full-width, inverted,
  // no box around it.
  const int input_y = layout.prompt_y;
  ui->fill_rect({x, input_y, w, 1}, " ", theme.fg_selection, theme.bg_selection);
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

  // The suggestion surface: the whole grid band takes the theme's command
  // background (the Pmenu slot), not the buffer's, so the results read as their
  // own pane instead of blending into the text behind them. The fill covers
  // every column of every grid row -- including the padding after the last
  // result and the empty-state row -- because the Lua kit pads its rows to the
  // full width and the two painters have to agree cell for cell.
  ui->fill_rect({layout.grid_x, layout.grid_y, layout.w, std::max(1, layout.grid_rows)},
                " ",
                theme.fg_command,
                theme.bg_command);

  if (layout.result_count == 0)
  {
    std::string empty = command_palette_query.empty()
                            ? "Type a command or search..."
                            : "No matches for \"" + command_palette_query + "\"";
    ui->draw_text(
        x + 2, layout.grid_y, ui_truncate_cells(empty, w - 4), theme.fg_comment, theme.bg_command);
    return;
  }

  // The grid: one cell per result, the selected one on the selection pair.
  // Cell labels keep their column alignment whichever row is selected.
  const int sel = std::clamp(command_palette_selected, 0, layout.result_count - 1);
  for (int row = 0; row < layout.grid_rows; row++)
  {
    const int actual_row = layout.grid_first_row + row;
    for (int col = 0; col < layout.grid_cols; col++)
    {
      const int idx = col * layout.rows_per_col + actual_row;
      if (idx < 0 || idx >= layout.result_count)
      {
        break;
      }
      const int cell_x = layout.grid_x + col * layout.grid_col_w;
      const int cell_y = layout.grid_y + row;
      const bool is_selected = idx == sel;
      const int fg = is_selected ? theme.fg_selection : theme.fg_command;
      const int bg = is_selected ? theme.bg_selection : theme.bg_command;
      ui->fill_rect({cell_x, cell_y, layout.grid_col_w, 1}, " ", fg, bg);

      const CommandPaletteSuggestion &suggestion = command_palette_results[(size_t)idx];
      const std::string label =
          ui_truncate_cells(suggestion.label, std::max(0, layout.grid_col_w - 2));
      // Truncate first, then drop match offsets that fell off the tail so
      // highlighting never points outside the drawn text.
      std::vector<int> match;
      for (int m : suggestion.match)
      {
        if (m >= 0 && m < (int)label.size())
        {
          match.push_back(m);
        }
      }
      const int match_fg = is_selected ? theme.fg_selection : theme.fg_keyword;
      ui_draw_marked_text(*ui, cell_x + 1, cell_y, label, match, fg, bg, match_fg);
    }
  }
}

void Editor::place_command_palette_cursor()
{
  if (!show_command_palette)
  {
    return;
  }
  const CommandPaletteLayout layout = build_command_palette_layout();
  // Mirrors render_command_palette: the prompt row is the last row of the
  // region, and the query is drawn with a leading ":" one cell in (the Lua
  // float is borderless and draws its body from the panel's first column).
  std::string query = command_palette_query;
  if (query.empty() || query[0] != ':')
  {
    query = ":" + query;
  }
  const std::string drawn = ui_truncate_cells(query, std::max(0, layout.w - 3));
  const bool lua_renders = lua_api && lua_api->has_lua_ui_handler("command_palette");
  const int text_x = layout.x + (lua_renders ? 0 : 1);
  ui->set_cursor(text_x + ui_cell_count(drawn), layout.prompt_y);
}
