#ifndef EDITOR_MODELS_H
#define EDITOR_MODELS_H

#include "types.h"
#include "tools/debugger/client.h"
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct PaneTreeNode {
  bool leaf = true;
  int pane_index = -1;
  int parent = -1;
  int first = -1;
  int second = -1;
  bool vertical = true;
  float ratio = 0.5f;
};

struct CommandPaletteSuggestion {
  std::string insert_text;
  std::string label;
  std::string category;
  std::string detail;
  int score = 0;
};

enum QuickPickKind {
  QUICK_PICK_NONE,
  QUICK_PICK_PROJECT_SEARCH,
  QUICK_PICK_DIAGNOSTICS,
  QUICK_PICK_SYMBOLS,
  QUICK_PICK_PLUGIN
};

struct QuickPickItem {
  std::string label;
  std::string detail;
  std::string preview;
  std::string filepath;
  int line = 0;
  int col = 0;
  int severity = 0;
};

struct SearchMatch {
  int line = 0;
  int col = 0;
  int len = 0;

  bool operator<(const SearchMatch &other) const {
    return std::tie(line, col, len) < std::tie(other.line, other.col, other.len);
  }
};

enum MenuBarAction {
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

struct MenuBarItem {
  std::string label;
  MenuBarAction action = MENU_ACTION_NONE;
  std::string command;
  bool enabled = true;
};

struct MenuBarMenu {
  std::string label;
  std::vector<MenuBarItem> items;
};

struct MenuBarSegment {
  int menu_index = -1;
  int x = 0;
  int end_x = 0;
};

enum RightPanelTab {
  RIGHT_PANEL_DEBUG,
  RIGHT_PANEL_GIT_DIFF,
  RIGHT_PANEL_PLUGIN
};

struct GitDiffPanel {
  bool visible = false;
  bool staged = false;
  std::string path;
  std::vector<std::string> lines;
  int scroll = 0;
};

struct DebuggerSessionState {
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
  std::string last_error;
};

struct TerminalTask {
  std::string name;
  std::string command;
  std::string source_path;
  std::string source_kind;
  std::string cwd;
};

struct TreeSitterInstallJob {
  std::string language;
  int terminal_index = -1;
  bool running = true;
  bool succeeded = false;
  bool failed = false;
  std::string progress;
};

enum MouseSelectionMode {
  MOUSE_SELECT_CHAR,
  MOUSE_SELECT_WORD,
  MOUSE_SELECT_LINE
};

enum ContextMenuSurface {
  CONTEXT_MENU_NONE,
  CONTEXT_MENU_EDITOR,
  CONTEXT_MENU_TAB,
  CONTEXT_MENU_SIDEBAR,
  CONTEXT_MENU_TERMINAL
};

enum ContextMenuAction {
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

struct ContextMenuItem {
  std::string label;
  ContextMenuAction action = CONTEXT_ACTION_NONE;
  bool enabled = true;
};

struct LSPJumpLocation {
  std::string filepath;
  Cursor cursor;
  int scroll_offset = 0;
  int scroll_x = 0;
  bool preview = false;
};

struct ClosedBufferSnapshot {
  std::string filepath;
  std::vector<std::string> lines;
  Cursor cursor;
  Selection selection;
  int scroll_offset;
  int scroll_x;
  bool modified;
  std::vector<FoldRange> collapsed_folds;
};

struct HomeMenuEntry {
  int action;
  int recent_index;
  int recent_workspace_index;
  int x;
  int y;
  int w;
};

enum SidebarView {
  SIDEBAR_VIEW_EXPLORER,
  SIDEBAR_VIEW_GIT
};

struct SidebarRenderRow {
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
};

struct SidebarRenderCache {
  std::vector<SidebarRenderRow> rows;
  std::unordered_map<std::string, int> path_to_row;
  std::string root_label;
  std::string normalized_root;
  bool tree_dirty = true;
  bool diagnostics_dirty = true;
  bool git_dirty = true;
};

struct GitSidebarRow {
  std::string path;
  std::string relative_path;
  std::string status;
};

struct FileTabSegment {
  int buffer_id = -1;
  int tab_index = -1;
  int x = 0;
  int label_x = 0;
  int close_x = 0;
  int end_x = 0;
  std::string label;
  bool active = false;
  bool modified = false;
  bool preview = false;
};

struct FileTabLayout {
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

enum EditorFocus { FOCUS_EDITOR, FOCUS_SIDEBAR };

#endif
