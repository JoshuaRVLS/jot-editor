#ifndef EDITOR_MODELS_H
#define EDITOR_MODELS_H

#include "tools/debugger/client.h"
#include "tools/symbols/index.h"
#include "types.h"
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct PaneTreeNode
{
  bool leaf = true;
  int pane_index = -1;
  int parent = -1;
  int first = -1;
  int second = -1;
  bool vertical = true;
  float ratio = 0.5f;
};

struct CommandPaletteSuggestion
{
  std::string insert_text;
  std::string label;
  std::string category;
  std::string detail;
  int score = 0;
  // 0-based byte offsets into `label` matched by the query; used to
  // emphasize matched characters while rendering. Empty when not computed.
  std::vector<int> match;
};

enum QuickPickKind
{
  QUICK_PICK_NONE,
  QUICK_PICK_PROJECT_SEARCH,
  QUICK_PICK_DIAGNOSTICS,
  QUICK_PICK_SYMBOLS,
  QUICK_PICK_REFERENCES,
  QUICK_PICK_CODE_ACTIONS,
  QUICK_PICK_PLUGIN,
  QUICK_PICK_FONT,
  QUICK_PICK_JUMPLIST,
  QUICK_PICK_WORKSPACE_SYMBOLS,
  QUICK_PICK_WORKSPACE_DIAGNOSTICS
};

struct QuickPickItem
{
  std::string label;
  std::string detail;
  std::string preview;
  std::string filepath;
  // The value the row stands for, when it is not a location: the font picker
  // puts the family name here so the label can carry the display text.
  std::string value;
  int line = 0;
  int col = 0;
  int severity = 0;
};

// One row of the cell-based settings menu (:settings / Ctrl+,). Each entry
// wraps a config key with its human label, current value, value type and
// edit state. Bool keys toggle on Enter; int/string keys open an inline
// input row. Lua-registered config keys (jot.config.set) appear here too
// as generic string entries, so the menu doubles as a config browser.
struct SettingsEntry
{
  enum class Type
  {
    Bool,
    Int,
    String
  };
  std::string key;    // config key
  std::string label;  // human-readable label
  std::string value;  // current string value (as stored in settings.conf)
  Type type = Type::String;
  // While the row is being edited, its input text and the row's screen
  // position (set by the render pass, used by mouse hit-testing).
  bool editing = false;
  std::string edit_input;
  int row_x = 0;
  int row_y = 0;
  int row_w = 0;
};

struct SearchMatch
{
  int line = 0;
  int col = 0;
  int len = 0;

  bool operator<(const SearchMatch &other) const
  {
    return std::tie(line, col, len) < std::tie(other.line, other.col, other.len);
  }
};

enum MenuBarAction
{
  MENU_ACTION_NONE,
  MENU_ACTION_COMMAND,
  MENU_ACTION_NEW_FILE,
  MENU_ACTION_OPEN_FINDER,
  MENU_ACTION_SAVE,
  MENU_ACTION_SAVE_AS,
  MENU_ACTION_CLOSE_FILE,
  MENU_ACTION_QUIT,
  MENU_ACTION_UNDO,
  MENU_ACTION_REDO,
  MENU_ACTION_CUT,
  MENU_ACTION_COPY,
  MENU_ACTION_PASTE,
  MENU_ACTION_SELECT_ALL,
  MENU_ACTION_SELECT_LINE,
  MENU_ACTION_DUPLICATE_LINE,
  MENU_ACTION_MOVE_LINE_UP,
  MENU_ACTION_MOVE_LINE_DOWN,
  MENU_ACTION_TOGGLE_COMMENT,
  MENU_ACTION_COMMAND_PALETTE,
  MENU_ACTION_TOGGLE_SIDEBAR,
  MENU_ACTION_TOGGLE_MINIMAP,
  MENU_ACTION_THEME,
  MENU_ACTION_HOME,
  MENU_ACTION_TOGGLE_TERMINAL,
  MENU_ACTION_NEW_TERMINAL,
  MENU_ACTION_TERMINAL_ZOOM,
  MENU_ACTION_TASKS,
  MENU_ACTION_RERUN_TASK,
  MENU_ACTION_TOGGLE_DEBUG_PANEL,
  MENU_ACTION_DEBUG_STOP,
  MENU_ACTION_DEBUG_CONTINUE,
  MENU_ACTION_DEBUG_PAUSE,
  MENU_ACTION_DEBUG_STEP_IN,
  MENU_ACTION_DEBUG_STEP_OVER,
  MENU_ACTION_DEBUG_STEP_OUT,
  MENU_ACTION_LSP_DEFINITION,
  MENU_ACTION_LSP_BACK,
  MENU_ACTION_HELP
};

