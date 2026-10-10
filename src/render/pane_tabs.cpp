// A pane's own tab strip (a VSCode editor group): the layout the painter and
// the mouse share, the paint itself, and the drop hint a live drag adds.
//
// The workspace strip (src/render/tabs.cpp) stays the window-wide list on row
// 0; this strip is the pane's own `tab_buffer_ids` list with `buffer_id`
// active, drawn on the pane's first chrome row while pane_shows_tab_strip is
// true (pane_tabs=auto by default: the row arrives with a split, or with
// `tabline = false`, so a lone pane never paints the workspace list twice).
// Widths come from
// the same allocator the workspace strip uses, so a narrow pane ellipsizes the
// same way and the overflow chip reports what did not fit.
//
// Everything inside a PaneTabLayout is relative to its own x/y (the pane's
// corner); the painter and the hit-tests both add them back, so neither can
// drift from the other.
#include "core/editor.h"
#include "core/file_icons.h"
#include "render/pane_edges.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // Narrower than the workspace strip's floor: a pane can be half the screen,
  // and a name has to stay readable in it. The ceiling keeps one long path from
  // swallowing the strip.
  constexpr int kMinPaneTabCells = 10;
  constexpr int kMaxPaneTabCells = 22;

  // The workspace strip's middle ellipsis: the head says which file, the tail
  // keeps the extension.
  std::string ellipsize_middle(const std::string &text, int max_cells)
  {
    if (max_cells <= 0)
    {
      return {};
    }
    if (ui_cell_count(text) <= max_cells)
    {
      return text;
    }
    if (max_cells <= 3)
    {
      return ui_take_cells(text, max_cells);
    }
    return ui_take_cells(text, max_cells - 1) + "…";
  }
} // namespace

