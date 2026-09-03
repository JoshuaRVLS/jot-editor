#include "ui/components.h"

#include "ui/text.h"

#include <algorithm>

void ui_draw_panel(UI &ui, const UIRect &rect, const UIPanelStyle &style)
{
  ui.fill_rect(rect, " ", style.fg, style.bg);
  ui.draw_border(rect, style.border_fg, style.border_bg);
}

void ui_draw_panel_title(UI &ui, const UIRect &rect, const std::string &title, int fg, int bg)
{
  if (rect.w <= 2)
    return;
  ui.draw_text(rect.x + 1, rect.y, ui_truncate_cells(title, rect.w - 2), fg, bg, true);
}

void ui_draw_footer(UI &ui, const UIRect &rect, const std::string &text, int fg, int bg)
{
  if (rect.w <= 2 || rect.h <= 0)
    return;
  ui.draw_text(rect.x + 1, rect.y + rect.h - 1, ui_truncate_cells(text, rect.w - 2), fg, bg);
}

void ui_draw_selectable_rows(UI &ui,
                             int x,
                             int y,
                             int w,
                             int max_rows,
                             const std::vector<UISelectableRow> &rows,
                             const UIListStyle &style)
{
  if (w <= 0 || max_rows <= 0)
    return;

  int count = std::min(max_rows, (int)rows.size());
  for (int i = 0; i < count; i++)
  {
    const auto &row = rows[(size_t)i];
    int fg = row.enabled ? style.fg : style.disabled_fg;
    int bg = row.enabled ? style.bg : style.disabled_bg;
    if (row.selected && row.enabled)
    {
      fg = style.selected_fg;
      bg = style.selected_bg;
    }

    UIRect row_rect = {x, y + i, w, 1};
    ui.fill_rect(row_rect, " ", fg, bg);
    ui.draw_text(
        x + 1, y + i, ui_truncate_cells(row.label, w - 1), fg, bg, row.selected && row.enabled);
  }
}

void ui_draw_button(UI &ui,
                    const UIButton &button,
                    int primary_fg,
                    int primary_bg,
                    int secondary_fg,
                    int secondary_bg,
                    int danger_fg,
                    int danger_bg,
                    int muted_fg,
                    int muted_bg)
{
  if (button.rect.w <= 0 || button.rect.h <= 0)
    return;

  int fg = secondary_fg;
  int bg = secondary_bg;
  switch (button.variant)
  {
  case UIButtonVariant::Primary:
    fg = primary_fg;
    bg = primary_bg;
    break;
  case UIButtonVariant::Danger:
    fg = danger_fg;
    bg = danger_bg;
    break;
  case UIButtonVariant::Muted:
    fg = muted_fg;
    bg = muted_bg;
    break;
  case UIButtonVariant::Secondary:
    break;
  }
  if (!button.enabled)
  {
    fg = muted_fg;
    bg = muted_bg;
  }

  ui.fill_rect(button.rect, " ", fg, bg);
  std::string label = ui_truncate_cells(button.label, button.rect.w);
  int label_w = ui_cell_count(label);
  int x = button.rect.x + std::max(0, (button.rect.w - label_w) / 2);
  for (int row = 0; row < button.rect.h; row++)
  {
    ui.draw_text(x, button.rect.y + row, label, fg, bg, button.focused && button.enabled);
  }
  if (button.focused && button.enabled && button.rect.h >= 3)
  {
    ui.draw_border(button.rect, fg, bg);
  }
}

bool ui_button_hit(const UIButton &button, int x, int y)
{
  return button.enabled && x >= button.rect.x && x < button.rect.x + button.rect.w
         && y >= button.rect.y && y < button.rect.y + button.rect.h;
}
