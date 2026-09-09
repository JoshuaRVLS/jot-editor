// The LSP completion popup overlay.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/integrations/lsp/matching.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace overlay_internal;

  std::string completion_kind_icon(int kind, bool use_nerd_icons)
  {
    if (!use_nerd_icons)
    {
      switch (kind)
      {
      case 2:
        return "[M] "; // Method
      case 3:
        return "[F] "; // Function
      case 4:
        return "[C] "; // Constructor
      case 5:
        return "[Fd] "; // Field
      case 6:
        return "[V] "; // Variable
      case 7:
        return "[Cl] "; // Class
      case 8:
        return "[I] "; // Interface
      case 9:
        return "[Mo] "; // Module
      case 10:
        return "[P] "; // Property
      case 12:
        return "[Val] "; // Value
      case 14:
        return "[K] "; // Keyword
      default:
        return "[?] ";
      }
    }

    switch (kind)
    {
    case 2:
      return "󰊕 "; // Method
    case 3:
      return "󰊕 "; // Function
    case 4:
      return " "; // Constructor
    case 5:
      return "󰇽 "; // Field
    case 6:
      return "󰀫 "; // Variable
    case 7:
      return "󰠱 "; // Class
    case 8:
      return " "; // Interface
    case 9:
      return " "; // Module
    case 10:
      return "󰜢 "; // Property
    case 12:
      return "󰎠 "; // Value
    case 14:
      return "󰌋 "; // Keyword
    default:
      return "󰘍 ";
    }
  }

  std::string completion_kind_name(int kind)
  {
    switch (kind)
    {
    case 2:
      return "method";
    case 3:
      return "function";
    case 4:
      return "ctor";
    case 5:
      return "field";
    case 6:
      return "variable";
    case 7:
      return "class";
    case 8:
      return "interface";
    case 9:
      return "module";
    case 10:
      return "property";
    case 12:
      return "value";
    case 14:
      return "keyword";
    default:
      return "symbol";
    }
  }

