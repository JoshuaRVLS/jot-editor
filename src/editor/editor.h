#ifndef EDITOR_H
#define EDITOR_H

// The Editor class, cut into navigable pieces.
//
// What is left here is the class itself: its collaborators, the type aliases
// and constants every part uses, and the includes below, one per region of the
// class body. Each region lives in src/editor/editor/api/ as a *fragment* (no
// include guard and no includes of its own) that is textually included inside
// the class, so the members keep the exact scoping they had when this header
// was 1800 lines -- no declaration moved to another class, nothing was renamed.
//
//   api/paint.h          render entry points, GUI per-pane scratch
//   api/runtime.h        smooth scroll, timers/pollers, LSP lifecycle
//   api/lsp_ui.h         pickers, completion/signature/hover, navigation
//   api/cpp.h            the C++ definition checks (`:cppcheck`)
//   api/editing.h        syntax/decorations, input dispatch, edit commands
//   api/public_api.h     the public command surface
//   api/explorer.h       file tree, sidebar caches and resize
//   api/buffers.h        clipboard, undo, buffer/file lifecycle, git
//   api/panels.h         minimap, bottom dock, terminal tasks, debugger
//   api/panes.h          folds, pane commands and pane geometry
//   api/commands.h       text commands, themes, setup, message line
//   api/bible.h          the Bible reader (`:bible`)
//   api/coding_time.h    the coding-time chip: WakaTime and the local store
//   api/test_hooks.h     the suite's headless hooks (core)
//   api/test_hooks_ui.h  the suite's hooks (surfaces and lifecycle)
//
// Behaviour that owns its state lives in a collaborator instead
// (editor/*_controller.h, editor/rest_client.h); the state itself is split
// under src/editor/state/ (see editor_state.h).

#include "autoclose.h"
#include "bracket.h"
#include "editor/discord_controller.h"
#include "editor/rest_client.h"
#include "editor/search_controller.h"
#include "editor_state.h"
#include "host_api.h"
#include "lsp/client.h"
#include "smooth_scroll.h"
#include "syntax/language.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class LuaAPI;
class EditorHostAPI;
class HostCoreAPI;
class HostRenderAPI;
class HostIOAPI;
class UIGui;
struct SidePanelView; // defined in lua/api.h (included by renderers)
struct HomeView;      // the home screen's rows, same header as SidePanelView

class Editor : private EditorState
{
  friend class LuaAPI;
  friend class EditorHostAPI;
  friend class HostCoreAPI;
  friend class HostRenderAPI;
  friend class HostIOAPI;
  // The collaborators own their own state and reach the shared editor state
  // through the Editor they were built with (editor/*_controller.h), so each
  // one has to be able to see it.
  friend class DiscordController;
  friend class RestClient;
  friend class SearchController;

private:
  // Temporary presentation gate. Keep the underlying feature intact while the
  // compact editor layout is evaluated. This is only the native *menu bar*
  // (File/Edit/... and its drop-downs); the row it used now belongs to the tab
  // strip, and the menus are retired until something asks for them back.
  static constexpr bool kTopBarVisible = false;

  // The activity bar is what turns the primary sidebar into a dock with more
  // than one view. With it on, the rail is drawn and the git view is reachable;
  // with it off the sidebar is a plain explorer.
  bool explorer_only() const
  {
    return !show_activity_bar;
  }

  // Rows spent above the panes: the retired menu bar (0) plus the tab strip.
  int topbar_height() const
  {
    return (kTopBarVisible ? 1 : 0) + (tabline_shown() ? 1 : 0);
  }

  // The first row the full-height side columns (the explorer and the right
  // dock) start on. Only the retired menu bar costs them a row: the workspace
  // strip spans the pane area between the columns, so their top row is theirs
  // even while the strip is up (the panes pay the strip through
  // topbar_height, the columns do not).
  int side_column_top() const
  {
    return kTopBarVisible ? 1 : 0;
  }

  // Whether the workspace strip owns row 0 right now: it can be turned off, and
  // `tabline_auto_hide` keeps it out of the way with only a few tabs open. The
  // home screen owns the whole frame, and the strip is workspace chrome: it is
  // not part of the launcher, so the row, its paint and its hit tests all stay
  // with the workspace until the screen closes.
  bool tabline_shown() const
  {
    return !show_home_menu && tabline_visible
           && (int)tab_order.indices(buffers).size() > tabline_auto_hide;
  }

