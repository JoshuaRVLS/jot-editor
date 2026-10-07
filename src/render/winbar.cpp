// The breadcrumb winbar: the row a code pane spends above its text, and the
// drop-down menu a crumb opens (dropbar.nvim's model, ported).
//
// The chain comes from features/winbar.h; this file turns it into cells, paints
// it and owns the interaction (a press opens a crumb, motion only moves the
// hover band).
// A folder row opens its own listing beside the menu it is on, keeping the level
// that offered it on screen, so the cascade shows the whole path the pointer
// walked and deeper levels drop as soon as a level's selection moves.
#include "editor.h"
#include "editor/file_icons.h"
#include "lua/api.h"
#include "syntax/index.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace
{
  // A crumb longer than this is middle-ellipsized: a 60-cell symbol name would
  // otherwise push its own file crumb off the row.
  constexpr int kMaxCrumbCells = 28;
  // The symbol index is a full-buffer scan, so while the buffer is being typed
  // in it is rebuilt at most this often (the row is a breadcrumb: a symbol that
  // appears one keystroke late is better than a scan per keystroke).
  constexpr long long kSymbolRefreshMs = 250;
  // The drop-down: its smallest width and the most rows it will show before
  // scrolling.
  constexpr int kMinMenuWidth = 18;
  constexpr int kMaxMenuRows = 14;
  // One cell: the separator between crumbs, and the chip standing in for the
  // crumbs that did not fit.
  const char *const kSeparatorGlyph = "›";
  const char *const kEllipsisGlyph = "…";
  // The dot on the entry the chain is already on (the strip's "has a tab" dot).
  const char *const kCurrentGlyph = "\uf111";
  // The chevron a folder row wears: the row opens another panel beside this
  // one. The same nf-fa chevron the telescope prompt uses.
  const char *const kChevronGlyph = "\uf054";
  // The most levels a cascade will stack. A path can nest arbitrarily deep; the
  // screen cannot, and past a point the panels would be unreadable anyway.
  constexpr int kMaxMenuLevels = 6;

  long long winbar_now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

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
    return ui_take_cells(text, max_cells - 1) + kEllipsisGlyph;
  }

  int entry_cells(const Winbar::Entry &entry)
  {
    int cells = ui_cell_count(entry.label) + (entry.current ? 2 : 0);
    if (!entry.icon.empty())
    {
      cells += ui_cell_count(entry.icon) + 1;
    }
    // A folder row carries its chevron on the right edge; reserve its cells so
    // a long name can never be drawn under it.
    if (entry.is_dir)
    {
      cells += ui_cell_count(kChevronGlyph) + 1;
    }
    return cells;
  }

  // The width a level's panel wants: its widest row plus the border and the one
  // cell of padding on each side.
  int menu_width_for(const std::vector<Winbar::Entry> &entries, int screen_w)
  {
    int widest = 0;
    for (const Winbar::Entry &entry : entries)
    {
      widest = std::max(widest, entry_cells(entry));
    }
    return std::clamp(widest + 4, kMinMenuWidth, std::max(kMinMenuWidth, screen_w));
  }

  // The height a level's panel wants: its rows (capped, and never taller than
  // the room it was measured against) plus the two border rows.
  int menu_height_for(const std::vector<Winbar::Entry> &entries, int room)
  {
    return std::min({(int)entries.size(), std::max(1, room), kMaxMenuRows}) + 2;
  }

  std::string entry_text(const Winbar::Entry &entry)
  {
    std::string text;
    if (entry.current)
    {
      text += std::string(kCurrentGlyph) + " ";
    }
    if (!entry.icon.empty())
    {
      text += entry.icon + " ";
    }
    text += entry.label;
    return text;
  }

  // One pane's row as the Lua painter sees it: the layout's own columns, so a
  // click on a glyph in the float lands on the crumb the layout put there.
  WinbarRowView winbar_row_view(const SplitPane &pane,
                                int pane_index,
                                const std::string &filepath,
                                const Winbar::WinbarLayout &layout)
  {
    WinbarRowView out;
    out.x = pane.x;
    out.y = layout.y;
    out.w = pane.w;
    out.pane = pane_index;
    out.filepath = filepath;
    out.truncated = layout.truncated;
    for (const Winbar::WinbarSegment &segment : layout.segments)
    {
      WinbarCrumbView crumb;
      if (segment.crumb_index < 0)
      {
        crumb.kind = "ellipsis";
        crumb.label = segment.label;
        crumb.ellipsis = true;
      }
      else
      {
        const Winbar::Crumb &source = layout.crumbs[(size_t)segment.crumb_index];
        crumb.label = segment.label;
        crumb.kind = source.kind;
        crumb.symbol_kind = source.symbol_kind;
        crumb.icon = source.icon;
        crumb.icon_fg = source.icon_fg;
        crumb.current = source.current;
      }
      crumb.x = segment.x;
      crumb.icon_x = segment.icon_x;
      crumb.label_x = segment.label_x;
      crumb.end_x = segment.end_x;
      crumb.hovered = segment.hovered;
      crumb.active = segment.active;
      out.crumbs.push_back(std::move(crumb));
    }
    return out;
  }
} // namespace

