#ifndef INTEGRATED_TERMINAL_H
#define INTEGRATED_TERMINAL_H

#include <deque>
#include <string>
#include <vector>

class IntegratedTerminal {
private:
  enum EscapeState { ESC_NONE, ESC_PENDING, ESC_CSI, ESC_OSC, ESC_OTHER };

  int master_fd;
  int child_pid;
  bool active;
  bool focused;
  std::deque<std::string> lines;
  std::string current_line;
  size_t current_column;
  EscapeState escape_state;
  bool osc_escape_pending;
  std::string csi_buffer;

  void push_line(const std::string &line);
  void handle_csi_sequence(char final_char);

public:
  IntegratedTerminal();
  ~IntegratedTerminal();

  bool open_shell();
  void close_shell();
  bool poll_output();
  bool send_key(int ch, bool is_ctrl, bool is_shift, bool is_alt);

  bool is_active() const { return active; }
  bool is_focused() const { return focused; }
  void set_focused(bool value) { focused = value; }
  const std::string &get_current_line() const { return current_line; }
  size_t get_cursor_column() const { return current_column; }

  std::vector<std::string> get_recent_lines(int max_lines) const;
};

#endif
