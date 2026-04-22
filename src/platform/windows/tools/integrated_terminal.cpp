#include "integrated_terminal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kMaxLines = 2000;

std::string trim_cr(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
    s.pop_back();
  }
  return s;
}

} // namespace

IntegratedTerminal::IntegratedTerminal()
    : master_fd(-1), child_pid(-1), active(false), focused(false), current_column(0),
      scroll_offset(0), current_fg(7), current_bg(0), utf8_expected_bytes(0),
      escape_state(ESC_NONE), osc_escape_pending(false) {}

IntegratedTerminal::~IntegratedTerminal() { close_shell(); }

bool IntegratedTerminal::open_shell() {
  active = true;
  lines.clear();
  styled_lines.clear();
  current_line.clear();
  current_styled_line.clear();
  current_column = 0;
  scroll_offset = 0;
  push_line("[Windows shell mode]");
  push_line("Type command and press Enter");
  return true;
}

void IntegratedTerminal::close_shell() {
  active = false;
  focused = false;
}

bool IntegratedTerminal::poll_output() { return false; }

bool IntegratedTerminal::send_key(int ch, bool is_ctrl, bool, bool) {
  if (!active) {
    return false;
  }

  if (is_ctrl && (ch == 'l' || ch == 'L')) {
    lines.clear();
    styled_lines.clear();
    current_line.clear();
    current_styled_line.clear();
    current_column = 0;
    return true;
  }

  if (ch == '\n' || ch == '\r' || ch == 13) {
    const std::string command = current_line;
    push_line("$ " + command);
    current_line.clear();
    current_styled_line.clear();
    current_column = 0;

    if (!command.empty()) {
      FILE *pipe = _popen(command.c_str(), "r");
      if (!pipe) {
        push_line("[failed to launch command]");
        return true;
      }

      char buf[1024];
      while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        push_line(trim_cr(std::string(buf)));
      }
      int rc = _pclose(pipe);
      push_line("[exit " + std::to_string(rc) + "]");
    }

    return true;
  }

  if (ch == 8 || ch == 127) {
    if (!current_line.empty()) {
      current_line.pop_back();
      if (!current_styled_line.empty()) {
        current_styled_line.pop_back();
      }
      current_column = current_line.size();
      return true;
    }
    return false;
  }

  if (ch >= 32 && ch <= 126) {
    char c = (char)ch;
    current_line.push_back(c);
    TerminalCell cell;
    cell.ch = std::string(1, c);
    cell.fg = current_fg;
    cell.bg = current_bg;
    current_styled_line.push_back(cell);
    current_column = current_line.size();
    return true;
  }

  return false;
}

bool IntegratedTerminal::scroll_lines(int delta, int visible_rows) {
  int max_offset = std::max(0, (int)lines.size() - std::max(1, visible_rows));
  int next = std::clamp(scroll_offset + delta, 0, max_offset);
  if (next == scroll_offset) {
    return false;
  }
  scroll_offset = next;
  return true;
}

void IntegratedTerminal::reset_scroll() { scroll_offset = 0; }

std::vector<std::string> IntegratedTerminal::get_recent_lines(int max_lines) const {
  std::vector<std::string> out;
  int limit = std::max(1, max_lines);

  std::vector<std::string> all(lines.begin(), lines.end());
  all.push_back(current_line);

  int max_offset = std::max(0, (int)all.size() - limit);
  int offset = std::clamp(scroll_offset, 0, max_offset);
  int start = std::max(0, (int)all.size() - limit - offset);

  for (int i = start; i < (int)all.size() && (int)out.size() < limit; i++) {
    out.push_back(all[i]);
  }

  if (out.empty()) {
    out.push_back("");
  }
  return out;
}

std::vector<std::vector<IntegratedTerminal::StyledCell>>
IntegratedTerminal::get_recent_styled_lines(int max_lines) const {
  std::vector<std::vector<StyledCell>> out;
  int limit = std::max(1, max_lines);

  std::vector<std::vector<StyledCell>> all;
  all.reserve(styled_lines.size() + 1);
  for (const auto &line : styled_lines) {
    std::vector<StyledCell> converted;
    converted.reserve(line.size());
    for (const auto &cell : line) {
      StyledCell c;
      c.ch = cell.ch;
      c.fg = cell.fg;
      c.bg = cell.bg;
      converted.push_back(c);
    }
    all.push_back(std::move(converted));
  }

  std::vector<StyledCell> current;
  current.reserve(current_styled_line.size());
  for (const auto &cell : current_styled_line) {
    StyledCell c;
    c.ch = cell.ch;
    c.fg = cell.fg;
    c.bg = cell.bg;
    current.push_back(c);
  }
  all.push_back(std::move(current));

  int max_offset = std::max(0, (int)all.size() - limit);
  int offset = std::clamp(scroll_offset, 0, max_offset);
  int start = std::max(0, (int)all.size() - limit - offset);

  for (int i = start; i < (int)all.size() && (int)out.size() < limit; i++) {
    out.push_back(all[i]);
  }

  if (out.empty()) {
    out.push_back({});
  }

  return out;
}

void IntegratedTerminal::push_line(const std::string &line) {
  lines.push_back(line);

  std::vector<TerminalCell> styled;
  styled.reserve(line.size());
  for (char c : line) {
    TerminalCell cell;
    cell.ch = std::string(1, c);
    cell.fg = current_fg;
    cell.bg = current_bg;
    styled.push_back(cell);
  }
  styled_lines.push_back(std::move(styled));

  while ((int)lines.size() > kMaxLines) {
    lines.pop_front();
  }
  while ((int)styled_lines.size() > kMaxLines) {
    styled_lines.pop_front();
  }
}

void IntegratedTerminal::handle_csi_sequence(char) {}

void IntegratedTerminal::sync_current_line() { current_column = current_line.size(); }

void IntegratedTerminal::put_glyph_at_cursor(const std::string &glyph) {
  if (glyph.empty()) {
    return;
  }
  current_line += glyph;
  current_column = current_line.size();
}
