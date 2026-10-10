// The workspace tab strip: one row of the pane area, the open buffers in the
// workspace's order. It spans the columns between the explorer and the right
// dock (the full width when neither is up): the side columns are full height
// and own their top row, so the strip owns that row only between them.
//
// This used to be a per-pane strip, built and painted inside each pane. The
// strip is now one row for the whole editor (the row the native menu bar gave
// up), so a buffer that only another pane has open is still visible and
// reachable; panes keep their local tab lists as history (Ctrl+Tab, close
// focus) but draw no strip of their own. The order, its pins and the
// jump-to-buffer letters live in ui/tab_order.h.
#include "core/editor.h"
#include "core/file_icons.h"
#include "lua/api.h"
#include "ui/components.h"
#include "ui/text.h"
#include "ui_internal.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // A tab narrower than this says nothing (its label is ellipsized to nothing
  // but the icon); a tab wider than this stops being a tab and becomes a list
  // entry, so the strip gives the space back to its neighbours.
  constexpr int kMinTabCells = 10;
  constexpr int kMaxTabCells = 24;
  // The dot that marks a buffer another pane is showing (the file finder's "has
  // a tab" dot, same glyph) and the pin glyph.
  const char *const kElsewhereGlyph = "\uf111";
  const char *const kPinGlyph = "\uf08d4";

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

  int cells_of(const std::vector<int> &widths)
  {
    int total = 0;
    for (int w : widths)
    {
      total += w;
    }
    return total;
  }
} // namespace

// The order is reconciled against the live buffers here, in the one place that
// paints the strip: the model is fed the buffer vector, hands identities to
// buffers it has not seen and drops the ones that are gone (see
// ui/tab_order.h for why it is a reconciliation rather than a set of
// open/close notifications).
void Editor::sync_tab_order()
{
  long long current_uid = 0;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    current_uid = buffers[(size_t)current_buffer].tab_uid;
  }
  tab_order.sync(buffers, current_uid, tabline_insert);
}

// The side columns are full height, so the strip is not allowed to paint under
// them or take their clicks; it spans the columns between the explorer and the
// dock. A zoomed terminal covers both columns, so its strip takes the row back.
void Editor::tabline_span(int &x, int &w) const
{
  const int screen_w = ui ? std::max(1, ui->get_render_width()) : 0;
  const bool covered = terminal_zoom_active;
  const int left = (!covered && show_sidebar) ? effective_sidebar_width() : 0;
  const int right = covered ? 0 : effective_right_panel_width();
  x = std::min(left, screen_w);
  w = std::max(1, screen_w - x - right);
}

bool Editor::tabline_contains_x(int x) const
{
  int left = 0;
  int w = 0;
  tabline_span(left, w);
  return x >= left && x < left + w;
}

