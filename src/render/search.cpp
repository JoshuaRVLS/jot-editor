// Search panel rendering and cursor placement. The panel's state and the rest
// of its behaviour live in SearchController (core/search_controller.h).
#include "core/search_controller.h"

#include "core/editor.h"
#include "lua/api/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // The label column: "Replace" padded to this many cells, and one border cell
  // before it. The input starts under the label, so the caret and the field
  // share the column the Lua kit pads to.
  constexpr int kLabelCells = 8;
  constexpr int kLabelCol = 9;
} // namespace

// The compact find bar: one row for the query with the toggle buttons and the
// match count right-aligned on it, one more for the replacement when it is up,
// and no key-hint footer. Everything the painter, the caret and the mouse need
// is derived here so the three agree on a cell.
SearchController::PanelGeometry SearchController::panel_geometry() const
{
  PanelGeometry geo;

  std::string count;
  if (!query_.empty() && results_.empty())
  {
    count = "No results";
  }
  else if (result_index_ >= 0 && !results_.empty())
  {
    count = std::to_string(result_index_ + 1) + "/" + std::to_string(results_.size());
  }
  else
  {
    count = "0/0";
  }
  geo.count = count;

  // The toggle buttons, the scope badge, then the count. Active toggles take the
  // accent, so the row reads as three buttons and not a legend.
  const bool flags[] = {case_sensitive_, whole_word_, regex_};
  const std::string labels[] = {"Aa", "W", ".*"};
  for (int i = 0; i < 3; i++)
  {
    if (!geo.chips.empty())
      geo.chips += ' ';
    PanelGeometry::Chip chip;
    chip.start = (int)geo.chips.size();
    chip.width = (int)labels[i].size();
    chip.flag = i;
    chip.on = flags[i];
    geo.chips += labels[i];
    geo.chip_hits.push_back(chip);
  }
  if (scoped_to_selection_)
  {
    geo.chips += ' ';
    PanelGeometry::Chip chip;
    chip.start = (int)geo.chips.size();
    chip.width = 3;
    chip.flag = -1;
    chip.on = true;
    geo.chips += "Sel";
    geo.chip_hits.push_back(chip);
  }
  geo.chips += "  " + count;

  // The bar is at least wide enough for the label, one input cell and the
  // cluster, so a narrow terminal shrinks the query field rather than sliding
  // the toggles over it.
  const int cluster_w = ui_cell_count(geo.chips);
  const int render_w = editor_.ui->get_render_width();
  geo.w = std::min(72, std::max(42, render_w / 2));
  geo.w = std::max(geo.w, cluster_w + kLabelCells + 3);
  geo.w = std::min(geo.w, std::max(20, render_w));
  geo.x = editor_.ui->get_width() - geo.w - 2;
  if (geo.x < 0)
    geo.x = 0;
  if (geo.x + geo.w > editor_.ui->get_width())
    geo.w = std::max(20, editor_.ui->get_width() - geo.x);
  geo.y = editor_.pane_area_top();
  geo.h = replace_visible_ ? 4 : 3;

  const int inner_w = geo.w - 2;
  geo.input_x = geo.x + kLabelCol;
  geo.input_w = std::max(1, inner_w - kLabelCells - cluster_w - 1);
  geo.cluster_x = std::max(geo.x + kLabelCol + 1, geo.x + 1 + (inner_w - cluster_w));
  return geo;
}

void SearchController::render_panel()
{
  if (!visible_)
    return;

  const PanelGeometry geo = panel_geometry();

  // A registered Lua UI handler paints the search panel from this state; the
  // native rect and row geometry stay the source of truth so the input caret
  // (placed natively) lands on the Lua-drawn fields.
  if (editor_.lua_api && editor_.lua_api->has_lua_ui_handler("search_panel"))
  {
    SearchView view;
    view.x = geo.x;
    view.y = geo.y;
    view.w = geo.w;
    view.h = geo.h;
    view.query = query_;
    view.replace_text = replace_text_;
    view.replace_visible = replace_visible_;
    view.focus_replace = replace_visible_ && focus_replace_;
    view.case_sensitive = case_sensitive_;
    view.whole_word = whole_word_;
    view.regex = regex_;
    view.scoped_to_selection = scoped_to_selection_;
    view.count = geo.count;
    if (editor_.lua_api->emit_search(view))
    {
      return;
    }
  }

  UIRect rect = {geo.x, geo.y, geo.w, geo.h};
  ui_draw_panel(*editor_.ui,
                rect,
                {editor_.theme.fg_command,
                 editor_.theme.bg_command,
                 editor_.theme.fg_panel_border,
                 editor_.theme.bg_command});
  ui_draw_panel_title(*editor_.ui,
                      rect,
                      scoped_to_selection_ ? " Find in Selection " : " Find ",
                      editor_.theme.fg_command,
                      editor_.theme.bg_command);

  const bool replace_focus = replace_visible_ && focus_replace_;
  auto draw_field = [&](int row_y, const std::string &label, const std::string &text, bool focused)
  {
    editor_.ui->draw_text(geo.x + 1, row_y, label, editor_.theme.fg_comment, editor_.theme.bg_command);
    const int field_fg = focused ? editor_.theme.fg_selection : editor_.theme.fg_command;
    const int field_bg = focused ? editor_.theme.bg_selection : editor_.theme.bg_command;
    // Fill the field the whole width the hit-test and the caret use, so the
    // focused field reads as an input box rather than a run of text.
    editor_.ui->draw_text(geo.input_x, row_y, std::string((size_t)geo.input_w, ' '), field_fg, field_bg);
    editor_.ui->draw_text(geo.input_x,
                          row_y,
                          ui_truncate_cells(text, geo.input_w),
                          field_fg,
                          field_bg,
                          focused);
  };

  draw_field(geo.y + 1, "Find", query_, !replace_focus);
  if (replace_visible_)
  {
    draw_field(geo.y + 2, "Replace", replace_text_, replace_focus);
  }

  // The cluster is right-aligned on the find row: comment for the count and the
  // inactive buttons, accent for the live ones.
  editor_.ui->draw_text(geo.cluster_x, geo.y + 1, geo.chips, editor_.theme.fg_comment, editor_.theme.bg_command);
  for (const PanelGeometry::Chip &chip : geo.chip_hits)
  {
    if (!chip.on)
      continue;
    editor_.ui->draw_text(geo.cluster_x + chip.start,
                          geo.y + 1,
                          geo.chips.substr((size_t)chip.start, (size_t)chip.width),
                          editor_.theme.fg_keyword,
                          editor_.theme.bg_command);
  }
}

void SearchController::place_cursor()
{
  if (!visible_)
    return;
  const PanelGeometry geo = panel_geometry();
  const bool replace = replace_visible_ && focus_replace_;
  const std::string &input = replace ? replace_text_ : query_;
  const int cursor_x = geo.input_x + std::min(geo.input_w - 1, std::max(0, ui_cell_count(input)));
  const int cursor_y = geo.y + (replace ? 2 : 1);
  editor_.ui->set_cursor(cursor_x, cursor_y);
}