// The document-symbol index behind the crumb chain, cached on the buffer and
// validated by its edit generation. See FileBuffer::symbol_cache.
const std::vector<SymbolMatch> &Editor::winbar_symbols(int buffer_id)
{
  static const std::vector<SymbolMatch> empty;
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    return empty;
  }
  FileBuffer &buf = buffers[(size_t)buffer_id];
  if (buf.is_lazy() || buf.filepath.empty())
  {
    buf.symbol_cache.clear();
    buf.symbol_cache_valid = false;
    return buf.symbol_cache;
  }
  const bool stale = !buf.symbol_cache_valid || buf.symbol_cache_generation != buf.edit_generation;
  if (stale && (!buf.symbol_cache_valid || winbar_now_ms() - buf.symbol_cache_ms >= kSymbolRefreshMs))
  {
    buf.symbol_cache = SymbolIndex::extract_document_symbols(buf.lines, buf.filepath);
    buf.symbol_cache_valid = true;
    buf.symbol_cache_generation = buf.edit_generation;
    buf.symbol_cache_ms = winbar_now_ms();
  }
  return buf.symbol_cache;
}

Winbar::WinbarLayout Editor::build_winbar_layout(const SplitPane &pane, int pane_index)
{
  Winbar::WinbarLayout layout;
  layout.y = pane_winbar_y(pane);
  layout.x = pane.x;
  layout.w = std::max(1, pane.w);
  if (!pane_has_winbar(pane))
  {
    return layout;
  }
  const FileBuffer &buf = buffers[(size_t)pane.buffer_id];
  // An inactive pane shows its own remembered view, so its crumb follows the
  // symbol at *its* cursor, not the focused pane's.
  int cursor_line = buf.cursor.y;
  if (pane_index != current_pane && pane.view_buffer_id == pane.buffer_id)
  {
    cursor_line = pane.view_cursor.y;
  }
  layout.crumbs = Winbar::build(buf.filepath, root_dir, winbar_symbols(pane.buffer_id), cursor_line);
  if (layout.crumbs.empty())
  {
    return layout;
  }

  const int start = pane.x + 1;
  const int right = pane.x + std::max(1, pane.w - 1);
  const int avail = std::max(1, right - start);
  const int count = (int)layout.crumbs.size();

  std::vector<int> widths((size_t)count, 0);
  std::vector<int> icon_w((size_t)count, 0);
  std::vector<int> label_w((size_t)count, 0);
  std::vector<std::string> labels((size_t)count);
  int total = 0;
  for (int i = 0; i < count; i++)
  {
    const Winbar::Crumb &crumb = layout.crumbs[(size_t)i];
    labels[(size_t)i] = ellipsize_middle(crumb.label, kMaxCrumbCells);
    icon_w[(size_t)i] = crumb.icon.empty() ? 0 : ui_cell_count(crumb.icon) + 1;
    label_w[(size_t)i] = ui_cell_count(labels[(size_t)i]);
    // " separator icon label": one leading cell per crumb, then the glyph and
    // the name.
    widths[(size_t)i] = 1 + icon_w[(size_t)i] + label_w[(size_t)i];
    total += widths[(size_t)i];
  }

  const int ellipsis_w = ui_cell_count(kEllipsisGlyph) + 1;
  int first = 0;
  while (first < count - 1)
  {
    const int need = total + (first > 0 ? ellipsis_w : 0);
    if (need <= avail)
    {
      break;
    }
    total -= widths[(size_t)first];
    first++;
  }
  // The tail -- the file and the cursor's symbol -- is the part that identifies
  // the line, so it is the last thing to give: if it alone still does not fit,
  // its label is trimmed rather than dropped.
  if (total > avail)
  {
    const int last = count - 1;
    const int room = std::max(0, avail - 1 - icon_w[(size_t)last]);
    labels[(size_t)last] = ellipsize_middle(layout.crumbs[(size_t)last].label, room);
    label_w[(size_t)last] = ui_cell_count(labels[(size_t)last]);
    widths[(size_t)last] = 1 + icon_w[(size_t)last] + label_w[(size_t)last];
    first = last;
  }
  layout.truncated = first > 0;
  layout.first_crumb = first;

  int x = start;
  if (layout.truncated)
  {
    Winbar::WinbarSegment chip;
    chip.crumb_index = -1;
    chip.x = x;
    chip.label = kEllipsisGlyph;
    chip.end_x = x + ellipsis_w;
    x = chip.end_x;
    layout.segments.push_back(std::move(chip));
  }
  for (int i = first; i < count; i++)
  {
    Winbar::WinbarSegment segment;
    segment.crumb_index = i;
    segment.x = x;
    segment.icon_x = icon_w[(size_t)i] > 0 ? x + 1 : -1;
    segment.label_x = x + 1 + icon_w[(size_t)i];
    segment.end_x = x + widths[(size_t)i];
    segment.label = labels[(size_t)i];
    segment.hovered = (pane_index == winbar_hover_pane && i == winbar_hover_crumb);
    segment.active = winbar_menu_open() && pane_index == winbar_menu_pane && i == winbar_menu_crumb;
    layout.segments.push_back(std::move(segment));
    x = layout.segments.back().end_x;
  }
  return layout;
}

