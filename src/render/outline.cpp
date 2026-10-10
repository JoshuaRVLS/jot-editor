#include "core/editor.h"
#include "lua/api/api.h"
#include "render/pane_edges.h"
#include "render/winbar_model.h"
#include "syntax/index.h"
#include "ui/text.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace
{

  // The kind -> color mapping now lives with the winbar (render/winbar_model.h),
  // which paints the same kinds as breadcrumbs; the outline keeps its own name
  // for it so the call sites below read the same.
  int outline_symbol_color(const Theme &theme, const std::string &kind)
  {
    return Winbar::symbol_color(theme, kind);
  }

  // A node's identity inside the tree: its kind and name under its parent's
  // identity, so a rebuild can recognize the same container after the lines
  // around it moved. Line numbers cannot be the key -- every edit above a
  // symbol shifts them -- and a name alone is not enough (two scopes may each
  // hold a `size`).
  std::string outline_node_key(const std::vector<SymbolMatch> &symbols, int index)
  {
    std::string key;
    for (int i = index; i >= 0 && i < (int)symbols.size(); i = symbols[(size_t)i].parent)
    {
      key = symbols[(size_t)i].kind + "\x1f" + symbols[(size_t)i].name + "\x1e" + key;
      if (symbols[(size_t)i].parent >= i)
      {
        break; // a malformed parent link, not a reason to loop forever
      }
    }
    return key;
  }

} // namespace

void Editor::note_outline_edit()
{
  outline_panel.dirty = true;
}

void Editor::ensure_outline_fresh(bool force)
{
  if (!outline_active())
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  FileBuffer &buf = get_buffer();
  if (!force && outline_panel.buffer == current_buffer && !outline_panel.dirty)
  {
    return;
  }
  // While the user is typing in the same buffer, rebuild at most every
  // ~250 ms so the symbol extractor never runs on every keystroke of a
  // large file. A buffer switch always rebuilds immediately.
  if (!force && outline_panel.dirty && outline_panel.buffer == current_buffer)
  {
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    if (outline_panel.last_rebuild_ms > 0 && now - outline_panel.last_rebuild_ms < 250)
    {
      return;
    }
  }

  // Carry the fold state over a same-buffer rebuild: a group the user folded
  // stays folded while they type (rebuilds run every ~250 ms), matched by its
  // path in the tree. A buffer switch starts fresh with every container open --
  // VS Code's outline shows the whole tree until it is folded.
  const bool same_buffer = outline_panel.buffer == current_buffer;
  std::vector<std::string> folded;
  if (same_buffer)
  {
    for (int i = 0; i < (int)outline_panel.symbols.size(); i++)
    {
      if (outline_panel.symbols[(size_t)i].has_children()
          && !(i < (int)outline_panel.expanded.size() && outline_panel.expanded[(size_t)i]))
      {
        folded.push_back(outline_node_key(outline_panel.symbols, i));
      }
    }
  }
  const int previous_scroll = outline_panel.scroll;

  outline_panel.symbols.clear();
  if (!buf.is_lazy() && !buf.filepath.empty() && buf.line_count() > 0)
  {
    outline_panel.symbols = SymbolIndex::extract_outline(buf.lines, buf.filepath);
  }
  outline_panel.buffer = current_buffer;
  outline_panel.dirty = false;
  outline_panel.last_rebuild_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
  outline_panel.selected =
      std::clamp(outline_panel.selected, 0, std::max(0, (int)outline_panel.symbols.size() - 1));
  // Scroll and folds survive the rebuild that a keystroke triggers; a buffer
  // switch starts at the top, everything open.
  outline_panel.expanded.assign(outline_panel.symbols.size(), 1);
  for (int i = 0; i < (int)outline_panel.symbols.size(); i++)
  {
    if (!outline_panel.symbols[(size_t)i].has_children())
    {
      continue;
    }
    const std::string key = outline_node_key(outline_panel.symbols, i);
    if (std::find(folded.begin(), folded.end(), key) != folded.end())
    {
      outline_panel.expanded[(size_t)i] = 0;
    }
  }
  outline_panel.visible =
      SymbolIndex::outline_visible_rows(outline_panel.symbols, outline_panel.expanded);
  outline_panel.scroll =
      same_buffer ? previous_scroll : 0;
  outline_panel.scroll =
      std::clamp(outline_panel.scroll, 0, std::max(0, (int)outline_panel.visible.size() - 1));
  outline_panel.hover_row = -1;
}