Editor::FileTabLayout Editor::build_tabline_layout()
{
  sync_tab_order();

  FileTabLayout layout;
  layout.y = tabline_y();
  tabline_span(layout.x, layout.w);

  const std::vector<int> ids = tab_order.indices(buffers);
  const int total = (int)ids.size();
  if (total == 0)
  {
    return layout;
  }

  // Labels first: the widths depend on them, and duplicates have to be told
  // apart before anything is measured.
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
    // generic file outline for one. A zero-initialised FileTypeIcon has a null
    // `glyph`, and pushing that into the vector below built a std::string from
    // a null pointer -- which throws. That is what a scratch buffer hit (Alt+N,
    // the home menu's new file): the tab strip died mid-layout, so the new tab
    // never appeared and the command looked like it had not run at all.
    const jot_icons::FileTypeIcon icon = jot_icons::file_type_icon(buffer.filepath);
    icons.push_back(icon.glyph);
    icon_colors.push_back(icon.color);
    // "▌ icon name ● ×": the width the tab wants when nothing forces it to
    // shrink, clamped so one long path cannot swallow the strip.
    const int label_cells = ui_cell_count(paths.back().empty()
                                              ? std::string("[No Name]")
                                              : std::filesystem::path(paths.back())
                                                    .filename()
                                                    .string())
                            + (buffer.modified ? 2 : 0);
    naturals.push_back(std::clamp(ui_cell_count(icon.glyph) + label_cells + 5, kMinTabCells,
                                  kMaxTabCells));
  }
  const std::vector<std::string> names = TabStrip::unique_names(paths);

  // The window of tabs on screen, with the two chips that report the rest. The
  // allocator decides: if everything from `start` fits at its natural-or-shared
  // width there are no chips at all; otherwise the widest window that fits at
  // the floor width is the one shown, and the chips take their cells first.
  int start = std::clamp(tabline_scroll_index, 0, std::max(0, total - 1));
  const std::string left_label = start > 0 ? ("‹" + std::to_string(start)) : std::string();
  const int left_cells = start > 0 ? ui_cell_count(left_label) + 1 : 0;
  std::vector<int> widths;
  int end = total;
  const std::vector<int> full(naturals.begin() + start, naturals.end());
  if (!TabStrip::allocate_widths(full, std::max(1, layout.w - left_cells), kMinTabCells, widths))
  {
    end = start + 1;
    for (int count = (int)full.size(); count >= 1; count--)
    {
      const int remaining = total - (start + count);
      const std::string right_label =
          remaining > 0 ? ("›+" + std::to_string(remaining)) : std::string();
      const int right_cells = remaining > 0 ? ui_cell_count(right_label) + 1 : 0;
      std::vector<int> window(naturals.begin() + start, naturals.begin() + start + count);
      if (TabStrip::allocate_widths(window, std::max(1, layout.w - left_cells - right_cells),
                                    kMinTabCells, widths))
      {
        end = start + count;
        break;
      }
    }
  }

  layout.hidden_before = start;
  layout.hidden_after = std::max(0, total - end);
  layout.hidden_count = total;
  if (start > 0)
  {
    layout.scroll_left_label = left_label;
    layout.scroll_left_x = layout.x;
    layout.scroll_left_end_x = layout.x + left_cells;
  }
  if (layout.hidden_after > 0)
  {
    layout.overflow_label = "›+" + std::to_string(layout.hidden_after);
    layout.scroll_right_end_x = layout.x + layout.w;
    layout.scroll_right_x = layout.x + layout.w - ui_cell_count(layout.overflow_label) - 1;
    layout.overflow_x = layout.scroll_right_x;
  }

  int x = layout.x + left_cells;
  for (int i = start; i < end; i++)
  {
    const int id = ids[(size_t)i];
    const FileBuffer &buffer = buffers[(size_t)id];
    const int width = widths[(size_t)(i - start)];
    FileTabSegment segment;
    segment.buffer_id = id;
    segment.tab_index = i;
    segment.x = x;
    segment.end_x = std::min(layout.x + layout.w, x + width);
    segment.active = id == current_buffer;
    segment.modified = buffer.modified;
    segment.preview = buffer.is_preview;
    segment.pinned = tab_order.pinned(id, buffers);
    segment.hovered = i == tabline_hover_index;
    segment.visible_elsewhere = buffer_visible_in_other_pane(id);
    segment.icon = icons[(size_t)i];
    segment.icon_fg = icon_colors[(size_t)i];
    if (tabline_jump_mode)
    {
      segment.jump_letter = tab_order.letter_for(id, buffers);
    }

    const int icon_cells = ui_cell_count(segment.icon);
    std::string markers;
    if (segment.pinned)
    {
      markers += std::string(" ") + kPinGlyph;
    }
    if (segment.visible_elsewhere)
    {
      markers += std::string(" ") + kElsewhereGlyph;
    }
    const int marker_cells = ui_cell_count(markers);
    // The label gives up its marker cells, then its own: riders first (they are
    // state), then the name.
    const int label_cells = std::max(1, width - icon_cells - marker_cells - 4);
    const std::string marker = buffer.modified ? " ●" : "";
    segment.label = " " + ellipsize_middle(names[(size_t)i] + marker, label_cells) + " ";
    segment.label_x = segment.x + 1 + icon_cells;
    const int label_end = segment.label_x + ui_cell_count(segment.label);
    segment.close_x = std::min(segment.end_x - 2, std::max(segment.label_x, label_end));
    segment.hidden_close = label_end > segment.close_x + 1;
    if (marker_cells <= 2 && segment.close_x - marker_cells > segment.label_x)
    {
      segment.marker_x = segment.close_x - marker_cells;
    }
    auto git_it = git_file_status.find(ui_internal::git_status_key_for(buffers[(size_t)id]));
    if (git_it != git_file_status.end())
    {
      segment.git_status = git_it->second;
    }
    x = segment.end_x;
    layout.segments.push_back(std::move(segment));
  }
  return layout;
}