struct MenuBarItem
{
  std::string label;
  MenuBarAction action = MENU_ACTION_NONE;
  std::string command;
  bool enabled = true;
};

struct MenuBarMenu
{
  std::string label;
  std::vector<MenuBarItem> items;
};

struct MenuBarSegment
{
  int menu_index = -1;
  int x = 0;
  int end_x = 0;
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

struct DebuggerSessionState
{
  std::string name;
  std::string adapter;
  std::string program;
  bool running = false;
  bool stopped = false;
  int active_thread_id = 0;
  int active_frame_id = 0;
  bool supports_read_memory = false;
  bool supports_disassemble = false;
  std::vector<DebuggerThread> threads;
  std::vector<DebuggerVariable> variables;
  std::vector<DebuggerMemoryRow> memory_rows;
  std::vector<DebuggerInstruction> instructions;
  std::string output;
  // Lines scrolled up from the end of `output` (0 = pinned to the bottom,
  // i.e. newest output visible). Clamped against the visible rows at render.
  int output_scroll = 0;
  std::string last_error;
};

struct TerminalTask
{
  std::string name;
  std::string command;
  std::string source_path;
  std::string source_kind;
  std::string cwd;
};

struct TreeSitterInstallJob
{
  std::string language;
  // Silent background job (pid >= 0) writes its output to output_path and is
  // polled via read_appended(); terminal_index is the fallback used when a
  // background job could not be started.
  int pid = -1;
  std::string output_path;
  size_t output_offset = 0;
  int terminal_index = -1;
  bool running = true;
  bool succeeded = false;
  bool failed = false;
  std::string progress;
  int verify_attempts = 0;
  std::string install_prefix;
  bool prefix_applied = false;
};

struct LspInstallJob
{
  std::string server;
  bool removing = false;
  // Silent background job (pid >= 0) writes its output to output_path and is
  // polled via read_appended(); terminal_index is the fallback used when a
  // background job could not be started.
  int pid = -1;
  std::string output_path;
  size_t output_offset = 0;
  int terminal_index = -1;
  bool running = true;
  bool succeeded = false;
  bool failed = false;
  std::string progress;
};

enum MouseSelectionMode
{
  MOUSE_SELECT_CHAR,
  MOUSE_SELECT_WORD,
  MOUSE_SELECT_LINE
};

enum ContextMenuSurface
{
  CONTEXT_MENU_NONE,
  CONTEXT_MENU_EDITOR,
  CONTEXT_MENU_TAB,
  CONTEXT_MENU_SIDEBAR,
  CONTEXT_MENU_TERMINAL
};

enum ContextMenuAction
{
  CONTEXT_ACTION_NONE,
  CONTEXT_ACTION_COPY,
  CONTEXT_ACTION_CUT,
  CONTEXT_ACTION_PASTE,
  CONTEXT_ACTION_SAVE_BUFFER,
  CONTEXT_ACTION_CLOSE_BUFFER,
  CONTEXT_ACTION_SIDEBAR_OPEN,
  CONTEXT_ACTION_SIDEBAR_NEW_FILE,
  CONTEXT_ACTION_SIDEBAR_NEW_FOLDER,
  CONTEXT_ACTION_SIDEBAR_RENAME,
  CONTEXT_ACTION_SIDEBAR_REFRESH,
  CONTEXT_ACTION_SIDEBAR_COPY_PATH,
  CONTEXT_ACTION_GIT_STAGE,
  CONTEXT_ACTION_GIT_UNSTAGE,
  CONTEXT_ACTION_GIT_DIFF,
  CONTEXT_ACTION_GIT_DIFF_STAGED,
  CONTEXT_ACTION_GIT_STAGE_ALL,
  CONTEXT_ACTION_GIT_REFRESH,
  CONTEXT_ACTION_TERMINAL_FOCUS,
  CONTEXT_ACTION_TERMINAL_NEW,
  CONTEXT_ACTION_TERMINAL_CLOSE,
  CONTEXT_ACTION_TERMINAL_RESET_SCROLL,
  CONTEXT_ACTION_TOGGLE_FOLD
};

struct ContextMenuItem
{
  std::string label;
  ContextMenuAction action = CONTEXT_ACTION_NONE;
  bool enabled = true;
};

// One place the cursor has been: what the jumplist stores and restores. The
// scroll fields are what make Ctrl+O feel like "back" rather than "reposition":
// the view you left comes back with the cursor.
struct JumpLocation
{
  std::string filepath;
  Cursor cursor;
  int scroll_offset = 0;
  int scroll_x = 0;
  bool preview = false;
};

struct ClosedBufferSnapshot
{
  std::string filepath;
  std::vector<std::string> lines;
  Cursor cursor;
  Selection selection;
  int scroll_offset;
  int scroll_x;
  bool modified;
  std::vector<FoldRange> collapsed_folds;
};

struct HomeMenuEntry
{
  int action;
  int recent_index;
  int recent_workspace_index;
  int x;
  int y;
  int w;
};

enum SidebarView
{
  SIDEBAR_VIEW_EXPLORER,
  SIDEBAR_VIEW_GIT
};

struct SidebarRenderRow
{
  std::string path;
  std::string normalized_path;
  std::string name;
  std::string label;
  std::string footer_label;
  bool is_dir = false;
  bool expanded = false;
  int depth = 0;
  int diagnostic_severity = 0;
  std::string git_status;
  // Per-language icon for files (empty for directories). Painted ahead of
  // the label in its brand color, like the status line.
  std::string icon;
  int icon_fg = -1; // -1 = use the row foreground
  // Tree indent guides ("│ ", "├─ ", "└─ " connectors; directories end with
  // their expander chevron). Empty for flat views.
  std::string guide;
  int guide_cells = 0; // cell width of `guide`
};

struct SidebarRenderCache
{
  std::vector<SidebarRenderRow> rows;
  std::unordered_map<std::string, int> path_to_row;
  std::string root_label;
  std::string normalized_root;
  bool tree_dirty = true;
  bool diagnostics_dirty = true;
  bool git_dirty = true;
};

struct GitSidebarRow
{
  std::string path;
  std::string relative_path;
  std::string status;
};

struct FileTabSegment
{
  int buffer_id = -1;
  int tab_index = -1;
  int x = 0;
  int label_x = 0;
  int close_x = 0;
  int end_x = 0;
  std::string label;
  // Per-language file glyph painted ahead of the label in its brand color
  // (shared with the status line / explorer); empty for directories and
  // unnamed buffers, -1 color = use the tab foreground.
  std::string icon;
  int icon_fg = -1;
  bool active = false;
  bool modified = false;
  bool preview = false;
  std::string git_status;
};

struct FileTabLayout
{
  int x = 0;
  int y = 0;
  int w = 0;
  std::vector<FileTabSegment> segments;
  std::string scroll_left_label;
  std::string overflow_label;
  int overflow_x = 0;
  int scroll_left_x = -1;
  int scroll_left_end_x = -1;
  int scroll_right_x = -1;
  int scroll_right_end_x = -1;
  int hidden_before = 0;
  int hidden_after = 0;
  int hidden_count = 0;
};

enum EditorFocus
{
  FOCUS_EDITOR,
  FOCUS_SIDEBAR,
  FOCUS_RIGHT_PANEL
};

#endif