// Recomputes the visible-row order from the tree and the expanded marks, then
// keeps the selection and scroll answers in range. Every mutation of the
// expanded marks goes through here, so selection/scroll/rows stay in step.
void Editor::outline_refresh_visible()
{
  if (outline_panel.expanded.size() < outline_panel.symbols.size())
  {
    outline_panel.expanded.resize(outline_panel.symbols.size(), 1);
  }
  outline_panel.visible =
      SymbolIndex::outline_visible_rows(outline_panel.symbols, outline_panel.expanded);
  outline_panel.selected =
      std::clamp(outline_panel.selected, 0, std::max(0, (int)outline_panel.visible.size() - 1));
  outline_panel.scroll =
      std::clamp(outline_panel.scroll, 0, std::max(0, (int)outline_panel.visible.size() - 1));
}

void Editor::outline_toggle_expanded(int flat_index)
{
  ensure_outline_fresh();
  if (flat_index < 0 || flat_index >= (int)outline_panel.symbols.size())
  {
    return;
  }
  if (!outline_panel.symbols[(size_t)flat_index].has_children())
  {
    return; // a leaf has nothing to fold
  }
  if ((int)outline_panel.expanded.size() <= flat_index)
  {
    outline_panel.expanded.resize((size_t)flat_index + 1, 1);
  }
  outline_panel.expanded[(size_t)flat_index] = !outline_panel.expanded[(size_t)flat_index];
  outline_refresh_visible();
  needs_redraw = true;
}

void Editor::toggle_outline_panel()
{
  if (outline_active())
  {
    close_outline_panel();
    return;
  }
  open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  active_plugin_panel.clear();
  show_debugger_panel = false;
  needs_redraw = true;
  ensure_outline_fresh(true);
  if (outline_panel.symbols.empty())
  {
    set_message("Outline: no symbols in this file");
  }
  else
  {
    set_message("Outline: " + std::to_string(outline_panel.symbols.size()) + " symbols");
  }
}

void Editor::close_outline_panel()
{
  if (outline_active())
  {
    close_right_panel_tab(RIGHT_PANEL_SYMBOLS);
  }
  needs_redraw = true;
}

void Editor::outline_move_selection(int delta)
{
  ensure_outline_fresh();
  outline_refresh_visible();
  if (outline_panel.visible.empty())
  {
    return;
  }
  outline_panel.selected =
      std::clamp(outline_panel.selected + delta, 0, (int)outline_panel.visible.size() - 1);
  needs_redraw = true;
}

// Left folds the selection's own subtree, Right expands it (walking down into
// the first child when it is already open, VS Code's outline keys).
void Editor::outline_collapse_or_expand(bool expand)
{
  ensure_outline_fresh();
  outline_refresh_visible();
  if (outline_panel.visible.empty())
  {
    return;
  }
  const int flat = outline_panel.visible[(size_t)outline_panel.selected];
  const SymbolMatch &node = outline_panel.symbols[(size_t)flat];
  if (expand)
  {
    if (node.has_children()
        && !(outline_panel.expanded.size() > (size_t)flat && outline_panel.expanded[(size_t)flat]))
    {
      outline_toggle_expanded(flat);
      return;
    }
    if (node.has_children())
    {
      // Already open: move down into the first child.
      if (outline_panel.selected + 1 < (int)outline_panel.visible.size())
      {
        outline_panel.selected++;
        needs_redraw = true;
      }
    }
    return;
  }
  if (node.has_children() && outline_panel.expanded.size() > (size_t)flat
      && outline_panel.expanded[(size_t)flat])
  {
    outline_toggle_expanded(flat);
    return;
  }
  // A leaf (or an already-closed group): move up to its parent.
  if (node.parent >= 0)
  {
    for (int i = outline_panel.selected; i-- > 0;)
    {
      if (outline_panel.visible[(size_t)i] == node.parent)
      {
        outline_panel.selected = i;
        needs_redraw = true;
        return;
      }
    }
  }
}

void Editor::outline_jump_selected()
{
  outline_jump_to(outline_panel.selected);
}