  // The strip's first screen row (below the retired menu bar, when it ever
  // returns).
  int tabline_y() const
  {
    return kTopBarVisible ? 1 : 0;
  }

  // Whether the winbar is on at all (`winbar = off` turns the row off for
  // every pane).
  bool winbar_shown() const
  {
    return winbar_mode != Winbar::WINBAR_MODE_OFF;
  }

  // The row count a pane pays when it shows the winbar. Set once per layout
  // pass by update_pane_layout, like tab_height, because the panes' geometry is
  // resolved there and every later question goes through pane_content_top.
  int winbar_row_height() const
  {
    return winbar_shown() ? 1 : 0;
  }

  // Whether this pane's buffer pays a breadcrumb row. Only the file itself can
  // answer: `auto` keeps the row for code (a breadcrumb of a .png is noise),
  // `on` shows it for every named file, and a buffer with no name -- an untitled
  // scratch buffer, a lazy placeholder -- never pays one.
  bool pane_has_winbar(const SplitPane &pane) const
  {
    if (!winbar_shown() || winbar_height <= 0)
    {
      return false;
    }
    if (pane.buffer_id < 0 || pane.buffer_id >= (int)buffers.size())
    {
      return false;
    }
    const FileBuffer &buf = buffers[(size_t)pane.buffer_id];
    if (buf.is_lazy() || buf.filepath.empty())
    {
      return false;
    }
    if (winbar_mode == Winbar::WINBAR_MODE_ON)
    {
      return true;
    }
    return Language::is_code_file(buf.filepath);
  }

  // The rows this pane actually spends on its winbar (0 when it shows none),
  // which is what the Lua viewport and the tests want -- `winbar_height` is the
  // row's height for a pane that has one, not this pane's answer.
  int pane_winbar_height(const SplitPane &pane) const
  {
    return pane_has_winbar(pane) ? winbar_height : 0;
  }

  // The screen row this pane's winbar is drawn on. The row sits directly above
  // the pane's text, below whatever chrome (the workspace strip) is not the
  // pane's own.
  int pane_winbar_y(const SplitPane &pane) const
  {
    return pane.y + pane_header_height(pane) - pane_winbar_height(pane);
  }

  // Whether this pane draws its own tab strip (a VSCode editor group).
  // `pane_tabs=auto` spends the row only where the workspace strip cannot
  // speak for the pane: while the pane area is split (the workspace strip acts
  // on the focused pane alone, so each group needs its own list), or when the
  // workspace strip is turned off (`tabline = false`) and the pane's list is
  // the only one left. A lone pane with the workspace strip on top paints no
  // second row, because that would be the same list twice. `always` shows the
  // row for every pane with a buffer, `off` for none.
  bool pane_shows_tab_strip(const SplitPane &pane) const
  {
    if (pane_tabs_mode == PANE_TABS_OFF || pane.tab_buffer_ids.empty())
    {
      return false;
    }
    if (pane_tabs_mode == PANE_TABS_ALWAYS)
    {
      return true;
    }
    return pane.tab_buffer_ids.size() > 1 && (panes.size() > 1 || !tabline_visible);
  }

  // The row this pane spends on its own strip (0 when it shows none).
  int pane_tab_strip_height(const SplitPane &pane) const
  {
    return pane_shows_tab_strip(pane) ? 1 : 0;
  }

  // The chrome rows a pane spends above its text: its own tab strip when it has
  // one, plus the winbar when this pane has one (the workspace strip is global
  // and sits above the panes, not inside them). update_pane_layout stores the
  // answer on the pane; everything else reads it back through pane_content_top /
  // pane_viewport_h (editor/model/panes.h), so the rule lives in one place.
  int pane_header_height(const SplitPane &pane) const
  {
    return pane_tab_strip_height(pane) + (pane_has_winbar(pane) ? winbar_height : 0);
  }

  // The screen row a pane's chrome starts at, for the few things that anchor to
  // the top of the pane area rather than to one pane (the find panel's overlay
  // rows, the caret it places).
  int pane_area_top() const
  {
    if (panes.empty())
    {
      return topbar_height();
    }
    const SplitPane &pane = panes[(size_t)std::clamp(current_pane, 0, (int)panes.size() - 1)];
    return topbar_height() + pane_header_height(pane);
  }

