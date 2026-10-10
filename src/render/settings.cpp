// Settings menu rendering: a quick-pick style panel over a search bar, listing
// the config keys that match the query. A row is drawn the way its type reads:
// a toggle glyph for booleans, steppers around a number, chevrons around an
// enum's choice, the raw value for strings.
//
// The layout pass below runs for both painters and records the rows, value
// columns and affordances on the entries, so the mouse hit test reads exactly
// what was drawn and the Lua surface paints into those same rects.
#include "core/editor.h"
#include "lua/api.h"
#include "render/ui_internal.h"
#include "ui/components.h"
#include "ui/text.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
// The text a row's value column shows. Booleans carry their state in a toggle
// glyph (nf-fa-toggle_on / _off) rather than colour, which the selection bar
// would swallow; every other type shows the raw stored value.
std::string value_display(const SettingsEntry &e)
{
  if (e.type == SettingsEntry::Type::Bool)
    return (e.value == "true" ? "\uF205 on" : "\uF204 off");
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

// The painted row the selected entry sits on: sections put headers between the
// rows, so a position in the selectable list is not a position on screen and
// every window decision has to be made in painted rows.
int display_row_of(const std::vector<SettingsRow> &rows, int entry_pos, int fallback)
{
  for (int i = 0; i < (int)rows.size(); i++)
  {
    if (rows[(size_t)i].entry_pos == entry_pos)
      return i;
  }
  return fallback;
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

  const int match_count = (int)settings_filtered.size();
  // The painted rows: the matches plus a header wherever a section starts.
  const int row_count = (int)settings_rows.size();

  // The choices drop-down needs room under the row it belongs to, so the panel
  // grows for it; a row with no room below opens its box upwards.
  SettingsEntry *dropdown_row = nullptr;
  if (settings_dropdown_open)
  {
    SettingsEntry *selected = settings_selected_entry();
    if (selected && selected->type == SettingsEntry::Type::Enum && !selected->options.empty())
      dropdown_row = selected;
  }
  const int dropdown_wanted =
      dropdown_row ? std::min((int)dropdown_row->options.size(), 10) : 0;

  // The panel fits the screen it is on: on a narrow terminal the margin shrinks
  // to nothing and the columns take what is left, rather than a 60-cell floor
  // pushing the right border and every value past the edge.
  const int margin = screen_w >= 80 ? 12 : 2;
  const int w = std::min(std::max(std::min(20, screen_w), screen_w - margin), 96);
  const int max_h = std::clamp(screen_h - 3, 3, 20);
  int h = std::min(std::max(row_count + 6, 10), max_h);
  if (dropdown_row)
  {
    const int row_offset =
        std::max(0, display_row_of(settings_rows, settings_selected, 0) - settings_scroll);
    h = std::min(max_h, std::max(h, row_offset + dropdown_wanted + 5));
  }
  const int x = std::max(0, (screen_w - w) / 2);
  const int y = std::max(1, (screen_h - h) / 3);

  settings_panel_x = x;
  settings_panel_y = y;
  settings_panel_w = w;
  settings_panel_h = h;

  settings_selected = std::clamp(settings_selected, 0, std::max(0, match_count - 1));
  // Row 0 is the title, row 1 the search bar, row 2 the divider under it and
  // the last row the footer, so the list gets what is left.
  const int search_y = y + 1;
  const int list_y = y + 3;
  const int list_h = std::max(1, h - 5);
  // The window runs over painted rows, headers included: a window measured in
  // entries could not fit the group names.
  const int selected_display = display_row_of(settings_rows, settings_selected, 0);
  const int max_scroll = std::max(0, row_count - list_h);
  settings_scroll = std::clamp(settings_scroll, 0, max_scroll);
  // Keep the selection visible: the input handler moves settings_selected and
  // the window follows on the next frame (up before down, so the bottom row
  // stays fully visible).
  if (selected_display < settings_scroll)
    settings_scroll = selected_display;
  if (selected_display >= settings_scroll + list_h)
    settings_scroll = selected_display - list_h + 1;

  const int key_w = std::max(10, (int)(w * 0.55));
  const int val_x = x + 1 + key_w;

  // Invalidate hit rects first; only visible rows get fresh rects below, so
  // mouse hit-testing can never match a scrolled-away row's stale rect.
  for (SettingsEntry &e : settings_entries)
  {
    e.row_y = -1;
    e.step_down_x = -1;
    e.step_up_x = -1;
  }

  for (int row = 0; row < list_h; row++)
  {
    const int display = settings_scroll + row;
    if (display < 0 || display >= row_count)
      break;
    const SettingsRow &r = settings_rows[(size_t)display];
    if (r.entry_pos < 0)
      continue; // a header owns no cells the mouse can act on
    SettingsEntry &e = settings_entries[(size_t)settings_filtered[(size_t)r.entry_pos]];
    const bool selected = r.entry_pos == settings_selected;
    e.row_x = x + 1;
    e.row_y = list_y + row;
    e.row_w = w - 2;
    e.value_x = val_x;
    e.value_w = std::max(1, w - key_w - 4);
    // The affordances belong to the row the cursor is on, so the list stays
    // quiet until a row is picked and the steppers travel with their number.
    if (!selected)
      continue;
    if (e.type == SettingsEntry::Type::Int)
    {
      // The steppers take the band's last four cells and the pair lands one cell
      // past the number; both painters truncate to `value_w`, which keeps the
      // recorded columns exactly where the glyphs end up.
      e.value_w = std::max(1, w - key_w - 8);
      const int text_end =
          e.value_x + ui_cell_count(truncate(value_display(e), e.value_w));
      e.step_down_x = text_end + 1;
      e.step_up_x = e.step_down_x + 2;
    }
    else if (e.type == SettingsEntry::Type::Enum)
    {
      // Room for the leading chevron, so the choice itself does not shift
      // sideways when the row gains focus.
      e.value_x = val_x + 2;
      e.value_w = std::max(1, w - key_w - 6);
      const int text_end =
          e.value_x + ui_cell_count(truncate(value_display(e), e.value_w));
      e.step_down_x = val_x;
      e.step_up_x = std::min(x + w - 2, text_end + 1);
    }
  }
  // The row that owned the drop-down has to still be on screen: a query that
  // filtered it away, or a scroll that carried it off, takes the box with it.
  if (settings_dropdown_open && (!dropdown_row || dropdown_row->row_y < 0))
    settings_dropdown_open = false;

  // --- The choices drop-down's box, placed before either painter draws so
  // the mouse reads the same rect the picture fills.
  if (settings_dropdown_open && dropdown_row)
  {
    const int count = (int)dropdown_row->options.size();
    int box_w = 14;
    for (const std::string &option : dropdown_row->options)
      box_w = std::max(box_w, ui_cell_count(option) + 4);
    box_w = std::min(box_w, std::max(10, w - 4));
    // Room above the row and below it: the box opens on whichever side holds
    // more and its list scrolls when even that is short. It never covers the row
    // itself, since the value being chosen has to stay readable.
    const int below = std::max(0, (y + h - 2) - (dropdown_row->row_y + 1) + 1);
    const int above = std::max(0, dropdown_row->row_y - (y + 1));
    const int rows = std::max(1, std::min(dropdown_wanted, std::max(below, above)));
    const int box_h = rows + 2;
    int by = below >= above ? dropdown_row->row_y + 1 : dropdown_row->row_y - box_h;
    by = std::clamp(by, y + 1, std::max(y + 1, y + h - 1 - box_h));
    settings_dropdown_x =
        std::clamp(dropdown_row->value_x - 1, x + 1, std::max(x + 1, x + w - 1 - box_w));
    settings_dropdown_y = by;
    settings_dropdown_w = box_w;
    settings_dropdown_h = box_h;
    settings_dropdown_index = std::clamp(settings_dropdown_index, 0, std::max(0, count - 1));
    settings_dropdown_scroll =
        std::clamp(settings_dropdown_scroll, 0, std::max(0, count - rows));
    if (settings_dropdown_index < settings_dropdown_scroll)
      settings_dropdown_scroll = settings_dropdown_index;
    if (settings_dropdown_index >= settings_dropdown_scroll + rows)
      settings_dropdown_scroll = settings_dropdown_index - rows + 1;
  }

  // A registered Lua UI handler takes over rendering (the panel then paints as a
  // float over the modal scrim); the layout and hit rects above are shared by
  // both paths.
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
    view.match_count = match_count;
    view.query = settings_query;
    view.search_x = x + 2;
    view.search_y = search_y;
    view.items.reserve((size_t)list_h);
    for (int row = 0; row < list_h; row++)
    {
      const int display = settings_scroll + row;
      if (display < 0 || display >= row_count)
        break;
      const SettingsRow &r = settings_rows[(size_t)display];
      SettingsItemView item;
      if (r.entry_pos < 0)
      {
        // A section header is an item like any other row, so the surface
        // paints exactly the window it was handed.
        item.header = true;
        item.label = r.header;
        view.items.push_back(std::move(item));
        continue;
      }
      const SettingsEntry &e = settings_entries[(size_t)settings_filtered[(size_t)r.entry_pos]];
      item.label = e.label;
      item.value = value_display(e);
      item.type = e.type == SettingsEntry::Type::Bool
                      ? "bool"
                      : (e.type == SettingsEntry::Type::Int
                             ? "int"
                             : (e.type == SettingsEntry::Type::Enum ? "enum" : "string"));
      item.selected = r.entry_pos == settings_selected;
      item.editing = e.editing;
      item.edit_input = e.edit_input;
      item.options = e.options;
      item.option_index = -1;
      for (int i = 0; i < (int)e.options.size(); i++)
      {
        if (e.options[(size_t)i] == e.value)
        {
          item.option_index = i;
          break;
        }
      }
      item.value_x = e.value_x;
      item.value_w = e.value_w;
      item.step_down_x = e.step_down_x;
      item.step_up_x = e.step_up_x;
      item.row_y = e.row_y;
      view.items.push_back(std::move(item));
    }
    if (dropdown_row)
    {
      view.dropdown_open = true;
      view.dropdown_x = settings_dropdown_x;
      view.dropdown_y = settings_dropdown_y;
      view.dropdown_w = settings_dropdown_w;
      view.dropdown_h = settings_dropdown_h;
      view.dropdown_index = settings_dropdown_index;
      view.dropdown_scroll = settings_dropdown_scroll;
      view.dropdown_options = dropdown_row->options;
      view.dropdown_value = dropdown_row->value;
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
                match_count == (int)settings_entries.size()
                    ? std::to_string(settings_entries.size()) + " keys"
                    : std::to_string(match_count) + "/"
                          + std::to_string(settings_entries.size()),
                theme.fg_comment,
                panel_theme.bg_command);

  // --- Search bar: a prompt mark, the query (or what to type), and the match
  // count against the right edge once a query is actually narrowing the list.
  ui->fill_rect({x + 1, search_y, std::max(1, w - 2), 1},
                " ",
                theme.fg_command,
                panel_theme.bg_command);
  ui->draw_text(x + 2, search_y, ">", theme.fg_comment, panel_theme.bg_command);
  if (settings_query.empty())
  {
    ui->draw_text(x + 4,
                  search_y,
                  truncate("Search " + std::to_string(settings_entries.size()) + " settings",
                           w - 8),
                  theme.fg_comment,
                  panel_theme.bg_command);
  }
  else
  {
    const std::string count = std::to_string(match_count) + "/"
                              + std::to_string(settings_entries.size());
    const int count_x = x + w - 3 - (int)ui_cell_count(count);
    ui->draw_text(x + 4,
                  search_y,
                  truncate(settings_query, std::max(1, count_x - x - 6)),
                  theme.fg_keyword,
                  panel_theme.bg_command);
    ui->draw_text(count_x, search_y, count, theme.fg_comment, panel_theme.bg_command);
  }

  if (match_count == 0)
    ui->draw_text(x + 3, list_y, "No settings match", theme.fg_comment, panel_theme.bg_command);

  // Divider between the search bar and the list.
  ui->fill_rect(
      {x + 1, y + 2, std::max(1, w - 2), 1}, "─", theme.fg_panel_border, panel_theme.bg_command);

  for (int row = 0; row < list_h; row++)
  {
    const int display = settings_scroll + row;
    if (display < 0 || display >= row_count)
      break;
    const SettingsRow &r = settings_rows[(size_t)display];
    if (r.entry_pos < 0)
    {
      // A section header: no chevron, no value, no row emphasis, just the
      // group's name in the panel's quiet ink on the label column.
      ui->fill_rect(
          {x + 1, list_y + row, std::max(1, w - 2), 1}, " ", theme.fg_comment, panel_theme.bg_command);
      ui->draw_text(x + 2,
                    list_y + row,
                    truncate(r.header, std::max(1, w - 4)),
                    theme.fg_comment,
                    panel_theme.bg_command);
      continue;
    }
    SettingsEntry &e = settings_entries[(size_t)settings_filtered[(size_t)r.entry_pos]];

    const bool selected = r.entry_pos == settings_selected;
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
      ui->draw_text(e.value_x, e.row_y, truncate(input, e.value_w), theme.fg_selection, bg);
      continue;
    }

    const std::string val = value_display(e);
    // An "off" toggle reads as inert, an "on" one as live; on the selection
    // bar both take the selection's ink so they stay legible.
    const int val_fg = selected ? theme.fg_selection
                                : (e.type == SettingsEntry::Type::Bool && e.value != "true"
                                       ? theme.fg_comment
                                       : theme.fg_keyword);
    ui->draw_text(e.value_x, e.row_y, truncate(val, e.value_w), val_fg, bg);
    // The cycling chevrons / steppers, in the row's own ink so they read on
    // the selection bar (where the accent colour can vanish into the fill).
    const int grip_fg = selected ? theme.fg_selection : theme.fg_comment;
    if (e.type == SettingsEntry::Type::Enum && e.step_down_x >= 0)
    {
      ui->draw_text(e.step_down_x, e.row_y, "\u2039", grip_fg, bg);
      ui->draw_text(e.step_up_x, e.row_y, "\u203A", grip_fg, bg);
    }
    else if (e.type == SettingsEntry::Type::Int && e.step_down_x >= 0)
    {
      ui->draw_text(e.step_down_x, e.row_y, "\uF068", grip_fg, bg);
      ui->draw_text(e.step_up_x, e.row_y, "\uF067", grip_fg, bg);
    }
  }

  // --- The choices drop-down, over the list it belongs to.
  if (dropdown_row)
  {
    UIRect box = {settings_dropdown_x,
                  settings_dropdown_y,
                  settings_dropdown_w,
                  settings_dropdown_h};
    ui_draw_panel(
        *ui, box,
        {theme.fg_command, panel_theme.bg_command, theme.fg_active_border, panel_theme.bg_command});
    const int rows = settings_dropdown_h - 2;
    for (int i = 0; i < rows; i++)
    {
      const int option = settings_dropdown_scroll + i;
      if (option < 0 || option >= (int)dropdown_row->options.size())
        break;
      const bool cursor = option == settings_dropdown_index;
      const bool current = dropdown_row->options[(size_t)option] == dropdown_row->value;
      const int row_y = settings_dropdown_y + 1 + i;
      const int row_fg = cursor ? theme.fg_selection : theme.fg_command;
      const int row_bg = cursor ? theme.bg_selection : panel_theme.bg_command;
      ui->fill_rect({settings_dropdown_x + 1, row_y, settings_dropdown_w - 2, 1}, " ", row_fg, row_bg);
      if (current)
        // nf-fa-check on the value in force, so the list says both what the
        // choices are and which one the config currently holds.
        ui->draw_text(settings_dropdown_x + 1, row_y, "\uF00C", cursor ? theme.fg_selection
                                                                      : theme.fg_keyword,
                      row_bg);
      ui->draw_text(settings_dropdown_x + 3,
                    row_y,
                    truncate(dropdown_row->options[(size_t)option], settings_dropdown_w - 4),
                    row_fg,
                    row_bg);
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
  if (!show_settings_menu)
    return;
  SettingsEntry *e = settings_selected_entry();
  // The text cursor belongs to whichever text field owns the keyboard: the row
  // being edited, else the search bar. An open drop-down hides it entirely.
  if (settings_dropdown_open)
  {
    ui->hide_cursor();
    return;
  }
  if (e && e->editing)
  {
    // "> " prompt, then the text cursor at the end of the input.
    const int cx = e->value_x + 2 + (int)ui_cell_count(e->edit_input);
    ui->set_cursor(cx, e->row_y);
    return;
  }
  ui->set_cursor(settings_panel_x + 4 + (int)ui_cell_count(settings_query),
                 settings_panel_y + 1);
}