void Editor::outline_jump_to(int visible_index)
{
  ensure_outline_fresh(true);
  outline_refresh_visible();
  if (outline_panel.visible.empty())
  {
    set_message("Outline: no symbols in this file");
    return;
  }
  visible_index = std::clamp(visible_index, 0, (int)outline_panel.visible.size() - 1);
  const int flat = outline_panel.visible[(size_t)visible_index];
  const SymbolMatch &symbol = outline_panel.symbols[(size_t)flat];
  FileBuffer &buf = get_buffer();
  if (buf.line_count() == 0)
  {
    return;
  }
  buf.cursor.y = std::clamp(symbol.line, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x = std::clamp(symbol.column, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  reveal_cursor_centered();
  needs_redraw = true;
  record_jump();
  set_message(get_filename(buf.filepath) + ":" + std::to_string(buf.cursor.y + 1) + "  "
              + symbol.kind + "  " + symbol.name);
}

void Editor::render_outline_panel()
{
  if (!outline_active() || !ui)
  {
    return;
  }
  ensure_outline_fresh();

  const int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = side_column_top();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  UIRect panel = {panel_x, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_terminal, theme.bg_terminal);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal, pane_layout::kNoEdges);

  const std::string title = " Outline ";
  ui->draw_text(panel_x + 1,
                panel_y,
                title,
                theme.fg_terminal_tab_focused,
                theme.bg_terminal_tab_focused,
                true);

  const int content_x = panel_x + 1;
  // The dock's tab strip owns panel_y + 1 and the header sits directly under
  // it: the outline has no view-tab row of its own, so nothing is reserved
  // between the two (the panels that do have one -- git, debugger -- keep
  // their tab row at panel_y + 2 and their header at panel_y + 3). The Lua
  // side_panel kit lays the outline out the same way, so the two paint paths
  // put the rows on the same cells.
  const int content_y = panel_y + 2;
  const int content_w = std::max(1, panel_w - 2);
  // No bottom border row to reserve: the panel's last row is content, marked
  // off from the status line by its background (see pane_edges.h).
  const int content_h = std::max(0, panel_h - 2);

  // Hand the model to a Lua UI handler when one is registered; it owns the
  // paint. Native fallback below stays byte-identical.
  SidePanelView view;
  view.x = panel_x;
  view.y = panel_y;
  view.w = panel_w;
  view.h = panel_h;
  view.title = " Outline ";
  build_right_panel_tab_strip_view(view);
  render_right_panel_tab_strip(panel_x, panel_y, panel_w);

  // Header: current file + symbol count.
  std::string file_label = "No file";
  const FileBuffer *buf = nullptr;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    buf = &buffers[(size_t)current_buffer];
  }
  if (buf && !buf->filepath.empty())
  {
    file_label = std::filesystem::path(buf->filepath).filename().string();
  }
  const std::string header = file_label + "  " + std::to_string(outline_panel.symbols.size())
                             + " symbol" + (outline_panel.symbols.size() == 1 ? "" : "s");
  view.header = ui_truncate_cells(header, content_w);
  view.header_fg = theme.fg_status_info;
  ui->draw_text(content_x, content_y, view.header, theme.fg_status_info, theme.bg_terminal, true);

  const int body_y = content_y + 1;
  const int body_h = std::max(0, content_h - 1);

  if (outline_panel.symbols.empty())
  {
    std::string note = "No symbols found";
    if (!buf || buf->filepath.empty())
    {
      note = "Open a file to see its symbols";
    }
    view.note = note;
    view.note_fg = theme.fg_comment;
    if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
    {
      return;
    }
    ui->draw_text(
        content_x, body_y, ui_truncate_cells(note, content_w), theme.fg_comment, theme.bg_terminal);
    return;
  }

  // Keep the selection on screen, then clamp the scroll window -- over the
  // visible rows of the tree, which is what a row here means.
  outline_refresh_visible();
  const int visible_count = (int)outline_panel.visible.size();
  int max_scroll = std::max(0, visible_count - body_h);
  if (outline_panel.selected < outline_panel.scroll)
  {
    outline_panel.scroll = outline_panel.selected;
  }
  if (outline_panel.selected >= outline_panel.scroll + body_h)
  {
    outline_panel.scroll = std::max(0, outline_panel.selected - body_h + 1);
  }
  outline_panel.scroll = std::clamp(outline_panel.scroll, 0, max_scroll);

  for (int row = 0; row < body_h; row++)
  {
    const int visible_index = outline_panel.scroll + row;
    if (visible_index < 0 || visible_index >= visible_count)
    {
      break;
    }
    const int flat = outline_panel.visible[(size_t)visible_index];
    const SymbolMatch &symbol = outline_panel.symbols[(size_t)flat];
    const bool selected = (visible_index == outline_panel.selected);

    // The row: indent, chevron, icon, name -- the detail in dimmer ink at the
    // row's end (the declared type, or the line number when there is none).
    const std::string indent((size_t)std::max(0, symbol.depth) * 2, ' ');
    const bool open = symbol.has_children() && (int)outline_panel.expanded.size() > flat
                      && outline_panel.expanded[(size_t)flat];
    std::string text = indent;
    // The chevron's two cells are spent whether or not the row has one (a leaf
    // keeps them blank), so the kind icons of a group and of its members line
    // up in one column beside the names -- never hanging off the indent.
    text += symbol.has_children() ? (open ? "\u25bc " : "\u25b6 ") : "  ";
    text += Winbar::symbol_icon(symbol.kind);
    text += " ";
    text += symbol.name;
    // A hint beside the name, never the whole row: the detail is capped the way
    // the Lua side-panel kit caps it, so a long declared type cannot eat the
    // name (and its icon) when the panel is narrow.
    const std::string detail =
        symbol.detail.empty()
            ? std::to_string(symbol.line + 1)
            : ui_truncate_cells(symbol.detail, std::clamp(content_w / 4, 3, 7));

    SidePanelRowView r;
    r.text = text;
    r.detail = detail;
    const bool hovered = !selected && visible_index == outline_panel.hover_row;
    r.fg = selected ? theme.fg_selection : outline_symbol_color(theme, symbol.kind);
    r.bg = selected ? theme.bg_selection : (hovered ? theme.bg_selection : theme.bg_terminal);
    r.bold = selected;
    r.selected = selected;
    r.hovered = hovered;
    view.rows.push_back(std::move(r));
  }
  if (lua_api && lua_api->has_lua_ui_handler("side_panel") && lua_api->emit_side_panel(view))
  {
    return;
  }
  for (size_t i = 0; i < view.rows.size(); i++)
  {
    const SidePanelRowView &r = view.rows[i];
    ui->fill_rect({content_x, body_y + (int)i, content_w, 1}, " ", r.fg, r.bg);
    const int name_w = std::max(1, content_w - 1 - (int)ui_cell_count(r.detail));
    ui->draw_text(
        content_x, body_y + (int)i, ui_truncate_cells(r.text, name_w), r.fg, r.bg, r.bold);
    ui->draw_text(content_x + std::max(0, content_w - (int)ui_cell_count(r.detail)),
                  body_y + (int)i,
                  r.detail,
                  r.selected ? theme.fg_selection : theme.fg_comment,
                  r.bg);
  }
}

