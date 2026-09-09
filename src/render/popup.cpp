// Plain-text popup rendering (LSP hover fallback, git commit help, ...).
#include "editor.h"
#include "jot/lua/api.h"
#include "render/ui_internal.h"

using namespace ui_internal;
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

void Editor::render_popup()
{
  if (!popup.visible)
    return;

  // Modal popups (help, keymap listings, ...) hand off to a Lua UI handler
  // when registered; hover popups keep the native path (their Lua counterpart
  // is the lsp.hover_ui handler).
  if (popup.presentation == POPUP_MODAL && lua_api && lua_api->has_lua_ui_handler("popup"))
  {
    PopupView view;
    view.title = popup.title;
    view.scroll = popup.scroll;
    view.x = popup.x;
    view.y = popup.y;
    view.w = popup.w;
    view.h = popup.h;
    view.lines = popup.lines;
    if (lua_api->emit_popup(view))
    {
      return;
    }
  }

  UIRect rect = {popup.x, popup.y, popup.w, popup.h};
  Theme popup_theme = theme;
  popup_theme.bg_command = theme.bg_panel_border;
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, popup_theme.bg_command, theme.fg_panel_border, popup_theme.bg_command});
  if (!popup.title.empty())
  {
    ui_draw_panel_title(
        *ui, rect, " " + popup.title + " ", theme.fg_command, popup_theme.bg_command);
  }

  bool in_code_block = false;
  std::string code_extension;
  int draw_row = 0;
  int content_row = 0;
  for (int i = 0; i < (int)popup.lines.size(); i++)
  {
    std::string fence_language;
    if (hover_markdown_fence_language(popup.lines[i], &fence_language))
    {
      in_code_block = !in_code_block;
      code_extension = in_code_block ? hover_language_extension(fence_language) : "";
      continue;
    }

    if (content_row++ < popup.scroll)
    {
      continue;
    }
    if (draw_row >= popup.h - 2)
      break;
    if (in_code_block)
    {
      draw_hover_code_line(ui,
                           popup.x + 1,
                           popup.y + 1 + draw_row,
                           popup.w - 2,
                           popup.lines[i],
                           code_extension,
                           popup_theme);
    }
    else
    {
      ui->draw_text(popup.x + 1,
                    popup.y + 1 + draw_row,
                    ui_truncate_cells(popup.lines[i], popup.w - 2),
                    theme.fg_command,
                    popup_theme.bg_command);
    }
    draw_row++;
  }

  if (popup.content_lines > popup.h - 2)
  {
    const int first = popup.scroll + 1;
    const int last = std::min(popup.content_lines, popup.scroll + popup.h - 2);
    const std::string count = std::to_string(first) + "-" + std::to_string(last) + "/"
                              + std::to_string(popup.content_lines);
    ui->draw_text(popup.x + std::max(1, popup.w - ui_cell_count(count) - 1),
                  popup.y + popup.h - 1,
                  count,
                  theme.fg_comment,
                  popup_theme.bg_command);
  }
}