PaneTabLayout Editor::build_pane_tab_layout(int pane_index) const
{
  PaneTabLayout layout;
  if (!ui || pane_index < 0 || pane_index >= (int)panes.size())
  {
    return layout;
  }
  const SplitPane &pane = panes[(size_t)pane_index];
  if (!pane_shows_tab_strip(pane))
  {
    return layout;
  }

  layout.pane = pane_index;
  layout.x = pane.x;
  layout.y = pane.y;
  layout.w = std::max(1, pane.w);

  const std::vector<int> &ids = pane.tab_buffer_ids;
  const int total = (int)ids.size();
  if (total == 0)
  {
    return layout;
  }

  // Labels first: the widths depend on them, and duplicate names have to be
  // told apart before anything is measured.
  std::vector<std::string> paths;
  std::vector<std::string> icons;
  std::vector<int> icon_colors;
  std::vector<int> naturals;
  paths.reserve(ids.size());
  for (int id : ids)
  {
    const FileBuffer &buffer = buffers[(size_t)id];
    paths.push_back(buffer.filepath);
    // Always through the icon table, empty path included: it answers with the
    // generic file outline. A null glyph would build a std::string from a null
    // pointer, which throws (the workspace strip hit that once).
    const jot_icons::FileTypeIcon icon = jot_icons::file_type_icon(buffer.filepath);
    icons.push_back(icon.glyph ? icon.glyph : "");
    icon_colors.push_back(icon.color);
    const std::string name = buffer.filepath.empty()
                                 ? std::string("[No Name]")
                                 : std::filesystem::path(buffer.filepath).filename().string();
    const int label_cells = ui_cell_count(name) + (buffer.modified ? 2 : 0);
    naturals.push_back(std::clamp(
        ui_cell_count(icons.back()) + label_cells + 5, kMinPaneTabCells, kMaxPaneTabCells));
  }
  const std::vector<std::string> names = TabStrip::unique_names(paths);

  // The window of tabs on screen, with the two chips that report the rest. The
  // allocator decides: everything that fits at its natural-or-shared width
  // means no chips at all; otherwise the widest window that fits at the floor
  // width is shown, and the chips take their cells first.
  int start = std::clamp(pane.tab_scroll_index, 0, std::max(0, total - 1));
  const std::string left_label = start > 0 ? ("‹" + std::to_string(start)) : std::string();
  const int left_cells = start > 0 ? ui_cell_count(left_label) + 1 : 0;
  std::vector<int> widths;
  int end = total;
  const std::vector<int> full(naturals.begin() + start, naturals.end());
  if (!TabStrip::allocate_widths(
          full, std::max(1, layout.w - left_cells), kMinPaneTabCells, widths))
  {
    end = start + 1;
    for (int count = (int)full.size(); count >= 1; count--)
    {
      const int remaining = total - (start + count);
      const std::string right_label =
          remaining > 0 ? ("›+" + std::to_string(remaining)) : std::string();
      const int right_cells = remaining > 0 ? ui_cell_count(right_label) + 1 : 0;
      std::vector<int> window(naturals.begin() + start, naturals.begin() + start + count);
      if (TabStrip::allocate_widths(
              window, std::max(1, layout.w - left_cells - right_cells), kMinPaneTabCells, widths))
      {
        end = start + count;
        break;
      }
    }
  }

  layout.first_index = start;
  layout.hidden_before = start;
  layout.hidden_after = std::max(0, total - end);
  if (start > 0)
  {
    layout.scroll_left_label = left_label;
    layout.scroll_left_x = 0;
    layout.scroll_left_end_x = left_cells;
  }
  if (layout.hidden_after > 0)
  {
    layout.overflow_label = "›+" + std::to_string(layout.hidden_after);
    layout.overflow_x = std::max(0, layout.w - ui_cell_count(layout.overflow_label) - 1);
  }

  int x = left_cells;
  for (int i = start; i < end; i++)
  {
    const int id = ids[(size_t)i];
    const FileBuffer &buffer = buffers[(size_t)id];
    const int width = widths[(size_t)(i - start)];
    PaneTabSegment segment;
    segment.buffer_id = id;
    segment.tab_index = i;
    segment.x = x;
    segment.end_x = std::min(layout.w, x + width);
    segment.active = id == pane.buffer_id;
    segment.modified = buffer.modified;
    segment.preview = buffer.is_preview;
    segment.hovered = pane_index == pane_tab_hover_pane && i == pane_tab_hover_index;
    segment.icon = icons[(size_t)i];
    segment.icon_fg = icon_colors[(size_t)i];
    // Jump mode trades the file glyph for the letter the tab answers to, the
    // same trade the workspace strip makes (tabs.cpp): the letters come from
    // the workspace order, so a letter shown here picks the same buffer it
    // would pick there (ui/tab_order.h). `letters()` is the read-only face of
    // the assignment, which the key handler already ran for every tab.
    if (tabline_jump_mode)
    {
      const auto &letters = tab_order.letters();
      const auto letter = letters.find(buffer.tab_uid);
      segment.jump_letter = letter != letters.end() ? letter->second : 0;
    }

    const int icon_cells = ui_cell_count(segment.icon);
    const int label_cells = std::max(1, width - icon_cells - 4);
    const std::string marker = buffer.modified ? " ●" : "";
    segment.label = " " + ellipsize_middle(names[(size_t)i] + marker, label_cells) + " ";
    segment.label_x = segment.x + 1 + icon_cells;
    const int label_end = segment.label_x + ui_cell_count(segment.label);
    segment.close_x = std::min(segment.end_x - 2, std::max(segment.label_x, label_end));
    segment.hidden_close = label_end > segment.close_x + 1;
    x = segment.end_x;
    layout.segments.push_back(std::move(segment));
  }
  return layout;
}

