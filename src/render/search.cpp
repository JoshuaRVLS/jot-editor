// Search panel rendering and cursor placement. The panel's state and the rest
// of its behaviour live in SearchController (jot/editor/search_controller.h).
#include "jot/editor/search_controller.h"

#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void SearchController::render_panel()
{
  if (!visible_)
    return;

  int w = std::min(72, std::max(42, editor_.ui->get_render_width() / 2));
  int h = replace_visible_ ? 5 : 4;
  int x = editor_.ui->get_width() - w - 2;
  int y = editor_.topbar_height() + editor_.tab_height;

  if (x < 0)
    x = 0;
  if (x + w > editor_.ui->get_width())
    w = std::max(20, editor_.ui->get_width() - x);

  // A registered Lua UI handler paints the search panel from this state; the
  // native rect and row geometry stay the source of truth so the input caret
  // (placed natively) lands on the Lua-drawn fields.
  if (editor_.lua_api && editor_.lua_api->has_lua_ui_handler("search_panel"))
  {
    SearchView view;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.query = query_;
    view.replace_text = replace_text_;
    view.replace_visible = replace_visible_;
    view.focus_replace = replace_visible_ && focus_replace_;
    view.case_sensitive = case_sensitive_;
    view.whole_word = whole_word_;
    view.regex = regex_;
    view.scoped_to_selection = scoped_to_selection_;
    if (result_index_ >= 0 && !results_.empty())
    {
      view.count =
          std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size());
    }
    else
    {
      view.count = "0/0";
    }
    if (editor_.lua_api->emit_search(view))
    {
      return;
    }
  }

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *editor_.ui, rect, {editor_.theme.fg_command, editor_.theme.bg_command, editor_.theme.fg_panel_border, editor_.theme.bg_command});

  std::string count = "0/0";
  if (result_index_ >= 0 && !results_.empty())
  {
    count = std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size());
  }
  else if (!query_.empty())
  {
    count = "0/0";
  }

  std::string chips;
  chips += case_sensitive_ ? " Aa " : " aa ";
  chips += whole_word_ ? " W " : " w ";
  if (regex_)
  {
    chips += " .* ";
  }
  if (scoped_to_selection_)
  {
    chips += " Sel ";
  }
  chips += " " + count + " ";
  ui_draw_panel_title(*editor_.ui,
                      rect,
                      scoped_to_selection_ ? " Find in Selection" : " Find",
                      editor_.theme.fg_command,
                      editor_.theme.bg_command);
  editor_.ui->draw_text(
      std::max(x + 1, x + w - (int)chips.size() - 1), y, chips, editor_.theme.fg_comment, editor_.theme.bg_command);

  int label_w = 9;
  int input_w = std::max(1, w - label_w - 3);
  int find_fg = focus_replace_ ? editor_.theme.fg_command : editor_.theme.fg_selection;
  int find_bg = focus_replace_ ? editor_.theme.bg_command : editor_.theme.bg_selection;
  editor_.ui->draw_text(x + 1, y + 1, "Find", editor_.theme.fg_comment, editor_.theme.bg_command);
  editor_.ui->draw_text(x + label_w,
                y + 1,
                ui_truncate_cells(query_, input_w),
                find_fg,
                find_bg,
                !focus_replace_);

  if (replace_visible_)
  {
    int replace_fg = focus_replace_ ? editor_.theme.fg_selection : editor_.theme.fg_command;
    int replace_bg = focus_replace_ ? editor_.theme.bg_selection : editor_.theme.bg_command;
    editor_.ui->draw_text(x + 1, y + 2, "Replace", editor_.theme.fg_comment, editor_.theme.bg_command);
    editor_.ui->draw_text(x + label_w,
                  y + 2,
                  ui_truncate_cells(replace_text_, input_w),
                  replace_fg,
                  replace_bg,
                  focus_replace_);
  }

  std::string footer = replace_visible_
                           ? (scoped_to_selection_
                                  ? "Enter next  Up prev  Tab field  ^R one  ^R+Shift all in sel"
                                  : "Enter next  Up prev  Tab field  ^R one  ^R+Shift all")
                           : "Enter next  Up prev  Tab case  ^H replace  ^E regex";
  editor_.ui->draw_text(
      x + 1, y + h - 2, ui_truncate_cells(footer, w - 3), editor_.theme.fg_comment, editor_.theme.bg_command);
}

void SearchController::place_cursor()
{
  if (!visible_)
    return;
  int w = std::min(72, std::max(42, editor_.ui->get_render_width() / 2));
  int x = std::max(0, editor_.ui->get_width() - w - 2);
  if (x + w > editor_.ui->get_width())
    w = std::max(20, editor_.ui->get_width() - x);
  const int label_w = 9;
  const int input_w = std::max(1, w - label_w - 3);
  const bool replace = replace_visible_ && focus_replace_;
  const std::string &input = replace ? replace_text_ : query_;
  const int cursor_x = x + label_w + std::min(input_w - 1, std::max(0, ui_cell_count(input)));
  const int cursor_y = editor_.topbar_height() + editor_.tab_height + (replace ? 2 : 1);
  editor_.ui->set_cursor(cursor_x, cursor_y);
}

