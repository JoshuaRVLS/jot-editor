#ifndef EDITOR_H
#define EDITOR_H

#include "autoclose.h"
#include "bracket.h"
#include "config.h"
#include "editor_features.h"
#include "imageviewer.h"
#include "telescope.h"
#include "terminal.h"
#include "ui.h"
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <vector>
// #include "python_api.h"

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

/*
struct Diagnostic {
  int line;
  int col;
  int end_line;
  int end_col;
  std::string message;
  int severity;
};
*/
// Moved to features.h // 1=Error, 2=Warning, 3=Info, 4=Hint

struct FileBuffer {
  std::vector<std::string> lines;
  Cursor cursor;
  Selection selection;
  int scroll_offset;
  int scroll_x;
  std::string filepath;
  bool modified;
  std::stack<State> undo_stack;
  std::stack<State> redo_stack;
  std::set<int> bookmarks;
  std::vector<Diagnostic> diagnostics; // New
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

struct SyntaxRule {
  std::regex pattern;
  int color;
};

class SyntaxHighlighter {
private:
  std::vector<SyntaxRule> rules;
  std::string file_extension;

public:
  void set_language(const std::string &ext);
  std::vector<std::pair<int, int>> get_colors(const std::string &line);
};

class PythonAPI; // Forward declaration

class Editor {
  friend class PythonAPI; // Allow PythonAPI to access private members

private:
  std::vector<FileBuffer> buffers;
  std::vector<SplitPane> panes;
  int current_pane;
  int current_buffer;

  bool running;
  std::string message;
  std::string clipboard;

  // Command palette
  bool show_command_palette;
  std::string command_palette_query;
  std::vector<std::string> command_palette_results;
  int command_palette_selected;

  // Telescope finder
  Telescope telescope;
  bool waiting_for_space_f;

  // Search panel
  bool show_search;
  std::string search_query;
  std::vector<std::pair<int, int>> search_results; // (line, col)
  int search_result_index;
  bool search_case_sensitive;

  // Save Prompt
  bool show_save_prompt;
  std::string save_prompt_input;

  // Quit Prompt
  bool show_quit_prompt;

  // Minimap
  bool show_minimap;
  int minimap_width;

  SyntaxHighlighter highlighter;
  Config config;
  ImageViewer image_viewer;
  Terminal terminal;
  UI *ui;
  Theme theme;
  std::string current_theme_name; // tracks active color scheme

  int status_height;
  int tab_height;
  int tab_size;
  bool auto_indent;
  bool needs_redraw;
  bool mouse_selecting;
  Cursor mouse_start;

  int idle_frame_count;
  int cursor_blink_frame;
  bool cursor_visible;

  bool show_context_menu;
  int context_menu_x;
  int context_menu_y;
  int context_menu_selected;

  Popup popup; // New

  // Custom Commands
  struct CustomCommand {
    std::string name;
    std::string callback;
  };
  std::vector<CustomCommand> custom_commands;

  // Input Prompt
  bool input_prompt_visible;
  std::string input_prompt_message;
  std::string input_prompt_buffer;
  std::string input_prompt_callback;

  // Sidebar
  bool show_sidebar;
  int sidebar_width;
  std::string root_dir;
  std::vector<FileNode> file_tree;
  int file_tree_selected;
  int file_tree_scroll; // New

  enum EditorFocus { FOCUS_EDITOR, FOCUS_SIDEBAR };
  EditorFocus focus_state;

  // Vim-like modal editing
  EditorMode mode;
  Cursor visual_start;      // anchor position when entering Visual mode
  bool visual_line_mode;    // true = V (line visual), false = v (char visual)
  char pending_key;         // first char of a two-key sequence (g, d, y, c…)
  bool has_pending_key;

  // Easter egg — Konami code
  std::vector<int> recent_keys;   // ring buffer of last 8 normal-mode keys
  int easter_egg_timer;           // frames remaining to show the easter egg

  PythonAPI *python_api;