// The whole winbar surface, once per frame and before the panes paint: every
// pane's row arrives together so the painter can tell which rows this frame has
// and close the ones it does not -- the same shape the crumb cascade uses.
// Returns whether the Lua painter took the rows; when it declines (or there is
// no handler) the panes fall back to painting their own rows natively.
bool Editor::emit_winbar_rows()
{
  winbar_lua_rows = false;
  if (!lua_api || !lua_api->has_lua_ui_handler("winbar"))
  {
    return false;
  }
  WinbarView view;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    const SplitPane &pane = panes[(size_t)i];
    if (!pane_has_winbar(pane))
    {
      continue;
    }
    const Winbar::WinbarLayout layout = build_winbar_layout(pane, i);
    if (layout.crumbs.empty())
    {
      continue;
    }
    view.rows.push_back(winbar_row_view(pane, i, buffers[(size_t)pane.buffer_id].filepath, layout));
  }
  winbar_lua_rows = lua_api->emit_winbar(view);
  return winbar_lua_rows;
}

void Editor::render_winbar(const SplitPane &pane, int pane_index)
{
  if (!ui || !pane_has_winbar(pane))
  {
    return;
  }
  // The Lua painter drew every row this frame (emit_winbar_rows, before the
  // panes), so the cells are already covered.
  if (winbar_lua_rows)
  {
    return;
  }
  const Winbar::WinbarLayout layout = build_winbar_layout(pane, pane_index);
  if (layout.crumbs.empty())
  {
    return;
  }
  const int y = layout.y;

  // The row is the pane's own: it stops one cell short on the right, the column
  // the text leaves to the scrollbar.
  ui->fill_rect({pane.x, y, std::max(1, pane.w - 1), 1}, " ", theme.fg_winbar, theme.bg_winbar);

  for (const Winbar::WinbarSegment &segment : layout.segments)
  {
    int fg = theme.fg_winbar_crumb;
    int bg = theme.bg_winbar;
    if (segment.crumb_index < 0)
    {
      ui->draw_text(segment.x, y, segment.label, theme.fg_winbar_separator, bg);
      continue;
    }
    const Winbar::Crumb &crumb = layout.crumbs[(size_t)segment.crumb_index];
    // The tail of the chain is the line's own file and symbol: it is inked with
    // the row's foreground and set bold, everything above it stays quiet.
    if (crumb.current || crumb.kind == "file")
    {
      fg = theme.fg_winbar;
    }
    if (segment.hovered)
    {
      fg = theme.fg_winbar_hover;
      bg = theme.bg_winbar_hover;
    }
    if (segment.active)
    {
      fg = theme.fg_selection;
      bg = theme.bg_selection;
    }
    // The first crumb on the row starts with its padding cell instead of a
    // separator: nothing comes before it.
    ui->draw_text(segment.x,
                  y,
                  segment.crumb_index == layout.first_crumb ? " " : kSeparatorGlyph,
                  theme.fg_winbar_separator,
                  bg);
    if (segment.icon_x >= 0)
    {
      int icon_fg = crumb.icon_fg;
      if (crumb.kind == "folder")
      {
        icon_fg = theme.fg_sidebar_directory;
      }
      else if (crumb.kind == "symbol")
      {
        icon_fg = Winbar::symbol_color(theme, crumb.symbol_kind);
      }
      ui->draw_text(segment.icon_x, y, crumb.icon, icon_fg >= 0 ? icon_fg : fg, bg);
    }
    ui->draw_text(segment.label_x, y, segment.label, fg, bg, crumb.current || crumb.kind == "file");
  }
}