  // The find/replace panel: its query, its flags, its matches, its render and
  // the input it consumes while it is up.
  SearchController search{*this};
  // The Discord Rich Presence session: its IPC client, its idle clock, the
  // content it builds and the `:discord` command.
  DiscordController discord{*this};
  // The HTTP client: the `:rest` runs and the `[Response]` tab they land in.
  RestClient rest{*this};

  using PaneTreeNode = ::PaneTreeNode;
  using CommandPaletteSuggestion = ::CommandPaletteSuggestion;
  using CommandPaletteLayout = ::CommandPaletteLayout;
  using QuickPickKind = ::QuickPickKind;
  using QuickPickItem = ::QuickPickItem;
  using SearchMatch = ::SearchMatch;
  using MenuBarAction = ::MenuBarAction;
  using MenuBarItem = ::MenuBarItem;
  using MenuBarMenu = ::MenuBarMenu;
  using MenuBarSegment = ::MenuBarSegment;
  using RightPanelTab = ::RightPanelTab;
  using GitDiffPanel = ::GitDiffPanel;
  using DebuggerSessionState = ::DebuggerSessionState;
  using TerminalTask = ::TerminalTask;
  using TreeSitterInstallJob = ::TreeSitterInstallJob;
  using MouseSelectionMode = ::MouseSelectionMode;
  using ContextMenuSurface = ::ContextMenuSurface;
  using ContextMenuAction = ::ContextMenuAction;
  using ContextMenuItem = ::ContextMenuItem;
  using JumpLocation = ::JumpLocation;
  using ClosedBufferSnapshot = ::ClosedBufferSnapshot;
  using HomeMenuEntry = ::HomeMenuEntry;
  using SidebarView = ::SidebarView;
  using SidebarRenderRow = ::SidebarRenderRow;
  using SidebarRenderCache = ::SidebarRenderCache;
  using GitSidebarRow = ::GitSidebarRow;
  using FileTabSegment = ::FileTabSegment;
  using FileTabLayout = ::FileTabLayout;
  using EditorFocus = ::EditorFocus;
  using BottomPanelView = ::BottomPanelView;

  static constexpr QuickPickKind QUICK_PICK_NONE = ::QUICK_PICK_NONE;
  static constexpr QuickPickKind QUICK_PICK_PROJECT_SEARCH = ::QUICK_PICK_PROJECT_SEARCH;
  static constexpr QuickPickKind QUICK_PICK_DIAGNOSTICS = ::QUICK_PICK_DIAGNOSTICS;
  static constexpr QuickPickKind QUICK_PICK_SYMBOLS = ::QUICK_PICK_SYMBOLS;
  static constexpr QuickPickKind QUICK_PICK_PLUGIN = ::QUICK_PICK_PLUGIN;
  static constexpr QuickPickKind QUICK_PICK_FONT = ::QUICK_PICK_FONT;
  static constexpr QuickPickKind QUICK_PICK_JUMPLIST = ::QUICK_PICK_JUMPLIST;
  static constexpr QuickPickKind QUICK_PICK_WORKSPACE_SYMBOLS = ::QUICK_PICK_WORKSPACE_SYMBOLS;
  static constexpr QuickPickKind QUICK_PICK_WORKSPACE_DIAGNOSTICS =
      ::QUICK_PICK_WORKSPACE_DIAGNOSTICS;

