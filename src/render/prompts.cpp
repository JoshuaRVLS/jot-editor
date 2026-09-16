// Save and quit confirmation prompts.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // Geometry for the single-field prompts (save, rename): one row of input
  // inside a box wide enough to type a name in. Shared because the renderer and
  // the caret placement have to land on exactly the same cell.
  struct FieldPromptBox
  {
    int x;
    int y;
    int w;
    int h;
  };

  FieldPromptBox field_prompt_box(int screen_w, int screen_h, const std::string &title)
  {
    const int box_w = std::min(screen_w, std::max(ui_cell_count(title) + 4, 46));
    const int x = std::max(0, screen_w / 2 - box_w / 2);
    const int y = screen_h / 2;
    return {x, y, box_w, 4};
  }
} // namespace

void Editor::render_save_prompt()
{
  int h = ui->get_height();
  int w = ui->get_render_width();

  // No instruction line: the panel title says what this is, and the field prefix
  // below carries the only thing that is not obvious.
  const std::string prompt = "Save As";
  const FieldPromptBox box = field_prompt_box(w, h, prompt);
  const int x = box.x + 2;
  const int y = box.y + 1;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  const UIRect rect = {box.x, box.y, box.w, box.h};

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
  const FieldPromptBox box = field_prompt_box(w, h, "Save As");
  const int x = box.x + 2;
  const int y = box.y + 1;
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


void Editor::render_rename_prompt()
{
  const int h = ui->get_height();
  const int w = ui->get_render_width();

  const std::string title = "Rename Symbol";
  const FieldPromptBox box = field_prompt_box(w, h, title);
  const int x = box.x + 2;
  const int y = box.y + 1;

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  const UIRect rect = {box.x, box.y, box.w, box.h};

  if (lua_api && lua_api->has_lua_ui_handler("rename_prompt"))
  {
    PromptView view;
    view.input = rename_prompt_input;
    view.x = rect.x;
    view.y = rect.y;
    view.w = rect.w;
    view.h = rect.h;
    if (lua_api->emit_prompt("rename_prompt", view))
    {
      return;
    }
  }

  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});

  // No instruction line: the panel title says what it is, and the field is
  // obvious. (The save prompt keeps the same shape.)
  const std::string disp = "New name: " + rename_prompt_input;
  ui->draw_text(x,
                std::min(h - 1, y + 1),
                ui_truncate_cells(disp, std::max(1, w - x - 1)),
                theme.fg_command,
                panel_theme.bg_command);
}

void Editor::place_rename_prompt_cursor()
{
  if (!show_rename_prompt)
  {
    return;
  }
  const int h = ui->get_height();
  const int w = ui->get_render_width();
  const FieldPromptBox box = field_prompt_box(w, h, "Rename Symbol");
  const int x = box.x + 2;
  const int y = box.y + 1;
  const std::string prefix = "New name: ";
  ui->set_cursor(x + std::min(std::max(0, w - x - 1), ui_cell_count(prefix + rename_prompt_input)),
                 std::min(h - 1, y + 1));
}
