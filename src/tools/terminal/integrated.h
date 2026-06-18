#ifndef INTEGRATED_TERMINAL_H
#define INTEGRATED_TERMINAL_H

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

typedef struct VTerm VTerm;
typedef struct VTermScreen VTermScreen;

class IntegratedTerminal {
public:
  struct StyledCell {
    std::string ch;
    int fg;
    int bg;
    bool fg_default = false;
    bool bg_default = false;
    bool reverse = false;
  };

  struct ResolvedCellColors {
    int fg;
    int bg;
  };

  struct OutputRow {
    std::string text;
    std::vector<StyledCell> cells;
  };

private:
  int master_fd;
  int child_pid;
  bool active;
  bool focused;
  std::string label;
  VTerm *vterm;
  VTermScreen *screen;
  int rows;
  int cols;
  int cursor_row;
  int cursor_col;
  std::string output_buffer;
  std::string current_line;
  int scroll_offset;
  std::deque<std::vector<StyledCell>> scrollback;

  void ensure_vterm(int new_rows, int new_cols);
  void destroy_vterm();
  void append_output(const char *s, size_t len);
  void write_output_buffer();
  void refresh_current_line();
  static void vterm_output_callback(const char *s, size_t len, void *user);

public:
  IntegratedTerminal();
  ~IntegratedTerminal();

  static ResolvedCellColors resolve_cell_colors(const StyledCell &cell,
                                                int default_fg,
                                                int default_bg) {
    ResolvedCellColors colors{cell.fg_default ? default_fg : cell.fg,
                              cell.bg_default ? default_bg : cell.bg};
    if (cell.reverse) {
      std::swap(colors.fg, colors.bg);
    }
    return colors;
  }

  bool open_shell(const std::string &cwd = "");
  void close_shell();
  bool poll_output();
  void resize(int new_rows, int new_cols);
  bool send_key(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool send_text(const std::string &text);
  bool scroll_lines(int delta, int visible_rows);
  void reset_scroll();

  bool is_active() const { return active; }
  int get_master_fd() const { return master_fd; }
  bool is_focused() const { return focused; }
  void set_focused(bool value) { focused = value; }
  const std::string &get_label() const { return label; }
  void set_label(const std::string &value) { label = value; }
  const std::string &get_current_line() const { return current_line; }
  size_t get_cursor_column() const { return (size_t)std::max(0, cursor_col); }
  int get_cursor_row() const { return cursor_row; }

  std::vector<std::string> get_recent_lines(int max_lines) const;
  std::vector<std::vector<StyledCell>> get_recent_styled_lines(int max_lines) const;
  std::vector<OutputRow> get_recent_output_rows(int max_lines) const;
};

#endif
