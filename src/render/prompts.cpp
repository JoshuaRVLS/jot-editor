// Save and quit confirmation prompts.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_save_prompt()
{
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  int x = std::max(0, w / 2 - ui_cell_count(prompt) / 2);
  int y = h / 2;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {std::max(0, x - 2), std::max(0, y - 1), std::min(w, ui_cell_count(prompt) + 4), 4};

  if (lua_api && lua_api->has_lua_ui_handler("save_prompt"))
  {
    PromptView view;
    view.input = save_prompt_input;
    view.x = rect.x;
    view.y = rect.y;
    view.w = rect.w;
    view.h = rect.h;
    if (lua_api->emit_prompt("save_prompt", view))
    {
      return;
    }
  }

  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, panel_theme.bg_command);

  std::string disp = "Filename: " + save_prompt_input;
  ui->draw_text(x,
                std::min(h - 1, y + 1),
                ui_truncate_cells(disp, std::max(1, w - x - 1)),
                theme.fg_command,
                panel_theme.bg_command);
}

void Editor::place_save_prompt_cursor()
{
  if (!show_save_prompt)
    return;
  const int h = ui->get_height();
  const int w = ui->get_render_width();
  const std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  const int x = std::max(0, w / 2 - ui_cell_count(prompt) / 2);
  const int y = h / 2;
  const std::string prefix = "Filename: ";
  ui->set_cursor(x + std::min(std::max(0, w - x - 1), ui_cell_count(prefix + save_prompt_input)),
                 std::min(h - 1, y + 1));
}

void Editor::render_quit_prompt()
{
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Unsaved changes! Quit anyway? (y/n)";
  int x = w / 2 - prompt.length() / 2;
  int y = h / 2;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x - 2, y - 1, (int)prompt.length() + 4, 3};

  if (lua_api && lua_api->has_lua_ui_handler("quit_prompt"))
  {
    PromptView view;
    view.x = rect.x;
    view.y = rect.y;
    view.w = rect.w;
    view.h = rect.h;
    if (lua_api->emit_prompt("quit_prompt", view))
    {
      return;
    }
  }

  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, panel_theme.bg_command);
}