void Editor::render_tabline()
{
  if (!ui || !tabline_shown())
  {
    return;
  }
  const FileTabLayout layout = build_tabline_layout();
  const int y = layout.y;

  // No band across the row: the strip paints only the tabs themselves, and the
  // cells between them and to the right of the last one keep the background the
  // frame was cleared with. Filling the whole row with TabLine's background
  // read as a gap beside the last tab (and painted under the explorer and the
  // dock, which own their own columns now).
  if (layout.scroll_left_x >= 0)
  {
    ui->fill_rect(
        {layout.scroll_left_x, y, layout.scroll_left_end_x - layout.scroll_left_x, 1},
        " ",
        theme.fg_comment,
        theme.bg_tab_inactive);
    ui->draw_text(layout.scroll_left_x, y, layout.scroll_left_label, theme.fg_comment,
                  theme.bg_tab_inactive);
  }
  for (const FileTabSegment &tab : layout.segments)
  {
    int fg = tab.active ? theme.fg_tab_active : theme.fg_tab_inactive;
    int bg = tab.active ? theme.bg_tab_active : theme.bg_tab_inactive;
    if (!tab.active && !tab.git_status.empty())
    {
      auto git_colors = ui_internal::git_tab_colors(theme, tab.git_status);
      fg = git_colors.first;
      bg = git_colors.second;
    }
    if (tab.hovered && !tab.active && tabline_drag_index < 0)
    {
      fg = theme.fg_tab_hover;
      bg = theme.bg_tab_hover;
    }
    // The tab's whole rectangle gets its background first: the label is padded
    // and the close cell is drawn separately, so without it the cleared
    // background showed through as a stray cell inside the tab's right edge.
    ui->fill_rect({tab.x, y, std::max(1, tab.end_x - tab.x), 1}, " ", fg, bg);
    // The leading cell stays blank: the strip used to ink a ▌ block on the
    // active tab and a separator on the rest, which read as a row of bars over
    // the names. What tells the tabs apart now is the band and the label's own
    // weight, so the reserved cell is only the label's left padding.
    // Jump mode trades the file glyph for the letter the tab answers to.
    if (tab.jump_letter)
    {
      ui->draw_text(tab.x + 1, y, std::string(1, tab.jump_letter), theme.fg_tab_target, bg);
    }
    else if (!tab.icon.empty())
    {
      ui->draw_text(tab.x + 1, y, tab.icon, tab.icon_fg >= 0 ? tab.icon_fg : fg, bg, tab.active,
                    tab.preview);
    }
    ui->draw_text(tab.label_x, y, tab.label, fg, bg, tab.active, tab.preview);
    if (tab.marker_x >= 0)
    {
      if (tab.pinned)
      {
        ui->draw_text(tab.marker_x, y, kPinGlyph, theme.fg_tab_pin, bg);
      }
      if (tab.visible_elsewhere)
      {
        ui->draw_text(tab.marker_x + (tab.pinned ? 1 : 0), y, kElsewhereGlyph, theme.fg_tab_alt, bg);
      }
    }
    // The close affordance belongs to the tab the user is on: the active one
    // keeps it, a hovered one shows it as the invitation, and every other tab
    // spends the cell on nothing -- a row of × glyphs beside every name was the
    // strip's loudest detail.
    const bool close_shown = !tab.hidden_close && (tab.active || tab.hovered);
    ui->draw_text(tab.close_x, y, close_shown ? "×" : " ",
                  tab.active ? theme.fg_tab_close : theme.fg_comment, bg);
  }
  if (!layout.overflow_label.empty())
  {
    ui->fill_rect(
        {layout.scroll_right_x, y, layout.scroll_right_end_x - layout.scroll_right_x, 1},
        " ",
        theme.fg_comment,
        theme.bg_tab_inactive);
    ui->draw_text(layout.overflow_x, y, layout.overflow_label, theme.fg_comment,
                  theme.bg_tab_inactive);
  }
  if (tabline_drag_index >= 0)
  {
    // A drop marker between the two tabs the pointer is between: the strip is
    // showing where the dragged tab would land.
    const int drop_x = tabline_drop_position_x(layout);
    if (drop_x >= layout.x && drop_x < layout.x + layout.w)
    {
      ui->draw_text(drop_x, y, "│", theme.fg_tab_drop, theme.bg_tab_active);
    }
  }
}