  static constexpr MenuBarAction MENU_ACTION_NONE = ::MENU_ACTION_NONE;
  static constexpr MenuBarAction MENU_ACTION_COMMAND = ::MENU_ACTION_COMMAND;
  static constexpr MenuBarAction MENU_ACTION_NEW_FILE = ::MENU_ACTION_NEW_FILE;
  static constexpr MenuBarAction MENU_ACTION_OPEN_FINDER = ::MENU_ACTION_OPEN_FINDER;
  static constexpr MenuBarAction MENU_ACTION_SAVE = ::MENU_ACTION_SAVE;
  static constexpr MenuBarAction MENU_ACTION_SAVE_AS = ::MENU_ACTION_SAVE_AS;
  static constexpr MenuBarAction MENU_ACTION_CLOSE_FILE = ::MENU_ACTION_CLOSE_FILE;
  static constexpr MenuBarAction MENU_ACTION_QUIT = ::MENU_ACTION_QUIT;
  static constexpr MenuBarAction MENU_ACTION_UNDO = ::MENU_ACTION_UNDO;
  static constexpr MenuBarAction MENU_ACTION_REDO = ::MENU_ACTION_REDO;
  static constexpr MenuBarAction MENU_ACTION_CUT = ::MENU_ACTION_CUT;
  static constexpr MenuBarAction MENU_ACTION_COPY = ::MENU_ACTION_COPY;
  static constexpr MenuBarAction MENU_ACTION_PASTE = ::MENU_ACTION_PASTE;
  static constexpr MenuBarAction MENU_ACTION_SELECT_ALL = ::MENU_ACTION_SELECT_ALL;
  static constexpr MenuBarAction MENU_ACTION_SELECT_LINE = ::MENU_ACTION_SELECT_LINE;
  static constexpr MenuBarAction MENU_ACTION_DUPLICATE_LINE = ::MENU_ACTION_DUPLICATE_LINE;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_UP = ::MENU_ACTION_MOVE_LINE_UP;
  static constexpr MenuBarAction MENU_ACTION_MOVE_LINE_DOWN = ::MENU_ACTION_MOVE_LINE_DOWN;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_COMMENT = ::MENU_ACTION_TOGGLE_COMMENT;
  static constexpr MenuBarAction MENU_ACTION_COMMAND_PALETTE = ::MENU_ACTION_COMMAND_PALETTE;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_SIDEBAR = ::MENU_ACTION_TOGGLE_SIDEBAR;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_MINIMAP = ::MENU_ACTION_TOGGLE_MINIMAP;
  static constexpr MenuBarAction MENU_ACTION_THEME = ::MENU_ACTION_THEME;
  static constexpr MenuBarAction MENU_ACTION_HOME = ::MENU_ACTION_HOME;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_TERMINAL = ::MENU_ACTION_TOGGLE_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_NEW_TERMINAL = ::MENU_ACTION_NEW_TERMINAL;
  static constexpr MenuBarAction MENU_ACTION_TERMINAL_ZOOM = ::MENU_ACTION_TERMINAL_ZOOM;
  static constexpr MenuBarAction MENU_ACTION_TASKS = ::MENU_ACTION_TASKS;
  static constexpr MenuBarAction MENU_ACTION_RERUN_TASK = ::MENU_ACTION_RERUN_TASK;
  static constexpr MenuBarAction MENU_ACTION_TOGGLE_DEBUG_PANEL = ::MENU_ACTION_TOGGLE_DEBUG_PANEL;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STOP = ::MENU_ACTION_DEBUG_STOP;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_CONTINUE = ::MENU_ACTION_DEBUG_CONTINUE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_PAUSE = ::MENU_ACTION_DEBUG_PAUSE;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_IN = ::MENU_ACTION_DEBUG_STEP_IN;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OVER = ::MENU_ACTION_DEBUG_STEP_OVER;
  static constexpr MenuBarAction MENU_ACTION_DEBUG_STEP_OUT = ::MENU_ACTION_DEBUG_STEP_OUT;
  static constexpr MenuBarAction MENU_ACTION_LSP_DEFINITION = ::MENU_ACTION_LSP_DEFINITION;
  static constexpr MenuBarAction MENU_ACTION_LSP_BACK = ::MENU_ACTION_LSP_BACK;
  static constexpr MenuBarAction MENU_ACTION_HELP = ::MENU_ACTION_HELP;

  static constexpr RightPanelTab RIGHT_PANEL_DEBUG = ::RIGHT_PANEL_DEBUG;
  static constexpr RightPanelTab RIGHT_PANEL_GIT_DIFF = ::RIGHT_PANEL_GIT_DIFF;
  static constexpr RightPanelTab RIGHT_PANEL_SYMBOLS = ::RIGHT_PANEL_SYMBOLS;
  static constexpr RightPanelTab RIGHT_PANEL_PLUGIN = ::RIGHT_PANEL_PLUGIN;

  static constexpr MouseSelectionMode MOUSE_SELECT_CHAR = ::MOUSE_SELECT_CHAR;
  static constexpr MouseSelectionMode MOUSE_SELECT_WORD = ::MOUSE_SELECT_WORD;
  static constexpr MouseSelectionMode MOUSE_SELECT_LINE = ::MOUSE_SELECT_LINE;

  static constexpr ContextMenuSurface CONTEXT_MENU_NONE = ::CONTEXT_MENU_NONE;
  static constexpr ContextMenuSurface CONTEXT_MENU_EDITOR = ::CONTEXT_MENU_EDITOR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TAB = ::CONTEXT_MENU_TAB;
  static constexpr ContextMenuSurface CONTEXT_MENU_SIDEBAR = ::CONTEXT_MENU_SIDEBAR;
  static constexpr ContextMenuSurface CONTEXT_MENU_TERMINAL = ::CONTEXT_MENU_TERMINAL;

