#include "ui_components.h"

#include "ui_text.h"

#include <algorithm>

void ui_draw_panel(UI &ui, const UIRect &rect, const UIPanelStyle &style) {
  ui.fill_rect(rect, " ", style.fg, style.bg);
  ui.draw_border(rect, style.border_fg, style.border_bg);
}

void ui_draw_panel_title(UI &ui, const UIRect &rect, const std::string &title,
                         int fg, int bg) {
  if (rect.w <= 2)
    return;
  ui.draw_text(rect.x + 1, rect.y, ui_truncate_cells(title, rect.w - 2), fg, bg,
               true);
}

void ui_draw_footer(UI &ui, const UIRect &rect, const std::string &text,
                    int fg, int bg) {
  if (rect.w <= 2 || rect.h <= 0)
    return;
  ui.draw_text(rect.x + 1, rect.y + rect.h - 1,
               ui_truncate_cells(text, rect.w - 2), fg, bg);
}

void ui_draw_selectable_rows(UI &ui, int x, int y, int w, int max_rows,
                             const std::vector<UISelectableRow> &rows,
                             const UIListStyle &style) {
  if (w <= 0 || max_rows <= 0)
    return;

  int count = std::min(max_rows, (int)rows.size());
  for (int i = 0; i < count; i++) {
    const auto &row = rows[(size_t)i];
    int fg = row.enabled ? style.fg : style.disabled_fg;
    int bg = row.enabled ? style.bg : style.disabled_bg;
    if (row.selected && row.enabled) {
      fg = style.selected_fg;
      bg = style.selected_bg;
    }

    UIRect row_rect = {x, y + i, w, 1};
    ui.fill_rect(row_rect, " ", fg, bg);
    ui.draw_text(x + 1, y + i, ui_truncate_cells(row.label, w - 1), fg, bg,
                 row.selected && row.enabled);
  }
}
