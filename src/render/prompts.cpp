// Save and quit confirmation prompts.
#include "core/editor.h"
#include "lua/api/api.h"
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

void Editor::render_prompt_modal()
{
  if (!show_save_prompt && !show_rename_prompt && !show_quit_prompt)
  {
    return;
  }
  // The same two steps every other modal here takes (the pickers, the modal
  // popup): dim what is already on the grid, then paint the panel over it. So
  // the buffer the prompt is about stays visible behind the panel instead of
  // being replaced by an empty grid, and the dim covers the whole screen -- the
  // sidebar and the status line included -- rather than looking like a scrim
  // that stopped at the pane edge.
  //
  // When a Lua handler owns the panel, render_*_prompt() only opens its float:
  // the panel lands in the float pass that follows this call, undimmed, because
  // a prompt surface is in the modal set (lua/api/api_float.cpp) and only the modal
  // surface's own float is spared the pass's re-dim.
  ui->dim_rect({0, 0, ui->get_render_width(), ui->get_height()});
  if (show_save_prompt)
  {
    render_save_prompt();
  }
  else if (show_rename_prompt)
  {
    render_rename_prompt();
  }
  else
  {
    render_quit_prompt();
  }
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

// The status-line prompt (the minibuffer). It is painted over the row the
// message line lives on, rather than in a box of its own: what the reader typed
// is the last thing on screen and the caret sits in it, so Enter is the same
// key it was in a shell.
void Editor::render_minibuffer()
{
  if (!show_minibuffer || !ui)
  {
    return;
  }
  const int w = ui->get_render_width();
  const int y = ui->get_height() - 1;
  const std::string text = minibuffer_label + minibuffer_input;
  // A failing search is the one state of the prompt worth a colour: the label
  // and the text stay the command slots every other input uses.
  const int fg = isearch_failed ? theme.fg_status_error : theme.fg_command;
  ui->fill_rect({0, y, w, 1}, " ", fg, theme.bg_command);
  ui->draw_text(0, y, ui_truncate_cells(text, w), fg, theme.bg_command);

  // What the prompt is offering, right-aligned and quieter: Tab's candidate
  // count for a path, the mode for a search.
  std::string hint;
  if (minibuffer_kind == MINIBUFFER_FIND_FILE && !minibuffer_matches.empty())
  {
    hint = std::to_string((int)minibuffer_matches.size()) + " match(es)";
  }
  else if (minibuffer_kind == MINIBUFFER_ISEARCH)
  {
    hint = isearch_regex ? "regexp" : "plain";
  }
  if (!hint.empty())
  {
    const int hint_w = ui_cell_count(hint);
    if (hint_w + 2 < w)
    {
      ui->draw_text(w - hint_w - 1, y, hint, theme.fg_comment, theme.bg_command);
    }
  }
}

void Editor::place_minibuffer_cursor()
{
  if (!show_minibuffer || !ui)
  {
    return;
  }
  const int cursor =
      std::clamp(minibuffer_cursor, 0, (int)minibuffer_input.size());
  const int x = ui_cell_count(minibuffer_label + minibuffer_input.substr(0, (size_t)cursor));
  const int y = ui->get_height() - 1;
  ui->set_cursor(std::min(std::max(0, x), std::max(0, ui->get_render_width() - 1)), y);
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