  static constexpr ContextMenuAction CONTEXT_ACTION_NONE = ::CONTEXT_ACTION_NONE;
  static constexpr ContextMenuAction CONTEXT_ACTION_COPY = ::CONTEXT_ACTION_COPY;
  static constexpr ContextMenuAction CONTEXT_ACTION_CUT = ::CONTEXT_ACTION_CUT;
  static constexpr ContextMenuAction CONTEXT_ACTION_PASTE = ::CONTEXT_ACTION_PASTE;
  static constexpr ContextMenuAction CONTEXT_ACTION_SAVE_BUFFER = ::CONTEXT_ACTION_SAVE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_CLOSE_BUFFER = ::CONTEXT_ACTION_CLOSE_BUFFER;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_OPEN = ::CONTEXT_ACTION_SIDEBAR_OPEN;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_NEW_FILE =
      ::CONTEXT_ACTION_SIDEBAR_NEW_FILE;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_NEW_FOLDER =
      ::CONTEXT_ACTION_SIDEBAR_NEW_FOLDER;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_RENAME =
      ::CONTEXT_ACTION_SIDEBAR_RENAME;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_REFRESH =
      ::CONTEXT_ACTION_SIDEBAR_REFRESH;
  static constexpr ContextMenuAction CONTEXT_ACTION_SIDEBAR_COPY_PATH =
      ::CONTEXT_ACTION_SIDEBAR_COPY_PATH;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE = ::CONTEXT_ACTION_GIT_STAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_UNSTAGE = ::CONTEXT_ACTION_GIT_UNSTAGE;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF = ::CONTEXT_ACTION_GIT_DIFF;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_DIFF_STAGED =
      ::CONTEXT_ACTION_GIT_DIFF_STAGED;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_STAGE_ALL = ::CONTEXT_ACTION_GIT_STAGE_ALL;
  static constexpr ContextMenuAction CONTEXT_ACTION_GIT_REFRESH = ::CONTEXT_ACTION_GIT_REFRESH;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_FOCUS =
      ::CONTEXT_ACTION_TERMINAL_FOCUS;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_NEW = ::CONTEXT_ACTION_TERMINAL_NEW;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_CLOSE =
      ::CONTEXT_ACTION_TERMINAL_CLOSE;
  static constexpr ContextMenuAction CONTEXT_ACTION_TERMINAL_RESET_SCROLL =
      ::CONTEXT_ACTION_TERMINAL_RESET_SCROLL;
  static constexpr ContextMenuAction CONTEXT_ACTION_TOGGLE_FOLD = ::CONTEXT_ACTION_TOGGLE_FOLD;
  static constexpr ContextMenuAction CONTEXT_ACTION_PIN_TAB = ::CONTEXT_ACTION_PIN_TAB;
  static constexpr ContextMenuAction CONTEXT_ACTION_UNPIN_TAB = ::CONTEXT_ACTION_UNPIN_TAB;
  static constexpr ContextMenuAction CONTEXT_ACTION_CLOSE_OTHER_TABS =
      ::CONTEXT_ACTION_CLOSE_OTHER_TABS;

  static constexpr SidebarView SIDEBAR_VIEW_EXPLORER = ::SIDEBAR_VIEW_EXPLORER;
  static constexpr SidebarView SIDEBAR_VIEW_GIT = ::SIDEBAR_VIEW_GIT;

  static constexpr EditorFocus FOCUS_EDITOR = ::FOCUS_EDITOR;
  static constexpr EditorFocus FOCUS_SIDEBAR = ::FOCUS_SIDEBAR;

#include "editor/api/cpp.h"
#include "editor/api/editing.h"
#include "editor/api/lsp_ui.h"
#include "editor/api/paint.h"
#include "editor/api/runtime.h"

#include "editor/api/public_api.h"

#include "editor/api/bible.h"
#include "editor/api/buffers.h"
#include "editor/api/coding_time.h"
#include "editor/api/commands.h"
#include "editor/api/explorer.h"
#include "editor/api/panels.h"
#include "editor/api/panes.h"
public:
#include "editor/api/test_hooks.h"
#include "editor/api/test_hooks_ui.h"
};

#endif
