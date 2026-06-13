#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include "ui.h"
#include <string>
#include <vector>

struct UIPanelStyle {
  int fg = 7;
  int bg = 0;
  int border_fg = 7;
  int border_bg = 0;
};

struct UIListStyle {
  int fg = 7;
  int bg = 0;
  int selected_fg = 0;
  int selected_bg = 4;
  int disabled_fg = 8;
  int disabled_bg = 0;
};

struct UISelectableRow {
  std::string label;
  bool selected = false;
  bool enabled = true;
};

void ui_draw_panel(UI &ui, const UIRect &rect, const UIPanelStyle &style);
void ui_draw_panel_title(UI &ui, const UIRect &rect, const std::string &title,
                         int fg, int bg);
void ui_draw_footer(UI &ui, const UIRect &rect, const std::string &text,
                    int fg, int bg);
void ui_draw_selectable_rows(UI &ui, int x, int y, int w, int max_rows,
                             const std::vector<UISelectableRow> &rows,
                             const UIListStyle &style);

#endif