  void render();
  void render_tabs();
  void render_panes();
  void render_easter_egg();
  void render_pane(const SplitPane &pane);
  void render_telescope();
  void render_minimap(int x, int y, int w, int h, int buffer_id);
  void render_image_viewer();
  void render_status_line();
  void render_command_palette();
  void render_search_panel();
  void render_context_menu();
  void render_save_prompt();
  void render_quit_prompt();
  void render_popup(); // New
  void render_input_prompt();
  void render_buffer_content(const SplitPane &pane, int buffer_id);

  void handle_input(int ch, bool is_ctrl = false, bool is_shift = false,
                    bool is_alt = false, int original_ch = 0);
  void handle_mouse_input(int x, int y, bool is_click, bool is_scroll_up,
                          bool is_scroll_down);

  void handle_normal_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  void handle_insert_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  void handle_visual_mode(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  void handle_command_palette(int ch);
  void handle_search_panel(int ch);
  void handle_telescope(int ch);
  void handle_save_prompt(int ch);
  void handle_input_prompt(int ch);
  void handle_mouse(void *event);

  void enter_normal_mode();
  void enter_insert_mode();
  void enter_visual_mode(bool line_mode = false);

  void vim_delete_line();
  void vim_delete_char();
  void vim_yank();
  void vim_paste();

  void move_cursor(int dx, int dy, bool extend_selection = false);
  void insert_char(char c);
  void insert_string(const std::string &str);
  void delete_char(bool forward = true);
  void delete_selection();
  void delete_line();

  void new_line();
  void insert_line_below();
  void insert_line_above();
  void duplicate_line();
  void move_line_up();
  void move_line_down();
  void toggle_comment();

  // API methods
  void show_popup(const std::string &text, int x, int y);
  void hide_popup();
  void register_command(const std::string &name, const std::string &callback);
  void show_input_prompt(const std::string &message,
                         const std::string &callback);
  void hide_input_prompt();
  void set_diagnostics(const std::string &filepath,
                       const std::vector<Diagnostic> &diagnostics);
  void add_diagnostic(const std::string &filepath,
                      const Diagnostic &diagnostic);
  void run_python_script(const std::string &script);

public:
  // Sidebar methods
  void toggle_sidebar();
  void load_file_tree(const std::string &path);

private:
  void handle_sidebar_input(int ch);
  void handle_sidebar_mouse(int x, int y, bool is_click);
  void render_sidebar();
  void build_tree(const std::string &path, std::vector<FileNode> &nodes,
                  int depth);

  void copy();
  void cut();
  void paste();

  void save_state();
  void undo();
  void redo();

  void clamp_cursor(int buffer_id);
  void move_word_forward(bool extend_selection = false);
  void move_word_backward(bool extend_selection = false);
  void move_to_line_start(bool extend_selection = false);
  void move_to_line_end(bool extend_selection = false);
  void move_to_file_start(bool extend_selection = false);
  void move_to_file_end(bool extend_selection = false);
  void ensure_cursor_visible(); // New method
  void select_all();
  void clear_selection();

  void open_file(const std::string &path);
  void close_buffer();
  void create_new_buffer();
  void save_file();
  void save_file_as();

  void toggle_minimap();
  void toggle_search();
  void toggle_command_palette();
  void execute_command(const std::string &cmd);

  void find_next();
  void find_prev();
  void perform_search();

  void split_pane_horizontal();
  void split_pane_vertical();
  void close_pane();
  void next_pane();
  void prev_pane();

  void toggle_bookmark();
  void next_bookmark();
  void prev_bookmark();

  void jump_to_matching_bracket();
  void format_document();
  void trim_trailing_whitespace();
  void toggle_auto_indent_setting();
  void change_tab_size(int delta);
  void apply_theme(const std::string &name);

  FileBuffer &get_buffer(int id = -1);
  SplitPane &get_pane(int id = -1);
  // EditorMode get_mode() const { return mode; }
  std::string get_file_extension(const std::string &path);
  std::string get_filename(const std::string &path);
  Theme &get_theme() { return theme; }

  int create_pane(int x, int y, int w, int h, int buffer_id);
  void update_pane_layout();
  void refresh_command_palette();
  void set_message(const std::string &msg);

public:
  Editor();
  ~Editor();
  void load_file(const std::string &fname);
  void run();
};

#endif