// Where the drop marker is drawn: the leading edge of the tab the dragged tab
// would land in front of, or the end of the last tab.
int Editor::tabline_drop_position_x(const FileTabLayout &layout) const
{
  const int drop = tabline_drop_position(layout);
  if (drop < 0 || layout.segments.empty())
  {
    return -1;
  }
  if (drop >= (int)layout.segments.size())
  {
    return std::min(layout.x + layout.w - 1, layout.segments.back().end_x);
  }
  return layout.segments[(size_t)drop].x;
}

// Clicks, the close control, the middle button, the wheel chips and a drag
// along the strip. Called before any pane sees the event: the strip's row
// belongs to it between the side columns, and a motion here only ever changes
// the hover highlight -- a tab is switched by an actual press (the lesson the
// Problems tab learned the hard way).
bool Editor::handle_tabline_mouse(int x,
                                  int y,
                                  bool is_click,
                                  bool is_click_release,
                                  bool is_middle_click,
                                  bool is_motion)
{
  if (!tabline_shown() || y != tabline_y())
  {
    if (is_click_release && tabline_drag_index >= 0)
    {
      tabline_drag_index = -1;
      tabline_drag_x = -1;
      tabline_drag_moved = false;
      return true;
    }
    return false;
  }

  const FileTabLayout layout = build_tabline_layout();
  const int position = tabline_position_at(layout, x);

  if (is_motion)
  {
    bool changed = false;
    if (tabline_hover_index != position)
    {
      tabline_hover_index = position;
      changed = true;
    }
    if (tabline_drag_index >= 0)
    {
      tabline_drag_x = x;
      changed = true;
      // One step per event toward the drop position the marker is showing, so
      // the strip follows the pointer instead of jumping to it.
      const int drop = tabline_drop_position(layout);
      if (drop >= 0 && drop != tabline_drag_index)
      {
        const int step = drop > tabline_drag_index ? 1 : -1;
        const int buffer_id = tab_order.index_at(buffers, tabline_drag_index);
        if (buffer_id >= 0 && tab_order.move(buffers, buffer_id, step))
        {
          tabline_drag_index += step;
          tabline_drag_moved = true;
          reveal_tabline_position(tabline_drag_index);
        }
      }
    }
    if (changed)
    {
      needs_redraw = true;
    }
    return position >= 0;
  }

  if (is_click_release)
  {
    if (tabline_drag_index >= 0)
    {
      tabline_drag_index = -1;
      tabline_drag_x = -1;
      tabline_drag_moved = false;
      needs_redraw = true;
      return true;
    }
    return position >= 0;
  }

  if (is_click && tabline_scroll_chip_at(layout, x) != 0)
  {
    const int direction = tabline_scroll_chip_at(layout, x);
    // A chip steps a window of tabs, not one tab.
    if (tabline_scroll(direction * std::max(1, (int)layout.segments.size() - 1)))
    {
      needs_redraw = true;
    }
    return true;
  }

  if (position < 0)
  {
    return false;
  }

  const FileTabSegment &tab = layout.segments[(size_t)position];
  if ((is_click && !tab.hidden_close && x == tab.close_x) || is_middle_click)
  {
    close_buffer_at(tab.buffer_id);
    tabline_hover_index = -1;
    focus_state = FOCUS_EDITOR;
    restart_blink_pointer();
    needs_redraw = true;
    return true;
  }

  if (is_click)
  {
    tabline_activate(tab.tab_index);
    // The press also arms a drag: moving the pointer before releasing reorders
    // the tab, releasing without moving leaves the switch above as it was.
    tabline_drag_index = tab.tab_index;
    tabline_drag_x = x;
    tabline_drag_moved = false;
    focus_state = FOCUS_EDITOR;
    restart_blink_pointer();
    needs_redraw = true;
    return true;
  }
  return true;
}

int Editor::tabline_position_at(const FileTabLayout &layout, int x) const
{
  for (size_t i = 0; i < layout.segments.size(); i++)
  {
    if (x >= layout.segments[i].x && x < layout.segments[i].end_x)
    {
      return (int)i;
    }
  }
  return -1;
}

int Editor::tabline_scroll_chip_at(const FileTabLayout &layout, int x) const
{
  if (layout.scroll_left_x >= 0 && x >= layout.scroll_left_x && x < layout.scroll_left_end_x)
  {
    return -1;
  }
  if (layout.scroll_right_x >= 0 && x >= layout.scroll_right_x && x < layout.scroll_right_end_x)
  {
    return 1;
  }
  return 0;
}