// ---------------------------------------------------------------------------
// The crumb drop-down (a cascade: level 0 is the crumb's siblings, each level
// below it a folder opened from the selected row of the level above)
// ---------------------------------------------------------------------------

void Editor::close_winbar_menu()
{
  if (!winbar_menu_open())
  {
    return;
  }
  winbar_menu_crumb = -1;
  winbar_menu_pane = -1;
  winbar_menu_crumbs.clear();
  winbar_menus.clear();
  needs_redraw = true;
}

// Keeps a level's selected row inside its own window.
void Editor::winbar_menu_clamp(int level)
{
  if (level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  WinbarMenuLevel &menu = winbar_menus[(size_t)level];
  const int rows = std::max(1, menu.h - 2);
  const int count = (int)menu.entries.size();
  menu.selected = std::clamp(menu.selected, 0, std::max(0, count - 1));
  if (menu.selected < menu.scroll)
  {
    menu.scroll = menu.selected;
  }
  if (menu.selected >= menu.scroll + rows)
  {
    menu.scroll = std::max(0, menu.selected - rows + 1);
  }
  menu.scroll = std::clamp(menu.scroll, 0, std::max(0, count - rows));
}

void Editor::open_winbar_menu(int crumb_index, int pane_index)
{
  if (!ui || pane_index < 0 || pane_index >= (int)panes.size())
  {
    return;
  }
  const SplitPane &pane = panes[(size_t)pane_index];
  if (!pane_has_winbar(pane))
  {
    return;
  }
  // Pressing the crumb whose menu is already up puts the whole cascade away.
  if (winbar_menu_open() && winbar_menu_crumb == crumb_index && winbar_menu_pane == pane_index)
  {
    close_winbar_menu();
    return;
  }
  const Winbar::WinbarLayout layout = build_winbar_layout(pane, pane_index);
  if (crumb_index < 0 || crumb_index >= (int)layout.crumbs.size())
  {
    return;
  }
  int segment_x = -1;
  for (const Winbar::WinbarSegment &segment : layout.segments)
  {
    if (segment.crumb_index == crumb_index)
    {
      segment_x = segment.x;
      break;
    }
  }
  if (segment_x < 0)
  {
    return; // the crumb did not fit on the row
  }

  std::vector<Winbar::Entry> entries =
      Winbar::menu_entries(layout.crumbs, crumb_index, winbar_symbols(pane.buffer_id));
  if (entries.empty())
  {
    close_winbar_menu();
    return;
  }

  winbar_menu_crumbs = layout.crumbs;
  winbar_menu_crumb = crumb_index;
  winbar_menu_pane = pane_index;
  winbar_menus.clear();

  const int screen_w = std::max(1, ui->get_render_width());
  const int screen_h = ui->get_height();
  WinbarMenuLevel level;
  level.title = layout.crumbs[(size_t)crumb_index].label;
  level.entries = std::move(entries);
  level.w = menu_width_for(level.entries, screen_w);
  // The panel hangs from the crumb's own column, and stops at the status line.
  const int room = std::max(3, screen_h - (layout.y + 1) - status_height - 1);
  level.h = menu_height_for(level.entries, room);
  level.x = std::clamp(segment_x, 0, std::max(0, screen_w - level.w));
  level.y = std::clamp(layout.y + 1, 0, std::max(0, screen_h - level.h));
  // Open on the row the chain is already on, so the menu says where "here" is.
  for (int i = 0; i < (int)level.entries.size(); i++)
  {
    if (level.entries[(size_t)i].current)
    {
      level.selected = i;
      break;
    }
  }
  winbar_menus.push_back(std::move(level));
  winbar_menu_clamp(0);
  needs_redraw = true;
}

// Opens the folder listing of `level`'s row `entry_index` as the next level,
// placed *beside* the level that offered it -- to the right, or to the left
// when the right edge has no room -- and anchored on the row itself, so the
// two panels read as one path with the row pointing at its children.
void Editor::winbar_menu_open_submenu(int level, int entry_index)
{
  if (!ui || level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  if ((int)winbar_menus.size() >= kMaxMenuLevels)
  {
    return;
  }
  // Copy the parent: pushing the child invalidates the reference.
  const WinbarMenuLevel parent = winbar_menus[(size_t)level];
  if (entry_index < 0 || entry_index >= (int)parent.entries.size())
  {
    return;
  }
  const Winbar::Entry &entry = parent.entries[(size_t)entry_index];
  if (!entry.is_dir || entry.path.empty())
  {
    return;
  }
  std::vector<Winbar::Entry> entries = Winbar::directory_entries(entry.path);
  if (entries.empty())
  {
    return;
  }

  const int screen_w = std::max(1, ui->get_render_width());
  const int screen_h = ui->get_height();
  WinbarMenuLevel child;
  child.title = entry.label;
  child.dir = entry.path;
  child.parent_entry = entry_index;
  child.entries = std::move(entries);
  child.w = menu_width_for(child.entries, screen_w);
  const int row_y = parent.y + 1 + (entry_index - parent.scroll);
  const int room = std::max(3, screen_h - row_y - status_height - 1);
  child.h = menu_height_for(child.entries, room);
  int x = parent.x + parent.w + 1;
  if (x + child.w > screen_w)
  {
    x = parent.x - child.w - 1; // no room on the right: mirror to the left
  }
  child.x = std::clamp(x, 0, std::max(0, screen_w - child.w));
  child.y = std::clamp(row_y, 0, std::max(0, screen_h - child.h));
  winbar_menus.resize((size_t)level + 1);
  winbar_menus.push_back(std::move(child));
  winbar_menu_clamp(level + 1);
}

// The cascade follows the selection: everything deeper than `level` goes (its
// rows no longer answer to the row the pointer is on), and a folder row opens
// its own listing beside it. A plain file row leaves the cascade where it is.
void Editor::winbar_menu_follow(int level)
{
  if (level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  // Read what the row is *before* the resize below: it drops the deeper levels
  // and would leave a reference into the cascade dangling.
  const int selected = winbar_menus[(size_t)level].selected;
  if ((int)winbar_menus.size() > level + 1
      && winbar_menus[(size_t)level + 1].parent_entry == selected)
  {
    return; // the cascade already shows this row's folder
  }
  const bool is_dir = selected >= 0 && selected < (int)winbar_menus[(size_t)level].entries.size()
                      && winbar_menus[(size_t)level].entries[(size_t)selected].is_dir;
  winbar_menus.resize((size_t)level + 1);
  if (is_dir)
  {
    winbar_menu_open_submenu(level, selected);
  }
  needs_redraw = true;
}

void Editor::winbar_menu_hover(int level, int index)
{
  if (level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  if (index < 0 || index >= (int)winbar_menus[(size_t)level].entries.size())
  {
    return;
  }
  if (winbar_menus[(size_t)level].selected != index)
  {
    winbar_menus[(size_t)level].selected = index;
    winbar_menu_clamp(level);
    needs_redraw = true;
  }
  winbar_menu_follow(level);
}

void Editor::winbar_menu_activate(int level, int index)
{
  if (level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  if (index < 0 || index >= (int)winbar_menus[(size_t)level].entries.size())
  {
    return;
  }
  winbar_menus[(size_t)level].selected = index;
  winbar_menu_clamp(level);
  const Winbar::Entry entry = winbar_menus[(size_t)level].entries[(size_t)index];
  if (entry.is_dir)
  {
    // A folder is not a destination: it opens its own listing beside this one.
    winbar_menu_follow(level);
    return;
  }
  const int pane_index = winbar_menu_pane;
  close_winbar_menu();
  if (pane_index < 0 || pane_index >= (int)panes.size())
  {
    return;
  }

  if (entry.kind == "symbol")
  {
    if (pane_index != current_pane)
    {
      activate_pane(pane_index);
    }
    FileBuffer &buf = get_buffer(panes[(size_t)pane_index].buffer_id);
    if (buf.line_count() == 0)
    {
      return;
    }
    buf.cursor.y = std::clamp(entry.line, 0, std::max(0, (int)buf.line_count() - 1));
    buf.cursor.x = std::clamp(entry.col, 0, (int)buf.line(buf.cursor.y).size());
    buf.preferred_x = buf.cursor.x;
    clear_selection();
    reveal_cursor_centered();
    record_jump();
    set_message(entry.label);
    needs_redraw = true;
    return;
  }

  if (!entry.path.empty())
  {
    open_file(entry.path, false);
    focus_state = FOCUS_EDITOR;
    set_message(entry.label);
    needs_redraw = true;
  }
}

// Moves a level's selection (the deepest one for the keyboard, whatever level
// the wheel is over for the mouse) and lets the cascade follow it.
void Editor::winbar_menu_scroll(int level, int delta)
{
  if (!winbar_menu_open() || delta == 0 || level < 0 || level >= (int)winbar_menus.size())
  {
    return;
  }
  const int count = (int)winbar_menus[(size_t)level].entries.size();
  if (count == 0)
  {
    return;
  }
  winbar_menus[(size_t)level].selected =
      (winbar_menus[(size_t)level].selected + delta + count) % count;
  winbar_menu_clamp(level);
  winbar_menu_follow(level);
  needs_redraw = true;
}

void Editor::winbar_menu_move(int delta)
{
  if (!winbar_menu_open())
  {
    return;
  }
  winbar_menu_scroll((int)winbar_menus.size() - 1, delta);
}

// h / Left: one level back out of the cascade (Esc puts the whole thing away,
// and stepping back from the only level closes it).
void Editor::winbar_menu_back()
{
  if (!winbar_menu_open())
  {
    return;
  }
  if ((int)winbar_menus.size() <= 1)
  {
    close_winbar_menu();
    return;
  }
  winbar_menus.pop_back();
  needs_redraw = true;
}

bool Editor::handle_winbar_menu_input(int ch)
{
  if (!winbar_menu_open())
  {
    return false;
  }
  if (ch == 27)
  {
    close_winbar_menu();
    return true;
  }
  const int level = (int)winbar_menus.size() - 1;
  if (ch == 'h' || ch == 'H' || ch == 1011)
  {
    winbar_menu_back();
    return true;
  }
  if (ch == 1008 || ch == 'k' || ch == 'K')
  {
    winbar_menu_move(-1);
    return true;
  }
  if (ch == 1009 || ch == 'j' || ch == 'J')
  {
    winbar_menu_move(1);
    return true;
  }
  if (ch == '\n' || ch == 13 || ch == ' ' || ch == 1010 || ch == 'l' || ch == 'L')
  {
    winbar_menu_activate(level, winbar_menus[(size_t)level].selected);
    return true;
  }
  // Like the context menu: while a menu is up it owns the keyboard, so a stray
  // key cannot edit the buffer behind it.
  return true;
}

// Which pane's winbar row is at y, and which crumb of it is under x (-1 for
// the row's own padding). False when the point is not on any pane's row.
bool Editor::winbar_row_crumb_at(int x, int y, int &pane_index, int &crumb_index)
{
  pane_index = -1;
  crumb_index = -1;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    const SplitPane &pane = panes[(size_t)i];
    if (!pane_has_winbar(pane) || y != pane_winbar_y(pane))
    {
      continue;
    }
    if (x < pane.x || x >= pane.x + std::max(1, pane.w))
    {
      continue;
    }
    pane_index = i;
    const Winbar::WinbarLayout layout = build_winbar_layout(pane, i);
    for (const Winbar::WinbarSegment &segment : layout.segments)
    {
      if (segment.crumb_index < 0 || x < segment.x || x >= segment.end_x)
      {
        continue;
      }
      crumb_index = segment.crumb_index;
      break;
    }
    return true;
  }
  return false;
}

// The level of the cascade a point lands in, deepest first so an overlapping
// child panel wins over the level that opened it. -1 when the point is in none.
int Editor::winbar_menu_level_at(int x, int y) const
{
  for (int i = (int)winbar_menus.size() - 1; i >= 0; i--)
  {
    const WinbarMenuLevel &menu = winbar_menus[(size_t)i];
    if (x >= menu.x && x < menu.x + menu.w && y >= menu.y && y < menu.y + menu.h)
    {
      return i;
    }
  }
  return -1;
}

bool Editor::handle_winbar_mouse(int x, int y, bool is_click, bool is_click_release, bool is_motion)
{
  if (!is_click && !is_motion && !is_click_release)
  {
    return false;
  }

  // The row stays live while its own drop-down is up: it is the menu's anchor,
  // not something the cascade covers. A press on the crumb that opened the menu
  // puts the cascade away, another crumb moves it there, and a press on the
  // row's padding opens nothing (the row is still the pane's, not the text's).
  // A motion here only ever moves the hover band -- the cascade below it is left
  // where it is, so brushing the row does not close what it opened.
  int row_pane = -1;
  int row_crumb = -1;
  if (winbar_row_crumb_at(x, y, row_pane, row_crumb))
  {
    if (is_click || is_click_release)
    {
      if (is_click)
      {
        if (row_crumb >= 0)
        {
          // The row belongs to its pane, so a crumb press is a press on that
          // pane: the menu's jumps then move the pane the pointer is in rather
          // than whichever one happened to hold the keyboard focus.
          activate_pane(row_pane);
          open_winbar_menu(row_crumb, row_pane);
        }
        else
        {
          close_winbar_menu();
        }
      }
      return true;
    }
    const bool moved = row_pane != winbar_hover_pane || row_crumb != winbar_hover_crumb;
    if (moved)
    {
      winbar_hover_pane = row_pane;
      winbar_hover_crumb = row_crumb;
      needs_redraw = true;
    }
    return true;
  }

  // An open cascade owns the pointer: a press picks the row under it or opens
  // the folder one level deeper, a motion moves the selection (and with it the
  // cascade), and a press anywhere else puts the whole thing away. The release
  // that follows a press is swallowed too -- the press already decided, and
  // letting it through would hand the pane a release it never saw a press for.
  if (winbar_menu_open())
  {
    if (is_click_release)
    {
      return true;
    }
    const int level = winbar_menu_level_at(x, y);
    if (level < 0)
    {
      if (is_click)
      {
        close_winbar_menu();
      }
      return true;
    }
    const WinbarMenuLevel &menu = winbar_menus[(size_t)level];
    const int row = y - menu.y - 1;
    const int index = menu.scroll + row;
    if (row >= 0 && row < std::max(1, menu.h - 2) && index >= 0
        && index < (int)menu.entries.size())
    {
      if (is_click)
      {
        winbar_menu_activate(level, index);
      }
      else
      {
        winbar_menu_hover(level, index);
      }
    }
    return true;
  }

  // Off the row and no cascade: the pointer left the row, so the hover band
  // clears and the event belongs to whatever is under it (the panes).
  if (winbar_hover_pane >= 0 || winbar_hover_crumb >= 0)
  {
    winbar_hover_pane = -1;
    winbar_hover_crumb = -1;
    needs_redraw = true;
  }
  return false;
}

void Editor::render_winbar_menu()
{
  if (!ui || !winbar_menu_open())
  {
    return;
  }

  // A registered Lua handler paints every level from this state; the rects stay
  // native so mouse hits keep landing on the rows the handler drew.
  if (lua_api && lua_api->has_lua_ui_handler("winbar_menu"))
  {
    WinbarMenuView view;
    for (const WinbarMenuLevel &menu : winbar_menus)
    {
      WinbarMenuLevelView level;
      level.x = menu.x;
      level.y = menu.y;
      level.w = menu.w;
      level.h = menu.h;
      level.title = menu.title;
      level.selected = menu.selected;
      level.scroll = menu.scroll;
      level.total = (int)menu.entries.size();
      const int rows = std::max(1, menu.h - 2);
      for (int i = 0; i < rows; i++)
      {
        const int index = menu.scroll + i;
        if (index < 0 || index >= (int)menu.entries.size())
        {
          break;
        }
        const Winbar::Entry &entry = menu.entries[(size_t)index];
        WinbarMenuEntryView row;
        row.label = entry.label;
        row.icon = entry.icon;
        row.kind = entry.kind;
        row.is_dir = entry.is_dir;
        row.current = entry.current;
        row.index = index;
        level.entries.push_back(std::move(row));
      }
      view.levels.push_back(std::move(level));
    }
    if (lua_api->emit_winbar_menu(view))
    {
      return;
    }
  }

  // Same panel surface convention as the context menu and the menu bar's
  // drop-down: a raised box in the panel color, the title on the top border.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  for (const WinbarMenuLevel &menu : winbar_menus)
  {
    const int rows = std::max(1, menu.h - 2);
    UIRect rect = {menu.x, menu.y, menu.w, menu.h};
    ui_draw_panel(
        *ui,
        rect,
        {theme.fg_winbar, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
    ui_draw_panel_title(
        *ui, rect, " " + menu.title + " ", theme.fg_winbar_crumb, panel_theme.bg_command);

    const int inner_w = std::max(1, menu.w - 2);
    std::vector<UISelectableRow> out;
    out.reserve((size_t)rows);
    for (int i = 0; i < rows; i++)
    {
      const int index = menu.scroll + i;
      if (index < 0 || index >= (int)menu.entries.size())
      {
        break;
      }
      const Winbar::Entry &entry = menu.entries[(size_t)index];
      // A folder row keeps its last two cells for the chevron, so a long name
      // is trimmed rather than drawn under the glyph.
      const std::string text = entry.is_dir ? ellipsize_middle(entry_text(entry), std::max(1, inner_w - 2))
                                            : entry_text(entry);
      out.push_back({text, index == menu.selected, true});
    }
    ui_draw_selectable_rows(*ui,
                            menu.x + 1,
                            menu.y + 1,
                            inner_w,
                            rows,
                            out,
                            {theme.fg_winbar,
                             panel_theme.bg_command,
                             theme.fg_selection,
                             theme.bg_selection,
                             theme.fg_comment,
                             panel_theme.bg_command});

    // The chevron on the right edge of every folder row: the glyph that says
    // the row opens another panel beside this one.
    for (int i = 0; i < rows; i++)
    {
      const int index = menu.scroll + i;
      if (index < 0 || index >= (int)menu.entries.size())
      {
        break;
      }
      if (!menu.entries[(size_t)index].is_dir)
      {
        continue;
      }
      const bool selected = index == menu.selected;
      ui->draw_text(menu.x + std::max(1, menu.w - 3),
                    menu.y + 1 + i,
                    kChevronGlyph,
                    selected ? theme.fg_selection : theme.fg_winbar_crumb,
                    selected ? theme.bg_selection : panel_theme.bg_command);
    }
  }
}
