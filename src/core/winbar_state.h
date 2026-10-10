#ifndef JOT_STATE_WINBAR_STATE_H
#define JOT_STATE_WINBAR_STATE_H

#include "render/winbar_model.h" // Winbar::Crumb, Winbar::Entry, WinbarMode
#include <string>
#include <vector>

// The winbar: the breadcrumb row a pane spends above its text and the drop-down
// a crumb opens.
//
// The model it presents lives in render/winbar_model.h (where crumbs come from and
// what a crumb's menu offers); this is the state around it: the setting, the
// symbol cache the chain is built from, and the open menu.
//
// The menu is a *cascade*, dropbar's model: level 0 is the crumb's own siblings
// -- a folder's children, a file's folder, the symbols sharing a scope -- and
// every level below it is a folder opened from the selected row of the level
// above, whose `parent_entry` keeps pointing at it. Each level carries its own
// rows, selection, scroll and rect, so a deeper panel is hit-tested where it is
// drawn and the level a key acts on is the one it was opened from.
//
// Split out of editor_state.h (the umbrella over src/editor/state/), like the
// other domain groups.
struct WinbarMenuLevel
{
  std::string title;     // the panel's top border: the crumb's or folder's name
  std::string dir;       // the folder this level lists, empty for the crumb's own menu
  int parent_entry = -1; // the row of the level above that opened this one
  std::vector<Winbar::Entry> entries;
  int selected = 0;
  int scroll = 0;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct WinbarState
{
  // `winbar`: off / auto (code files only) / on (every file).
  Winbar::WinbarMode winbar_mode = Winbar::WINBAR_MODE_AUTO;

  // The crumb under the pointer (visual only -- a crumb is opened by a press).
  // The symbol index the chain is cut from is cached on the buffer itself
  // (FileBuffer::symbol_cache), validated by its edit generation.
  int winbar_hover_crumb = -1;
  int winbar_hover_pane = -1;

  // The open drop-down: which crumb opened it, the chain it was cut from, and
  // the cascade itself (level 0 first, each deeper level a folder listing).
  int winbar_menu_crumb = -1;
  int winbar_menu_pane = -1;
  std::vector<Winbar::Crumb> winbar_menu_crumbs;
  std::vector<WinbarMenuLevel> winbar_menus;

  // A menu is open whenever the cascade has a level. A function rather than a
  // mirror flag, so "open" and "has levels" can never disagree.
  bool winbar_menu_open() const
  {
    return !winbar_menus.empty();
  }

  // Whether the Lua painter took every row this frame (emit_winbar_rows, before
  // the panes). The rows are one surface for the whole split, so a pane cannot
  // decide this for itself: when the painter took them the panes paint no rows
  // at all, and when it declined each pane paints its own natively.
  bool winbar_lua_rows = false;
};

#endif