int Editor::tabline_drop_position(const FileTabLayout &layout) const
{
  if (tabline_drag_x < 0 || layout.segments.empty())
  {
    return -1;
  }
  for (size_t i = 0; i < layout.segments.size(); i++)
  {
    const FileTabSegment &tab = layout.segments[i];
    if (tabline_drag_x < tab.x + (tab.end_x - tab.x) / 2)
    {
      return (int)i;
    }
  }
  return (int)layout.segments.size() - 1;
}

bool Editor::buffer_visible_in_other_pane(int buffer_id) const
{
  for (size_t i = 0; i < panes.size(); i++)
  {
    if ((int)i == current_pane)
    {
      continue;
    }
    if (panes[i].buffer_id == buffer_id)
    {
      return true;
    }
  }
  return false;
}

bool Editor::tabline_activate(int position)
{
  const int id = tab_order.index_at(buffers, position);
  if (id < 0 || id >= (int)buffers.size())
  {
    return false;
  }
  // A buffer that is on screen somewhere takes the focus there; otherwise it
  // opens in the pane you are in.
  for (size_t i = 0; i < panes.size(); i++)
  {
    if ((int)i != current_pane && panes[i].buffer_id == id)
    {
      activate_pane((int)i);
      reveal_tabline_position(position);
      return true;
    }
  }
  if (id == current_buffer)
  {
    return true;
  }
  const bool shown = show_buffer_in_current_pane(id);
  reveal_tabline_position(position);
  return shown;
}

bool Editor::show_buffer_in_current_pane(int buffer_id)
{
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    return false;
  }
  SplitPane &pane = get_pane();
  capture_pane_view(current_pane);
  pane.buffer_id = buffer_id;
  current_buffer = buffer_id;
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), buffer_id)
      == pane.tab_buffer_ids.end())
  {
    pane.tab_buffer_ids.push_back(buffer_id);
  }
  restore_pane_view(current_pane);
  focus_state = FOCUS_EDITOR;
  clamp_cursor(buffer_id);
  ensure_cursor_visible();
  needs_redraw = true;
  return true;
}

// The keyboard face of the strip. Alt+<digit> is a strip *position*: the number
// you press is the tab you can see. While the workspace strip is the list on
// screen (a lone pane), that position is a workspace position; once the pane
// area is split the groups draw their own lists, so the focused group's list is
// the one on screen and the position lands there (the same place a click on
// that strip goes, switch_pane_to_local_tab). Ctrl+Tab still walks the pane's
// history either way (see cycle_local_tab).
bool Editor::handle_tabline_key(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  (void)is_shift;
  // Jump mode owns the next keystroke: a letter that names a tab switches to it,
  // anything else (including the same key again) just puts the letters away.
  if (tabline_jump_mode)
  {
    tabline_jump_mode = false;
    needs_redraw = true;
    if (!is_ctrl && !is_alt && ch >= 32 && ch < 127)
    {
      const char want = (char)std::tolower((unsigned char)ch);
      const int buffer_id = tab_order.buffer_for_letter(buffers, want);
      if (buffer_id >= 0)
      {
        // The letter picks the buffer, not the pane: one already showing it
        // takes the focus, otherwise it opens in the pane you are in. This is
        // the number keys' rule, so a split behaves like the strip it draws.
        const int position = tab_order.position_of(buffers, buffer_id);
        if (position < 0 || !tabline_activate(position))
        {
          if (buffer_id != current_buffer)
          {
            show_buffer_in_current_pane(buffer_id);
          }
          reveal_tab_for_buffer(buffer_id);
        }
      }
    }
    return true;
  }

  if (!tabline_active())
  {
    return false;
  }

  // Which list the position keys address: the workspace strip while it owns the
  // row, the focused group's own list while the panes have taken it over.
  const bool local_strips = pane_strips_own_tabs();

  if (is_alt && ch >= '1' && ch <= '9')
  {
    const int position = ch - '1';
    if (!local_strips)
    {
      return tabline_activate(position);
    }
    if (position >= (int)get_pane().tab_buffer_ids.size())
    {
      return false;
    }
    return switch_pane_to_local_tab(current_pane, position);
  }
  if (is_alt && ch == '0')
  {
    if (!local_strips)
    {
      const int total = (int)tab_order.indices(buffers).size();
      return total > 0 && tabline_activate(total - 1);
    }
    const int total = (int)get_pane().tab_buffer_ids.size();
    return total > 0 && switch_pane_to_local_tab(current_pane, total - 1);
  }
  if (is_alt && (ch == ',' || ch == '<'))
  {
    return local_strips ? cycle_local_tab(-1) : cycle_tabline_tab(-1);
  }
  if (is_alt && (ch == '.' || ch == '>'))
  {
    return local_strips ? cycle_local_tab(1) : cycle_tabline_tab(1);
  }
  // Alt+R (lowercase: Alt+Shift+<letter> is the pane-split family, and
  // Alt+h/j/k/l is pane focus).
  if (is_alt && ch == 'r')
  {
    hide_lsp_completion();
    tabline_jump_mode = true;
    // The letters are handed out here rather than by the painter, so the key
    // that follows always has something to match.
    for (int id : tab_order.indices(buffers))
    {
      tab_order.letter_for(id, buffers);
    }
    needs_redraw = true;
    set_message("Jump to a tab by its letter");
    return true;
  }
  return false;
}

