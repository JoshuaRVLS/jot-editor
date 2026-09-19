#ifndef JOT_MODEL_PANELS_H
#define JOT_MODEL_PANELS_H

#include "tools/symbols/index.h"
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

#endif
