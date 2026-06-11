#ifndef INTEGRATED_TERMINAL_H
#define INTEGRATED_TERMINAL_H

#include <deque>
#include <string>
#include <vector>

class IntegratedTerminal {
private:
  struct TerminalCell {
    std::string ch;
    int fg;
    int bg;
  };

  enum EscapeState {
    ESC_NONE,
    ESC_PENDING,
    ESC_CSI,
    ESC_OSC,
    ESC_STRING,
    ESC_OTHER
  };

  int master_fd;
  int child_pid;
  bool active;
  bool focused;
  std::string label;
  std::deque<std::string> lines;
  std::deque<std::vector<TerminalCell>> styled_lines;
  std::string current_line;
  std::vector<TerminalCell> current_styled_line;
  size_t current_column;
  int scroll_offset;
  int current_fg;
  int current_bg;
  std::string utf8_pending;
  int utf8_expected_bytes;
  EscapeState escape_state;
  bool osc_escape_pending;
  std::string csi_buffer;

  void push_line(const std::string &line);
  void handle_csi_sequence(char final_char);
  void sync_current_line();
  void put_glyph_at_cursor(const std::string &glyph);

public:
  struct StyledCell {
    std::string ch;
    int fg;
    int bg;
  };

  struct OutputRow {
    std::string text;
    std::vector<StyledCell> cells;
  };

  IntegratedTerminal();
  ~IntegratedTerminal();

  bool open_shell(const std::string &cwd = "");
  void close_shell();
  bool poll_output();
  bool send_key(int ch, bool is_ctrl, bool is_shift, bool is_alt);
  bool send_text(const std::string &text);
  bool scroll_lines(int delta, int visible_rows);
  void reset_scroll();

  bool is_active() const { return active; }
  bool is_focused() const { return focused; }
  void set_focused(bool value) { focused = value; }
  const std::string &get_label() const { return label; }
  void set_label(const std::string &value) { label = value; }
  const std::string &get_current_line() const { return current_line; }
  size_t get_cursor_column() const { return current_column; }

  std::vector<std::string> get_recent_lines(int max_lines) const;
  std::vector<std::vector<StyledCell>> get_recent_styled_lines(int max_lines) const;
  std::vector<OutputRow> get_recent_output_rows(int max_lines) const;
};

#endif