void Editor::render_lsp_completion()
{
  if (!lsp_completion_visible || lsp_completion_items.empty() || panes.empty())
  {
    return;
  }

  auto &pane = get_pane();
  auto &buf = get_buffer(pane.buffer_id);
  if (!lsp_completion_filepath.empty() && buf.filepath != lsp_completion_filepath)
  {
    hide_lsp_completion();
    return;
  }
  if (buf.cursor.y != lsp_completion_replace_start.y
      || buf.cursor.x < lsp_completion_replace_start.x)
  {
    hide_lsp_completion();
    return;
  }
  refresh_lsp_completion_filter();
  if (!lsp_completion_visible || lsp_completion_items.empty())
  {
    return;
  }

  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  const int line_num_width = 8;
  const bool use_nerd_icons = config.get_bool("lsp_completion_nerd_icons", true);
  int visible_h = std::max(1, pane.h - tab_height - 1);
  int visible_w = std::max(12, draw_w - 2 - line_num_width);

  // Caret screen row: the popup placement is decided from the space below
  // and above it, so this must be known before sizing the box.
  int cursor_row = 0;
  const int viewport_h = std::max(1, pane.h - tab_height - 1);
  for (int row = 0; row < viewport_h; row++)
  {
    int line = Folding::buffer_line_for_visible_offset(
        buf.fold_ranges, buf.scroll_offset, row, (int)buf.line_count());
    if (line >= 0 && line == buf.cursor.y && !Folding::is_line_hidden(buf.fold_ranges, line))
    {
      cursor_row = row;
      break;
    }
  }
  const int cursor_y = pane.y + tab_height + cursor_row;
  const int space_below = (pane.y + visible_h - 1) - cursor_y;
  const int space_above = cursor_y - (pane.y + tab_height);

  const int max_items_cfg = std::clamp(config.get_int("lsp_completion_max_items", 8), 3, 20);
  int max_items = std::min(max_items_cfg, (int)lsp_completion_items.size());
  const int footer_h = 1;
  // The bordered box needs max_items + footer + 2 rows. When neither the
  // space below nor above the caret fits the full window, shrink the visible
  // rows so the popup never covers the caret -- the line being typed stays
  // readable even at the very bottom of the screen.
  int need_h = max_items + footer_h + 2;
  if (space_below < need_h && space_above < need_h)
  {
    max_items = std::max(3, std::min(max_items, std::max(space_below, space_above) - footer_h - 2));
    need_h = max_items + footer_h + 2;
  }
  int selected = std::clamp(lsp_completion_selected, 0, (int)lsp_completion_items.size() - 1);
  int start_idx = std::max(0, selected - max_items + 1);
  if (selected < start_idx)
  {
    start_idx = selected;
  }
  if (start_idx + max_items > (int)lsp_completion_items.size())
  {
    start_idx = std::max(0, (int)lsp_completion_items.size() - max_items);
  }

  int longest_label = 12;
  int longest_meta = 8;
  for (int i = start_idx; i < start_idx + max_items; i++)
  {
    if (i < 0 || i >= (int)lsp_completion_items.size())
    {
      continue;
    }
    longest_label = std::max(longest_label, (int)lsp_completion_items[i].label.size() + 4);
    std::string meta = completion_kind_name(lsp_completion_items[i].kind);
    if (!lsp_completion_items[i].detail.empty())
    {
      meta += " " + one_line_text(lsp_completion_items[i].detail);
    }
    else if (!lsp_completion_items[i].documentation.empty())
    {
      meta += " " + one_line_text(lsp_completion_items[i].documentation);
    }
    longest_meta = std::max(longest_meta, std::min(32, (int)meta.size()));
  }

  int box_w = std::clamp(longest_label + longest_meta + 8, 28, std::min(visible_w, 82));
  int box_h = max_items + footer_h;

  int safe_cursor_y = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
  const std::string &line = buf.line(safe_cursor_y);
  int cursor_visual = compute_visual_column(line, buf.cursor.x, tab_size);
  int scroll_visual = compute_visual_column(line, buf.scroll_x, tab_size);
  int cursor_x =
      pane.x + 1 + line_num_width + (cursor_visual - scroll_visual)
      + lsp_inlay_hint_cells_before(buf.filepath, buf.cursor.y, buf.cursor.x, line);

  int min_x = pane.x + 1 + line_num_width;
  int max_x = pane.x + draw_w - box_w - 1;
  if (max_x < min_x)
  {
    max_x = min_x;
  }

  int min_y = pane.y + tab_height;
  int max_y = pane.y + visible_h - box_h;
  if (max_y < min_y)
  {
    max_y = min_y;
  }

  auto clamp_box_x = [&](int x) { return std::clamp(x, min_x, max_x); };
  auto clamp_box_y = [&](int y) { return std::clamp(y, min_y, max_y); };

  // Vertical side: below the caret by default (typing flows downward); flip
  // above when the bottom clips. After the shrink above, the box fits the
  // chosen side whenever any side fits at all.
  bool place_below;
  if (space_below >= need_h)
  {
    place_below = true;
  }
  else if (space_above >= need_h)
  {
    place_below = false;
  }
  else
  {
    place_below = space_below >= space_above; // last resort: larger side
  }
  // Horizontal: extend right of the caret when the pane has room, else left.
  const bool place_right = cursor_x + 2 + box_w + 2 <= pane.x + draw_w;

  int box_x = clamp_box_x(place_right ? cursor_x + 2 : cursor_x - box_w - 2);
  int box_y = clamp_box_y(place_below ? cursor_y + 2 : cursor_y - box_h - 2);

  // A registered Lua UI handler paints the completion popup from this state;
  // the box geometry stays native (placement is cursor-avoidance logic the
  // Lua layer shouldn't duplicate) so the popup tracks the caret exactly.
  if (lua_api && lua_api->has_lua_ui_handler("lsp_completion"))
  {
    CompletionView view;
    view.x = box_x;
    view.y = box_y;
    view.w = box_w;
    view.h = box_h;
    view.max_items = max_items;
    view.start = start_idx;
    view.selected = selected;
    view.total = (int)lsp_completion_items.size();
    view.all_total = (int)lsp_completion_all_items.size();
    view.filtered = view.all_total > view.total;
    view.prefix = lsp_completion_prefix;
    for (int row = 0; row < max_items; row++)
    {
      const int item_idx = start_idx + row;
      if (item_idx < 0 || item_idx >= (int)lsp_completion_items.size())
      {
        break;
      }
      const auto &item = lsp_completion_items[item_idx];
      CompletionItemView iv;
      iv.label = item.label;
      iv.kind = item.kind;
      iv.kind_name = completion_kind_name(item.kind);
      iv.kind_icon = completion_kind_icon(item.kind, use_nerd_icons);
      iv.deprecated = item.deprecated;
      iv.detail = one_line_text(item.detail);
      iv.documentation = one_line_text(item.documentation);
      iv.match = completion_matching::match_positions(lsp_completion_prefix, item.label);
      view.items.push_back(std::move(iv));
    }
    if (lua_api->emit_lsp_completion(view))
    {
      return;
    }
  }

  UIRect rect = {box_x, box_y, box_w, box_h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  UIRect border_rect = {box_x - 1, box_y - 1, box_w + 2, box_h + 2};
  ui->draw_border(border_rect, theme.fg_panel_border, theme.bg_command);

  int label_w = std::clamp(longest_label, 12, std::max(12, box_w * 55 / 100));
  int meta_x = box_x + 2 + label_w + 1;
  int meta_w = std::max(0, box_w - (meta_x - box_x) - 1);

  for (int row = 0; row < max_items; row++)
  {
    int item_idx = start_idx + row;
    if (item_idx < 0 || item_idx >= (int)lsp_completion_items.size())
    {
      break;
    }

    const auto &item = lsp_completion_items[item_idx];
    bool selected_row = (item_idx == selected);
    int fg = selected_row ? theme.fg_selection : theme.fg_command;
    int bg = selected_row ? theme.bg_selection : theme.bg_command;
    std::string icon = completion_kind_icon(item.kind, use_nerd_icons);
    std::string label = clip_text(icon + item.label, label_w);
    std::string meta = completion_kind_name(item.kind);
    if (item.deprecated)
    {
      meta += " deprecated";
    }
    std::string detail =
        !item.detail.empty() ? one_line_text(item.detail) : one_line_text(item.documentation);
    if (!detail.empty())
    {
      meta += "  " + detail;
    }
    meta = clip_text(meta, meta_w);

    UIRect row_rect = {box_x, box_y + row, box_w, 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    ui->draw_text(box_x + 1, box_y + row, label, fg, bg, selected_row);
    if (meta_w > 0)
    {
      ui->draw_text(meta_x,
                    box_y + row,
                    meta,
                    selected_row ? theme.fg_selection : theme.fg_comment,
                    bg,
                    selected_row);
    }
  }

  UIRect footer_rect = {box_x, box_y + max_items, box_w, 1};
  ui->fill_rect(footer_rect, " ", theme.fg_comment, theme.bg_command);
  std::string footer = std::to_string(std::clamp(selected + 1, 1, (int)lsp_completion_items.size()))
                       + "/" + std::to_string(lsp_completion_items.size());
  if (!lsp_completion_prefix.empty())
  {
    footer += "  " + lsp_completion_prefix;
  }
  if ((int)lsp_completion_all_items.size() > (int)lsp_completion_items.size())
  {
    footer += "  filtered";
  }
  ui->draw_text(box_x + 1,
                box_y + max_items,
                clip_text(footer, std::max(0, box_w - 2)),
                theme.fg_comment,
                theme.bg_command);
}
