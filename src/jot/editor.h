#ifndef EDITOR_H
#define EDITOR_H

// The Editor class, cut into navigable pieces.
//
// What is left here is the class itself: its collaborators, the type aliases
// and constants every part uses, and the includes below, one per region of the
// class body. Each region lives in src/jot/editor/api/ as a *fragment* (no
// include guard and no includes of its own) that is textually included inside
// the class, so the members keep the exact scoping they had when this header
// was 1800 lines -- no declaration moved to another class, nothing was renamed.
//
//   api/paint.h          render entry points, GUI per-pane scratch
//   api/runtime.h        smooth scroll, timers/pollers, LSP lifecycle
//   api/lsp_ui.h         pickers, completion/signature/hover, navigation
//   api/editing.h        syntax/decorations, input dispatch, edit commands
//   api/public_api.h     the public command surface
//   api/explorer.h       file tree, sidebar caches and resize
//   api/buffers.h        clipboard, undo, buffer/file lifecycle, git
//   api/panels.h         minimap, bottom dock, terminal tasks, debugger
//   api/panes.h          folds, pane commands and pane geometry
//   api/commands.h       text commands, themes, setup, message line
//   api/test_hooks.h     the suite's headless hooks (core)
//   api/test_hooks_ui.h  the suite's hooks (surfaces and lifecycle)
//
// Behaviour that owns its state lives in a collaborator instead
// (jot/editor/*_controller.h); the state itself is split under
// src/jot/state/ (see editor_state.h).

#include "autoclose.h"
#include "bracket.h"
#include "editor_state.h"
#include "host_api.h"
#include "jot/editor/discord_controller.h"
#include "jot/editor/search_controller.h"
#include "smooth_scroll.h"
#include "tools/lsp/client.h"
#include <string>
#include <utility>
#include <vector>

class LuaAPI;
class EditorHostAPI;
class HostCoreAPI;
class HostRenderAPI;
class HostIOAPI;
class UIGui;
struct SidePanelView; // defined in jot/lua/api.h (included by renderers)

class Editor : private EditorState
{
  friend class LuaAPI;
  friend class EditorHostAPI;
  friend class HostCoreAPI;
  friend class HostRenderAPI;
  friend class HostIOAPI;
  // The collaborators own their own state and reach the shared editor state
  // through the Editor they were built with (jot/editor/*_controller.h), so
  // each one has to be able to see it.
  friend class DiscordController;
  friend class SearchController;

private:
  // Temporary presentation gate. Keep the underlying feature intact while the
  // compact editor layout is evaluated.
  static constexpr bool kTopBarVisible = false;

  // The activity bar is what turns the primary sidebar into a dock with more
  // than one view. With it on, the rail is drawn and the git view is reachable;
  // with it off the sidebar is a plain explorer.
  bool explorer_only() const
  {
    return !show_activity_bar;
  }

  int topbar_height() const
  {
    return kTopBarVisible ? 1 : 0;
  }

  // The find/replace panel: its query, its flags, its matches, its render and
  // the input it consumes while it is up.
  SearchController search{*this};
  // The Discord Rich Presence session: its IPC client, its idle clock, the
  // content it builds and the `:discord` command.
  DiscordController discord{*this};

  using PaneTreeNode = ::PaneTreeNode;
  using CommandPaletteSuggestion = ::CommandPaletteSuggestion;
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

  static constexpr SidebarView SIDEBAR_VIEW_EXPLORER = ::SIDEBAR_VIEW_EXPLORER;
  static constexpr SidebarView SIDEBAR_VIEW_GIT = ::SIDEBAR_VIEW_GIT;

  static constexpr EditorFocus FOCUS_EDITOR = ::FOCUS_EDITOR;
  static constexpr EditorFocus FOCUS_SIDEBAR = ::FOCUS_SIDEBAR;

#include "jot/editor/api/paint.h"
#include "jot/editor/api/runtime.h"
#include "jot/editor/api/lsp_ui.h"
#include "jot/editor/api/editing.h"

#include "jot/editor/api/public_api.h"

#include "jot/editor/api/explorer.h"
#include "jot/editor/api/buffers.h"
#include "jot/editor/api/panels.h"
#include "jot/editor/api/panes.h"
#include "jot/editor/api/commands.h"
public:
#include "jot/editor/api/test_hooks.h"
#include "jot/editor/api/test_hooks_ui.h"
};

#endif