// Mouse for the dock's outline: motion moves the hover highlight, a press on a
// container's chevron folds it, and a press anywhere else in the body selects
// the row and jumps the buffer to the symbol. The geometry is the render's
// (render_outline_panel) and the Lua side_panel kit's: the tab strip owns the
// panel's first interior row, the header the row under it, and the body starts
// on panel_y + 3.
bool Editor::handle_outline_panel_mouse(int x, int y, bool is_click)
{
  if (!outline_active() || !ui)
  {
    return false;
  }
  const int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return false;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = side_column_top();
  const int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  if (x < panel_x || x >= panel_x + panel_w || y < panel_y || y >= panel_y + panel_h)
  {
    // Pointer left the panel: drop the hover highlight.
    if (outline_panel.hover_row != -1)
    {
      outline_panel.hover_row = -1;
      needs_redraw = true;
    }
    return false;
  }
  ensure_outline_fresh();
  const int body_y = panel_y + 3;
  const int body_h = std::max(0, panel_h - 3);
  const int row = y - body_y;
  const int index = outline_panel.scroll + row;
  const bool on_row = row >= 0 && row < body_h && index >= 0
                      && index < (int)outline_panel.visible.size();
  if (!is_click)
  {
    const int hover = on_row ? index : -1;
    if (outline_panel.hover_row != hover)
    {
      outline_panel.hover_row = hover;
      needs_redraw = true;
    }
    return true;
  }
  // A press anywhere in the dock takes the focus, the way the tab strip does.
  outline_panel.hover_row = -1;
  focus_state = FOCUS_RIGHT_PANEL;
  needs_redraw = true;
  if (!on_row)
  {
    return true;
  }
  const int flat = outline_panel.visible[(size_t)index];
  const SymbolMatch &symbol = outline_panel.symbols[(size_t)flat];
  // The row is indent, chevron, icon, name; the chevron is the two cells the
  // group's fold is drawn in, so a press there folds instead of jumping.
  const int chevron_span = symbol.has_children() ? 2 : 0;
  const bool on_chevron = chevron_span > 0 && x >= panel_x + 1 + symbol.depth * 2
                          && x < panel_x + 1 + symbol.depth * 2 + chevron_span;
  outline_panel.selected = index;
  if (on_chevron)
  {
    outline_toggle_expanded(flat);
  }
  else
  {
    outline_jump_selected();
  }
  return true;
}
