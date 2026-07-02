#include "tools/terminal/integrated.h"

#include <algorithm>

namespace {
constexpr int kDefaultRows = 24;
constexpr int kDefaultCols = 80;
}

IntegratedTerminal::IntegratedTerminal()
    : master_fd(-1), child_pid(-1), active(false), focused(false), label(""),
      vterm(nullptr), screen(nullptr), rows(kDefaultRows), cols(kDefaultCols),
      cursor_row(0), cursor_col(0), scroll_offset(0) {}

IntegratedTerminal::~IntegratedTerminal() { close_shell(); }

void IntegratedTerminal::ensure_vterm(int new_rows, int new_cols) {
  rows = std::max(1, new_rows);
  cols = std::max(1, new_cols);
}

void IntegratedTerminal::destroy_vterm() {
  vterm = nullptr;
  screen = nullptr;
  current_line.clear();
  output_buffer.clear();
  scrollback.clear();
  cursor_row = 0;
  cursor_col = 0;
  scroll_offset = 0;
}

void IntegratedTerminal::append_output(const char *, size_t) {}
void IntegratedTerminal::write_output_buffer() {}
void IntegratedTerminal::refresh_current_line() {}
void IntegratedTerminal::vterm_output_callback(const char *, size_t, void *) {}

bool IntegratedTerminal::open_shell(const std::string &) {
  active = false;
  focused = false;
  current_line = "ConPTY integrated terminal is not implemented in this build";
  return false;
}

void IntegratedTerminal::close_shell() {
  master_fd = -1;
  child_pid = -1;
  active = false;
  focused = false;
  destroy_vterm();
}

bool IntegratedTerminal::poll_output() { return false; }

void IntegratedTerminal::resize(int new_rows, int new_cols) {
  ensure_vterm(new_rows, new_cols);
}

bool IntegratedTerminal::send_key(int, bool, bool, bool) { return false; }

bool IntegratedTerminal::send_text(const std::string &) { return false; }

bool IntegratedTerminal::scroll_lines(int delta, int visible_rows) {
  int total = (int)scrollback.size() + rows;
  int view = std::max(1, visible_rows);
  int max_offset = std::max(0, total - view);
  int next = std::clamp(scroll_offset + delta, 0, max_offset);
  bool changed = next != scroll_offset;
  scroll_offset = next;
  return changed;
}

void IntegratedTerminal::reset_scroll() { scroll_offset = 0; }

std::vector<IntegratedTerminal::OutputRow>
IntegratedTerminal::get_recent_output_rows(int max_lines) const {
  std::vector<OutputRow> out;
  if (max_lines <= 0) {
    return out;
  }
  if (!current_line.empty()) {
    out.push_back({current_line, {}});
  }
  return out;
}

std::vector<std::string>
IntegratedTerminal::get_recent_lines(int max_lines) const {
  std::vector<std::string> out;
  for (const auto &row : get_recent_output_rows(max_lines)) {
    out.push_back(row.text);
  }
  return out;
}

std::vector<std::vector<IntegratedTerminal::StyledCell>>
IntegratedTerminal::get_recent_styled_lines(int max_lines) const {
  std::vector<std::vector<StyledCell>> out;
  for (const auto &row : get_recent_output_rows(max_lines)) {
    out.push_back(row.cells);
  }
  return out;
}
