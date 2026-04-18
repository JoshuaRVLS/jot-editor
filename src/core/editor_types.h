#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include "editor_features.h"
#include <cstddef>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum PanelType {
  PANEL_EDITOR,
  PANEL_MINIMAP,
  PANEL_SEARCH,
  PANEL_COMMAND_PALETTE,
  PANEL_TELESCOPE
};

enum EditorMode { MODE_NORMAL, MODE_INSERT, MODE_VISUAL };

struct Theme {
  int fg_default = 7;
  int bg_default = 0;
  int fg_keyword = 6;
  int bg_keyword = 0;
  int fg_string = 2;
  int bg_string = 0;
  int fg_comment = 8; // Grey for comments
  int bg_comment = 0;
  int fg_number = 5;
  int bg_number = 0;
  int fg_function = 3;
  int bg_function = 0;
  int fg_type = 6;
  int bg_type = 0;
  int fg_panel_border = 8; // Grey border
  int bg_panel_border = 0; // Black background (cleaner)
  int fg_selection = 0;
  int bg_selection = 6;
  int fg_line_num = 8; // Grey line numbers
  int bg_line_num = 0;
  int fg_cursor = 0;
  int bg_cursor = 7;
  int fg_status = 7;
  int bg_status = 0; // Clean status bar
  int fg_command = 7;
  int bg_command = 0;
  int fg_minimap = 8;
  int bg_minimap = 0;
  int fg_image_border = 7;
  int bg_image_border = 0;
  int fg_bracket1 = 1;
  int fg_bracket2 = 2;
  int fg_bracket3 = 3;
  int fg_bracket4 = 4;
  int fg_bracket5 = 5;
  int fg_bracket6 = 6;
  int fg_bracket_match = 3;
  int bg_bracket_match = 0;
  int fg_telescope = 7;
  int bg_telescope = 0;
  int fg_telescope_selected = 0;
  int bg_telescope_selected = 6;
  int fg_telescope_preview = 7;
  int bg_telescope_preview = 0;
};

struct Cursor {
  int x, y;
  bool operator==(const Cursor &other) const {
    return x == other.x && y == other.y;
  }
};

struct Selection {
  Cursor start;
  Cursor end;
  bool active;
};

struct State {
  std::vector<std::string> lines;
  Cursor cursor;
  Selection selection;
};

struct SyntaxLineCache {
  bool valid = false;
  std::size_t line_hash = 0;
  std::size_t line_length = 0;
  std::vector<std::pair<int, int>> colors;
};

struct FileBuffer {
  std::vector<std::string> lines;
  Cursor cursor;
  int preferred_x; // desired column for vertical movement
  Selection selection;
  int scroll_offset;
  int scroll_x;
  std::string filepath;
  bool modified;
  std::stack<State> undo_stack;
  std::stack<State> redo_stack;
  std::set<int> bookmarks;
  std::vector<Diagnostic> diagnostics;
  std::string syntax_cache_extension;
  std::size_t syntax_cache_line_count = 0;
  std::unordered_map<int, SyntaxLineCache> syntax_cache;
};

struct Popup {
  bool visible;
  std::string text;
  int x, y;
  int w, h;
};

struct FileNode {
  std::string name;
  std::string path;
  bool is_dir;
  bool expanded;
  int depth;
  std::vector<FileNode> children;
};

struct SplitPane {
  int x, y, w, h;
  int buffer_id;
  bool active;
};

enum PaneLayoutMode {
  PANE_LAYOUT_SINGLE,
  PANE_LAYOUT_VERTICAL,
  PANE_LAYOUT_HORIZONTAL
};

struct SyntaxRule {
  std::regex pattern;
  int color;
};

#endif
