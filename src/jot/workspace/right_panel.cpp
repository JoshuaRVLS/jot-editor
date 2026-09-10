// Right dock tab management (src/jot/workspace/right_panel.cpp).
//
// The right dock behaves like VSCode's sidebar: panels open as tabs
// (git, git diff, symbols, debug, plugin) and the tab strip at the top
// switches between them. Ctrl+Shift+B toggles the dock itself. Commands
// such as :gitpanel add their tab (open_right_panel_tab); closing a tab
// (clicking its ×, or q/Esc while focused) removes it and, when it was the
// active one, activates a neighbor. Closing the last tab hides the dock.
//
// The strip lives on the panel's first interior row (panel_y + 1), so every
// panel renderer draws it through render_right_panel_tab_strip() and the Lua
// side_panel handler renders the same row from view.panel_tabs — the two
// paths stay byte-identical.

#include "editor.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
  // Nerd Fonts glyphs (classic FontAwesome / cod codepoints, present in
  // every Nerd Fonts build).
  constexpr const char *kIconGit = "\uE725";    // cod git-branch
  constexpr const char *kIconDiff = "\uF1C9";   // file-code
  constexpr const char *kIconSymbols = "\uF0CA"; // list-ul
  constexpr const char *kIconDebug = "\uF121";  // bug
  constexpr const char *kIconPlugin = "\uF12E"; // puzzle-piece

  // Label for a dock tab: " icon Name ". The active tab gets a " ×" close
  // suffix (VSCode-style), hit-tested by the strip mouse handler.
  std::string tab_label(RightPanelTab tab, bool active)
  {
    const char *icon = kIconGit;
    const char *name = "Git";
    switch (tab)
    {
    case RIGHT_PANEL_GIT_DIFF:
      icon = kIconDiff;
      name = "Diff";
      break;
    case RIGHT_PANEL_SYMBOLS:
      icon = kIconSymbols;
      name = "Symbols";
      break;
    case RIGHT_PANEL_DEBUG:
      icon = kIconDebug;
      name = "Debug";
      break;
    case RIGHT_PANEL_PLUGIN:
      icon = kIconPlugin;
      name = "Plugin";
      break;
    default:
      break;
    }
    return std::string(" ") + icon + " " + name + (active ? " \u00D7 " : " ");
  }
} // namespace

void Editor::toggle_right_panel()
{
  if (show_right_panel)
  {
    show_right_panel = false;
    focus_state = FOCUS_EDITOR;
  }
  else
  {
    show_right_panel = true;
    if (right_panel_tabs.empty())
    {
      right_panel_tabs.push_back(active_right_panel_tab);
    }
    focus_state = FOCUS_RIGHT_PANEL;
    update_pane_layout();
  }
  needs_redraw = true;
}

void Editor::open_right_panel_tab(RightPanelTab tab)
{
  if (!right_panel_tab_open(tab))
  {
    right_panel_tabs.push_back(tab);
  }
  active_right_panel_tab = tab;
  show_right_panel = true;
  update_pane_layout();
  needs_redraw = true;
}

void Editor::close_right_panel_tab(RightPanelTab tab)
{
  auto it = std::find(right_panel_tabs.begin(), right_panel_tabs.end(), tab);
  if (it != right_panel_tabs.end())
  {
    right_panel_tabs.erase(it);
  }
  if (right_panel_tabs.empty())
  {
    show_right_panel = false;
    focus_state = FOCUS_EDITOR;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
  }
  else if (active_right_panel_tab == tab)
  {
    active_right_panel_tab = right_panel_tabs.back();
  }
  needs_redraw = true;
}

bool Editor::right_panel_tab_open(RightPanelTab tab) const
{
  return std::find(right_panel_tabs.begin(), right_panel_tabs.end(), tab)
         != right_panel_tabs.end();
}

// Populates view.panel_tabs for the Lua side_panel handler; each panel
// renderer calls this while building its SidePanelView.
void Editor::build_right_panel_tab_strip_view(SidePanelView &view) const
{
  view.panel_tabs.clear();
  for (RightPanelTab tab : right_panel_tabs)
  {
    SidePanelTabView tab_view;
    tab_view.label = tab_label(tab, tab == active_right_panel_tab);
    tab_view.active = tab == active_right_panel_tab;
    view.panel_tabs.push_back(std::move(tab_view));
  }
}

// Native fallback for the tab strip row; the Lua side_panel handler renders
// the identical row from view.panel_tabs. Clicking a tab activates it,
// clicking the × on the active tab closes it (see handle_..._mouse).
void Editor::render_right_panel_tab_strip(int panel_x, int panel_y, int panel_w)
{
  if (!ui || right_panel_tabs.empty())
  {
    return;
  }
  const int y = panel_y + 1;
  int x = panel_x + 1;
  for (RightPanelTab tab : right_panel_tabs)
  {
    const bool active = tab == active_right_panel_tab;
    const std::string label = tab_label(tab, active);
    const int w = (int)label.size();
    if (x + w >= panel_x + panel_w)
    {
      break;
    }
    ui->draw_text(x,
                  y,
                  label,
                  active ? theme.fg_terminal_tab_focused : theme.fg_terminal_tab_inactive,
                  active ? theme.bg_terminal_tab_focused : theme.bg_terminal,
                  active);
    x += w;
  }
}

bool Editor::handle_right_panel_tab_strip_mouse(int x, int y, bool is_click)
{
  if (!show_right_panel || !ui)
  {
    return false;
  }
  const int panel_w = effective_right_panel_width();
  if (panel_w <= 0)
  {
    return false;
  }
  const int panel_x = std::max(0, ui->get_render_width() - panel_w);
  const int panel_y = topbar_height();
  if (y != panel_y + 1)
  {
    return false;
  }
  if (x < panel_x || x >= panel_x + panel_w)
  {
    return false;
  }
  // Consume the whole strip row so clicks never fall through to content.
  if (!is_click)
  {
    return true;
  }
  focus_state = FOCUS_RIGHT_PANEL;
  needs_redraw = true;
  int cx = panel_x + 1;
  for (RightPanelTab tab : right_panel_tabs)
  {
    const std::string label = tab_label(tab, tab == active_right_panel_tab);
    const int w = (int)label.size();
    if (x >= cx && x < cx + w)
    {
      if (tab == active_right_panel_tab && x >= cx + w - 3)
      {
        // The " × " suffix (3 bytes) of the active tab closes it. Route
        // through the panel-specific closer so per-panel state (e.g. the
        // git diff's return-to-panel flag) is honored.
        if (tab == RIGHT_PANEL_GIT_DIFF)
        {
          close_git_diff_panel();
        }
        else if (tab == RIGHT_PANEL_SYMBOLS)
        {
          close_outline_panel();
        }
        else if (tab == RIGHT_PANEL_PLUGIN)
        {
          active_plugin_panel.clear();
          close_right_panel_tab(tab);
        }
        else
        {
          close_right_panel_tab(tab);
        }
      }
      else
      {
        open_right_panel_tab(tab);
      }
      return true;
    }
    cx += w;
  }
  return true;
}