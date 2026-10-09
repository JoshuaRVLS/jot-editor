#ifndef JOT_MODEL_PANELS_H
#define JOT_MODEL_PANELS_H

#include "syntax/index.h"
enum PanelType
{
  PANEL_EDITOR,
  PANEL_MINIMAP,
  PANEL_SEARCH,
  PANEL_COMMAND_PALETTE,
  PANEL_TELESCOPE
};

enum RightPanelTab
{
  RIGHT_PANEL_DEBUG,
  RIGHT_PANEL_GIT_DIFF,
  RIGHT_PANEL_GIT,
  RIGHT_PANEL_SYMBOLS,
  RIGHT_PANEL_PLUGIN
};

struct GitDiffPanel
{
  bool visible = false;
  bool staged = false;
  std::string path;
  // Set when the diff is one commit's version of the file (a timeline row):
  // the header names the revision instead of staged/unstaged.
  std::string revision;
  std::vector<std::string> lines;
  int scroll = 0;
};

// Persistent per-buffer symbol outline shown in the right panel.
struct OutlinePanelState
{
  bool dirty = true;             // buffer content changed since the last rebuild
  int buffer = -1;               // buffer index the symbol list was built for
  long long last_rebuild_ms = 0; // throttles rebuilds while typing
  std::vector<SymbolMatch> symbols;
  int selected = 0;
  int scroll = 0;
  // The tree view: which subtree is open (a symbol may have children -- a
  // namespace's members, a class's fields and methods -- and a chevron shows
  // when it does), plus the visible-row order that selection, scrolling and
  // hit-testing all read so they agree on what a row means.
  std::vector<char> expanded;
  std::vector<int> visible;
  // Visible-row index under the pointer in the dock's outline (-1 = none):
  // the hover highlight, and the row a click lands on.
  int hover_row = -1;
};

enum EditorFocus
{
  FOCUS_EDITOR,
  FOCUS_SIDEBAR,
  FOCUS_RIGHT_PANEL,
  FOCUS_BOTTOM_PANEL
};

// Views the bottom panel hosts. They share the panel's geometry and its view
// tabs; only the body differs, so a dock that used to be terminal-only now
// switches between the shell and the diagnostics list.
enum BottomPanelView
{
  BOTTOM_PANEL_TERMINAL,
  BOTTOM_PANEL_PROBLEMS
};

class IntegratedTerminal;

// A rectangle in screen cells, for surfaces whose geometry is not a pane or a
// dock: the floating terminal's box. Same shape as the editor's PaneArea, kept
// here so a class-body declaration can name it without the UI headers.
struct TerminalBox
{
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

// One place a terminal is on screen: the content rows of the bottom dock or the
// floating terminal's box. The two share the row rendering, the scroll window
// and the mouse selection, so those take a view rather than each hardcoding a
// geometry.
struct TerminalView
{
  IntegratedTerminal *term = nullptr;
  int x = 0; // first content cell
  int y = 0;
  int w = 0;
  int h = 0;
  bool floating = false;
};

#endif