void Editor::render_pane_tab_strip(int pane_index)
{
  if (!ui || pane_index < 0 || pane_index >= (int)panes.size())
  {
    return;
  }
  const SplitPane &pane = panes[(size_t)pane_index];
  if (!pane_shows_tab_strip(pane))
  {
    return;
  }
  const PaneTabLayout layout = build_pane_tab_layout(pane_index);
  const int y = pane.y;
  ui->fill_rect(
      {pane.x, y, std::max(1, pane.w), 1}, " ", theme.fg_tab_inactive, theme.bg_tab_inactive);

  if (layout.scroll_left_x >= 0)
  {
    ui->draw_text(layout.x + layout.scroll_left_x,
                  y,
                  layout.scroll_left_label,
                  theme.fg_comment,
                  theme.bg_tab_inactive);
  }
  for (const PaneTabSegment &tab : layout.segments)
  {
    int fg = tab.active ? theme.fg_tab_active : theme.fg_tab_inactive;
    int bg = tab.active ? theme.bg_tab_active : theme.bg_tab_inactive;
    if (tab.hovered && !tab.active && !pane_tab_drag_active)
    {
      fg = theme.fg_tab_hover;
      bg = theme.bg_tab_hover;
    }
    // The leading edge: inked on the active tab, a separator on the others
    // (the workspace strip's rule, per pane).
    ui->draw_text(layout.x + tab.x,
                  y,
                  "▌",
                  tab.active ? theme.fg_active_border : theme.fg_tab_separator,
                  bg,
                  tab.active);
    // Jump mode trades the file glyph for the letter the tab answers to.
    if (tab.jump_letter)
    {
      ui->draw_text(layout.x + tab.x + 1, y, std::string(1, tab.jump_letter),
                    theme.fg_tab_target, bg);
    }
    else if (!tab.icon.empty())
    {
      ui->draw_text(layout.x + tab.x + 1,
                    y,
                    tab.icon,
                    tab.icon_fg >= 0 ? tab.icon_fg : fg,
                    bg,
                    tab.active,
                    tab.preview);
    }
    ui->draw_text(layout.x + tab.label_x, y, tab.label, fg, bg, tab.active, tab.preview);
    ui->draw_text(layout.x + tab.close_x,
                  y,
                  tab.hidden_close ? " " : "×",
                  tab.active ? theme.fg_tab_close : theme.fg_comment,
                  bg);
  }
  if (layout.overflow_x >= 0)
  {
    ui->draw_text(layout.x + layout.overflow_x,
                  y,
                  layout.overflow_label,
                  theme.fg_comment,
                  theme.bg_tab_inactive);
  }
  // The live drag's slot marker, when this pane is the one it would land in.
  if (pane_tab_drag_active && pane_tab_drop.kind == PANE_TAB_DROP_INSERT
      && pane_tab_drop.pane == pane_index && pane_tab_drop.on_strip)
  {
    const int drop_x = pane_tab_drop_x(layout);
    if (drop_x >= 0 && drop_x < layout.w)
    {
      ui->draw_text(layout.x + drop_x, y, "│", theme.fg_tab_drop, theme.bg_tab_active, true);
    }
  }
}

// Drawn after every pane so it reads over the text: a body insert outlines the
// pane the tab would join, an edge target inks the one side the split would
// take. (A strip-row drop paints its slot marker inside render_pane_tab_strip.)
void Editor::render_pane_tab_drag_hint()
{
  if (!ui || !pane_tab_drag_active || pane_tab_drop.kind == PANE_TAB_DROP_NONE)
  {
    return;
  }
  const int pane_index = pane_tab_drop.pane;
  if (pane_index < 0 || pane_index >= (int)panes.size())
  {
    return;
  }
  if (pane_zoom_active && pane_index != current_pane)
  {
    return;
  }
  const SplitPane &pane = panes[(size_t)pane_index];
  if (pane.h <= 0 || pane.w <= 0)
  {
    return;
  }
  const UIRect rect = {pane.x, pane.y, std::max(1, pane.w), std::max(1, pane.h)};
  UIBorderEdges edges = pane_layout::kNoEdges;
  if (pane_tab_drop.kind == PANE_TAB_DROP_INSERT)
  {
    if (pane_tab_drop.on_strip)
    {
      return;
    }
    edges.left = true;
    edges.right = true;
    edges.top = true;
    edges.bottom = true;
  }
  else
  {
    switch (pane_tab_drop.edge)
    {
    case 1:
      edges.left = true;
      break;
    case 2:
      edges.right = true;
      break;
    case 3:
      edges.top = true;
      break;
    case 4:
      edges.bottom = true;
      break;
    default:
      return;
    }
  }
  ui->draw_border(rect, theme.fg_tab_drop, theme.bg_panel_border, edges);
}

int Editor::pane_tab_position_at(const PaneTabLayout &layout, int x) const
{
  const int local = x - layout.x;
  for (size_t i = 0; i < layout.segments.size(); i++)
  {
    if (local >= layout.segments[i].x && local < layout.segments[i].end_x)
    {
      return (int)i;
    }
  }
  return -1;
}

int Editor::pane_tab_scroll_chip_at(const PaneTabLayout &layout, int x) const
{
  const int local = x - layout.x;
  if (layout.scroll_left_x >= 0 && local >= layout.scroll_left_x
      && local < layout.scroll_left_end_x)
  {
    return -1;
  }
  if (layout.overflow_x >= 0 && local >= layout.overflow_x)
  {
    return 1;
  }
  return 0;
}

int Editor::pane_tab_drop_x(const PaneTabLayout &layout) const
{
  if (layout.segments.empty())
  {
    return 0;
  }
  // The marker sits on the leading edge of the tab the dragged one lands in
  // front of, or the end of the last tab when the slot is past them all.
  for (const PaneTabSegment &tab : layout.segments)
  {
    if (tab.tab_index >= pane_tab_drop.slot)
    {
      return tab.x;
    }
  }
  return layout.segments.back().end_x;
}