bool Editor::cycle_tabline_tab(int delta)
{
  sync_tab_order();
  const int total = (int)tab_order.indices(buffers).size();
  if (total <= 0 || delta == 0)
  {
    return false;
  }
  const int at = tab_order.position_of(buffers, current_buffer);
  const int from = at >= 0 ? at : 0;
  const int target = ((from + delta) % total + total) % total;
  return tabline_activate(target);
}

bool Editor::tabline_scroll(int delta)
{
  if (delta == 0)
  {
    return false;
  }
  const FileTabLayout layout = build_tabline_layout();
  if (layout.hidden_count == 0 || layout.hidden_after + layout.hidden_before == 0)
  {
    return false;
  }
  const int old = tabline_scroll_index;
  const int step = delta > 0 ? 1 : -1;
  tabline_scroll_index = std::clamp(tabline_scroll_index + step, 0, std::max(0, layout.hidden_count - 1));
  if (tabline_scroll_index == old)
  {
    return false;
  }
  // A scroll that cannot show anything new is not a scroll (the same guard the
  // per-pane strip had).
  const FileTabLayout after = build_tabline_layout();
  if (after.segments.empty())
  {
    tabline_scroll_index = old;
    return false;
  }
  needs_redraw = true;
  return true;
}

void Editor::reveal_tabline_position(int position)
{
  const int before = tabline_scroll_index;
  if (position < tabline_scroll_index)
  {
    tabline_scroll_index = position;
  }
  for (int guard = 0; guard < 64; guard++)
  {
    const FileTabLayout layout = build_tabline_layout();
    if (position < layout.hidden_before + (int)layout.segments.size())
    {
      break;
    }
    if (layout.hidden_after <= 0)
    {
      break;
    }
    tabline_scroll_index++;
  }
  if (tabline_scroll_index != before)
  {
    needs_redraw = true;
  }
}

// Opening/refocusing a file has to leave its strip entry on screen, the way the
// pane-local strip used to scroll to the pane's own tab.
void Editor::reveal_tab_for_buffer(int buffer_id)
{
  sync_tab_order();
  const int position = tab_order.position_of(buffers, buffer_id);
  if (position >= 0)
  {
    reveal_tabline_position(position);
  }
}

void Editor::close_other_tabs(int keep_buffer)
{
  if (keep_buffer < 0 || keep_buffer >= (int)buffers.size())
  {
    return;
  }
  // Closing erases from the middle of `buffers`, so every index to the right of
  // the one just closed shifts down. Re-find each victim by uid instead of
  // trusting a snapshot of the list, and bound the sweep by the size it had.
  const long long keep_uid = buffers[(size_t)keep_buffer].tab_uid;
  const int rounds = (int)buffers.size();
  for (int round = 0; round < rounds; round++)
  {
    int victim = -1;
    for (int i = 0; i < (int)buffers.size(); i++)
    {
      if (buffers[(size_t)i].tab_uid != keep_uid)
      {
        victim = i;
        break;
      }
    }
    if (victim < 0)
    {
      break;
    }
    close_buffer_at(victim);
  }
  // The kept tab is the one the pointer asked for: show it, wherever the
  // closes left the focus.
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (buffers[(size_t)i].tab_uid == keep_uid)
    {
      show_buffer_in_current_pane(i);
      break;
    }
  }
  needs_redraw = true;
}

// ---------------------------------------------------------------------------
// Easter egg: Konami code (↑↑↓↓←→←→) rainbow popup
// ---------------------------------------------------------------------------
