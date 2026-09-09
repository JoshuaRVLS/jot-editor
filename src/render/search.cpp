// Search panel rendering (project-wide search) and cursor placement.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_search_panel()
{
  if (!show_search)
    return;

  int w = std::min(72, std::max(42, ui->get_render_width() / 2));
  int h = search_replace_visible ? 5 : 4;
  int x = ui->get_width() - w - 2;
  int y = topbar_height() + tab_height;

  if (x < 0)
    x = 0;
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);

  // A registered Lua UI handler paints the search panel from this state; the
  // native rect and row geometry stay the source of truth so the input caret
  // (placed natively) lands on the Lua-drawn fields.
  if (lua_api && lua_api->has_lua_ui_handler("search_panel"))
  {
    SearchView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.query = search_query;
    view.replace_text = search_replace_text;
    view.replace_visible = search_replace_visible;
    view.focus_replace = search_replace_visible && search_focus_replace;
    view.case_sensitive = search_case_sensitive;
    view.whole_word = search_whole_word;
    view.regex = search_regex;
    view.scoped_to_selection = search_scoped_to_selection;
    if (search_result_index >= 0 && !search_results.empty())
    {
      view.count =
          std::to_string(search_result_index + 1) + "/" + std::to_string(search_results.size());
    }
    else
    {
      view.count = "0/0";
    }
    if (lua_api->emit_search(view))
    {
      return;
    }
  }

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui, rect, {theme.fg_command, theme.bg_command, theme.fg_panel_border, theme.bg_command});

  std::string count = "0/0";
  if (search_result_index >= 0 && !search_results.empty())
  {
    count = std::to_string(search_result_index + 1) + "/" + std::to_string(search_results.size());
  }
  else if (!search_query.empty())
  {
    count = "0/0";
  }

  std::string chips;
  chips += search_case_sensitive ? " Aa " : " aa ";
  chips += search_whole_word ? " W " : " w ";
  if (search_regex)
  {
    chips += " .* ";
  }
  if (search_scoped_to_selection)
  {
    chips += " Sel ";
  }
  chips += " " + count + " ";
  ui_draw_panel_title(*ui,
                      rect,
                      search_scoped_to_selection ? " Find in Selection" : " Find",
                      theme.fg_command,
                      theme.bg_command);
  ui->draw_text(
      std::max(x + 1, x + w - (int)chips.size() - 1), y, chips, theme.fg_comment, theme.bg_command);

  int label_w = 9;
  int input_w = std::max(1, w - label_w - 3);
  int find_fg = search_focus_replace ? theme.fg_command : theme.fg_selection;
  int find_bg = search_focus_replace ? theme.bg_command : theme.bg_selection;
  ui->draw_text(x + 1, y + 1, "Find", theme.fg_comment, theme.bg_command);
  ui->draw_text(x + label_w,
                y + 1,
                ui_truncate_cells(search_query, input_w),
                find_fg,
                find_bg,
                !search_focus_replace);

  if (search_replace_visible)
  {
    int replace_fg = search_focus_replace ? theme.fg_selection : theme.fg_command;
    int replace_bg = search_focus_replace ? theme.bg_selection : theme.bg_command;
    ui->draw_text(x + 1, y + 2, "Replace", theme.fg_comment, theme.bg_command);
    ui->draw_text(x + label_w,
                  y + 2,
                  ui_truncate_cells(search_replace_text, input_w),
                  replace_fg,
                  replace_bg,
                  search_focus_replace);
  }

  std::string footer = search_replace_visible
                           ? (search_scoped_to_selection
                                  ? "Enter next  Up prev  Tab field  ^R one  ^R+Shift all in sel"
                                  : "Enter next  Up prev  Tab field  ^R one  ^R+Shift all")
                           : "Enter next  Up prev  Tab case  ^H replace  ^E regex";
  ui->draw_text(
      x + 1, y + h - 2, ui_truncate_cells(footer, w - 3), theme.fg_comment, theme.bg_command);
}

void Editor::place_search_cursor()
{
  if (!show_search)
    return;
  int w = std::min(72, std::max(42, ui->get_render_width() / 2));
  int x = std::max(0, ui->get_width() - w - 2);
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);
  const int label_w = 9;
  const int input_w = std::max(1, w - label_w - 3);
  const bool replace = search_replace_visible && search_focus_replace;
  const std::string &input = replace ? search_replace_text : search_query;
  const int cursor_x = x + label_w + std::min(input_w - 1, std::max(0, ui_cell_count(input)));
  const int cursor_y = topbar_height() + tab_height + (replace ? 2 : 1);
  ui->set_cursor(cursor_x, cursor_y);
}

