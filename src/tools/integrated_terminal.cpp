#include "integrated_terminal.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__) || defined(__DragonFly__)
#include <pty.h>
#endif

IntegratedTerminal::IntegratedTerminal()
    : master_fd(-1), child_pid(-1), active(false), focused(false),
      current_column(0), scroll_offset(0), current_fg(7), current_bg(0),
      utf8_expected_bytes(0), escape_state(ESC_NONE),
      osc_escape_pending(false) {}

IntegratedTerminal::~IntegratedTerminal() { close_shell(); }

namespace {
void write_all(int fd, const char *data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = write(fd, data + sent, size - sent);
    if (n <= 0) {
      break;
    }
    sent += (size_t)n;
  }
}

int parse_csi_number(const std::string &value, int fallback) {
  if (value.empty()) {
    return fallback;
  }

  int result = 0;
  for (char c : value) {
    if (!std::isdigit((unsigned char)c)) {
      return fallback;
    }
    result = result * 10 + (c - '0');
  }
  return result;
}

termios build_shell_termios() {
  termios tio {};

  // Start with a sane interactive TTY profile for the child shell instead of
  // inheriting editor raw mode flags from stdin.
  tio.c_iflag = BRKINT | ICRNL | IXON | IMAXBEL;
  tio.c_oflag = OPOST | ONLCR;
  tio.c_cflag = CREAD | CS8;
  tio.c_lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK;

#ifdef ECHOCTL
  tio.c_lflag |= ECHOCTL;
#endif
#ifdef ECHOKE
  tio.c_lflag |= ECHOKE;
#endif

  tio.c_cc[VINTR] = 3;   // Ctrl+C
  tio.c_cc[VQUIT] = 28;  // Ctrl+backslash
  tio.c_cc[VERASE] = 127;
  tio.c_cc[VKILL] = 21;  // Ctrl+U
  tio.c_cc[VEOF] = 4;    // Ctrl+D
  tio.c_cc[VMIN] = 1;
  tio.c_cc[VTIME] = 0;

  return tio;
}
} // namespace

void IntegratedTerminal::push_line(const std::string &line) {
  sync_current_line();
  lines.push_back(line);
  styled_lines.push_back(current_styled_line);
  while ((int)lines.size() > 2000) {
    lines.pop_front();
    styled_lines.pop_front();
  }
}

void IntegratedTerminal::sync_current_line() {
  current_line.clear();
  for (const auto &cell : current_styled_line) {
    current_line += cell.ch;
  }
}

void IntegratedTerminal::put_glyph_at_cursor(const std::string &glyph) {
  if (glyph.empty()) {
    return;
  }

  while (current_column > current_styled_line.size()) {
    current_styled_line.push_back({" ", current_fg, current_bg});
  }
  if (current_column < current_styled_line.size()) {
    current_styled_line[current_column] = {glyph, current_fg, current_bg};
  } else {
    current_styled_line.push_back({glyph, current_fg, current_bg});
  }
  current_column++;
}

namespace {
std::vector<int> parse_sgr_params(const std::string &params) {
  std::vector<int> out;
  if (params.empty()) {
    out.push_back(0);
    return out;
  }

  size_t start = 0;
  while (start <= params.size()) {
    size_t end = params.find(';', start);
    std::string part = (end == std::string::npos)
                           ? params.substr(start)
                           : params.substr(start, end - start);
    if (part.empty()) {
      out.push_back(0);
    } else {
      out.push_back(parse_csi_number(part, 0));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}
} // namespace

void IntegratedTerminal::handle_csi_sequence(char final_char) {
  std::string params = csi_buffer;
  csi_buffer.clear();

  if (!params.empty() && (params[0] == '?' || params[0] == '>' ||
                          params[0] == '!')) {
    params.erase(params.begin());
  }

  int default_param = 1;
  if (final_char == 'J' || final_char == 'K') {
    // ANSI defaults for ED/EL are 0 (erase to end), not 1.
    default_param = 0;
  }

  int first = default_param;
  size_t sep = params.find(';');
  if (sep == std::string::npos) {
    first = parse_csi_number(params, default_param);
  } else {
    first = parse_csi_number(params.substr(0, sep), default_param);
  }

  switch (final_char) {
  case 'A':
  case 'B':
    break;
  case 'C':
    current_column += (size_t)std::max(1, first);
    while (current_column > current_styled_line.size()) {
      current_styled_line.push_back({" ", current_fg, current_bg});
    }
    break;
  case 'D': {
    size_t amount = (size_t)std::max(1, first);
    current_column = (amount > current_column) ? 0 : current_column - amount;
    break;
  }
  case 'G':
    current_column = (size_t)std::max(0, first - 1);
    while (current_column > current_styled_line.size()) {
      current_styled_line.push_back({" ", current_fg, current_bg});
    }
    break;
  case 'H':
  case 'f':
    current_column = 0;
    break;
  case 'J':
    // ED (Erase in Display):
    // 0 = cursor -> end (common during prompt redraw; do NOT full-clear)
    // 1 = start -> cursor
    // 2/3 = full clear
    // For this line-based view, only treat full clear modes as full reset.
    if (first == 2 || first == 3) {
      lines.clear();
      styled_lines.clear();
      current_line.clear();
      current_styled_line.clear();
      current_column = 0;
    }
    break;
  case 'K':
    if (first == 2) {
      current_styled_line.clear();
      current_line.clear();
      current_column = 0;
    } else if (first == 1) {
      // Erase from start to cursor without shifting the remainder.
      size_t erase_to = std::min(current_column, current_styled_line.size());
      for (size_t i = 0; i < erase_to; i++) {
        current_styled_line[i] = {" ", current_fg, current_bg};
      }
    } else {
      if (current_column < current_styled_line.size()) {
        current_styled_line.erase(current_styled_line.begin() + (long)current_column,
                                  current_styled_line.end());
      }
    }
    sync_current_line();
    break;
  case 'm': {
    auto params_vec = parse_sgr_params(params);
    for (size_t i = 0; i < params_vec.size(); i++) {
      int p = params_vec[i];
      if (p == 0) {
        current_fg = 7;
        current_bg = 0;
      } else if (p == 39) {
        current_fg = 7;
      } else if (p == 49) {
        current_bg = 0;
      } else if (p >= 30 && p <= 37) {
        current_fg = p - 30;
      } else if (p >= 90 && p <= 97) {
        current_fg = 8 + (p - 90);
      } else if (p >= 40 && p <= 47) {
        current_bg = p - 40;
      } else if (p >= 100 && p <= 107) {
        current_bg = 8 + (p - 100);
      } else if (p == 38) {
        if (i + 2 < params_vec.size() && params_vec[i + 1] == 5) {
          current_fg = std::clamp(params_vec[i + 2], 0, 255);
          i += 2;
        } else if (i + 4 < params_vec.size() && params_vec[i + 1] == 2) {
          i += 4; // RGB not directly supported in 256 UI; ignore for now.
        }
      } else if (p == 48) {
        if (i + 2 < params_vec.size() && params_vec[i + 1] == 5) {
          current_bg = std::clamp(params_vec[i + 2], 0, 255);
          i += 2;
        } else if (i + 4 < params_vec.size() && params_vec[i + 1] == 2) {
          i += 4;
        }
      }
    }
    break;
  }
  default:
    break;
  }
}

bool IntegratedTerminal::open_shell() {
#if defined(JOT_PLATFORM_WINDOWS)
  return false;
#else
  if (active) {
    focused = true;
    return true;
  }

  termios shell_termios = build_shell_termios();
  winsize shell_ws {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &shell_ws) != 0) {
    shell_ws.ws_col = 120;
    shell_ws.ws_row = 40;
  }

  int fd = -1;
  pid_t pid = forkpty(&fd, nullptr, &shell_termios, &shell_ws);
  if (pid < 0) {
    return false;
  }

  if (pid == 0) {
    const char *shell = getenv("SHELL");
    setenv("TERM", "xterm-256color", 1);
    if (shell && *shell) {
      execlp(shell, shell, "-i", nullptr);
    }
    execlp("/bin/bash", "bash", "-i", nullptr);
    execlp("/bin/sh", "sh", "-i", nullptr);
    _exit(127);
  }

  master_fd = fd;
  child_pid = pid;
  active = true;
  focused = true;
  escape_state = ESC_NONE;
  osc_escape_pending = false;
  csi_buffer.clear();
  lines.clear();
  styled_lines.clear();
  current_line.clear();
  current_styled_line.clear();
  current_column = 0;
  scroll_offset = 0;
  current_fg = 7;
  current_bg = 0;
  utf8_pending.clear();
  utf8_expected_bytes = 0;

  int flags = fcntl(master_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
  }
  return true;
#endif
}

void IntegratedTerminal::close_shell() {
  if (!active) {
    return;
  }

  if (child_pid > 0) {
    kill(child_pid, SIGTERM);
    waitpid(child_pid, nullptr, WNOHANG);
  }
  if (master_fd >= 0) {
    close(master_fd);
  }
  master_fd = -1;
  child_pid = -1;
  active = false;
  focused = false;
  current_column = 0;
  scroll_offset = 0;
  escape_state = ESC_NONE;
  osc_escape_pending = false;
  csi_buffer.clear();
  styled_lines.clear();
  current_styled_line.clear();
  utf8_pending.clear();
  utf8_expected_bytes = 0;
}

bool IntegratedTerminal::poll_output() {
  if (!active || master_fd < 0) {
    return false;
  }

  bool changed = false;
  char buf[4096];

  while (true) {
    ssize_t n = read(master_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }

    changed = true;
    for (ssize_t i = 0; i < n; i++) {
      unsigned char c = static_cast<unsigned char>(buf[i]);

      if (escape_state == ESC_PENDING) {
        if (c == '[') {
          escape_state = ESC_CSI;
          csi_buffer.clear();
        } else if (c == ']') {
          escape_state = ESC_OSC;
          osc_escape_pending = false;
        } else if (c >= 0x20 && c <= 0x2f) {
          escape_state = ESC_OTHER;
        } else {
          escape_state = ESC_NONE;
        }
        continue;
      }

      if (escape_state == ESC_CSI) {
        if (c >= 0x30 && c <= 0x3f) {
          csi_buffer.push_back((char)c);
          continue;
        }
        if (c >= 0x20 && c <= 0x2f) {
          continue;
        }
        if (c >= '@' && c <= '~') {
          handle_csi_sequence((char)c);
          escape_state = ESC_NONE;
        }
        continue;
      }

      if (escape_state == ESC_OSC) {
        if (osc_escape_pending) {
          osc_escape_pending = false;
          if (c == '\\') {
            escape_state = ESC_NONE;
            continue;
          }
        }

        if (c == '\a') {
          escape_state = ESC_NONE;
          continue;
        }

        if (c == 27) {
          osc_escape_pending = true;
        }
        continue;
      }

      if (escape_state == ESC_OTHER) {
        if (c >= 0x30 && c <= 0x7e) {
          escape_state = ESC_NONE;
        }
        continue;
      }

      if (c == 27) {
        escape_state = ESC_PENDING;
        continue;
      }

      if (c == '\r') {
        current_column = 0;
      } else if (c == '\f') {
        lines.clear();
        styled_lines.clear();
        current_line.clear();
        current_styled_line.clear();
        current_column = 0;
        utf8_pending.clear();
        utf8_expected_bytes = 0;
      } else if (c == '\n') {
        if (!utf8_pending.empty()) {
          put_glyph_at_cursor("?");
          utf8_pending.clear();
          utf8_expected_bytes = 0;
        }
        push_line(current_line);
        current_line.clear();
        current_styled_line.clear();
        current_column = 0;
      } else if (c == '\b' || c == 127) {
        if (current_column > 0) {
          current_column--;
          if (current_column < current_styled_line.size()) {
            current_styled_line.erase(current_styled_line.begin() +
                                      (long)current_column);
          }
          sync_current_line();
        }
      } else if (c == '\t') {
        for (int j = 0; j < 2; j++) {
          put_glyph_at_cursor(" ");
        }
        sync_current_line();
      } else if (c >= 32) {
        if (utf8_expected_bytes > 0) {
          if ((c & 0xC0) == 0x80) {
            utf8_pending.push_back((char)c);
            if ((int)utf8_pending.size() >= utf8_expected_bytes) {
              put_glyph_at_cursor(utf8_pending);
              utf8_pending.clear();
              utf8_expected_bytes = 0;
              sync_current_line();
            }
          } else {
            put_glyph_at_cursor("?");
            utf8_pending.clear();
            utf8_expected_bytes = 0;
            if (c < 0x80) {
              put_glyph_at_cursor(std::string(1, (char)c));
              sync_current_line();
            } else if ((c & 0xE0) == 0xC0) {
              utf8_pending = std::string(1, (char)c);
              utf8_expected_bytes = 2;
            } else if ((c & 0xF0) == 0xE0) {
              utf8_pending = std::string(1, (char)c);
              utf8_expected_bytes = 3;
            } else if ((c & 0xF8) == 0xF0) {
              utf8_pending = std::string(1, (char)c);
              utf8_expected_bytes = 4;
            } else {
              put_glyph_at_cursor("?");
              sync_current_line();
            }
          }
        } else if (c < 0x80) {
          put_glyph_at_cursor(std::string(1, (char)c));
          sync_current_line();
        } else if ((c & 0xE0) == 0xC0) {
          utf8_pending = std::string(1, (char)c);
          utf8_expected_bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
          utf8_pending = std::string(1, (char)c);
          utf8_expected_bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
          utf8_pending = std::string(1, (char)c);
          utf8_expected_bytes = 4;
        } else {
          put_glyph_at_cursor("?");
          sync_current_line();
        }
      }
    }
  }

  int status = 0;
  if (child_pid > 0) {
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid) {
      if (master_fd >= 0) {
        close(master_fd);
      }
      active = false;
      focused = false;
      master_fd = -1;
      child_pid = -1;
      push_line("[terminal exited]");
      changed = true;
    }
  }

  return changed;
}

bool IntegratedTerminal::send_key(int ch, bool is_ctrl, bool is_shift,
                                  bool is_alt) {
  (void)is_shift;
  if (!active || master_fd < 0) {
    return false;
  }

  reset_scroll();

  auto send_bytes = [&](const char *s) {
    if (!s) {
      return;
    }
    write_all(master_fd, s, strlen(s));
  };

  if (ch == 1008) {
    send_bytes("\x1b[A");
    return true;
  }
  if (ch == 1009) {
    send_bytes("\x1b[B");
    return true;
  }
  if (ch == 1010) {
    send_bytes("\x1b[C");
    return true;
  }
  if (ch == 1011) {
    send_bytes("\x1b[D");
    return true;
  }
  if (ch == 1012) {
    send_bytes("\x1b[H");
    return true;
  }
  if (ch == 1013) {
    send_bytes("\x1b[F");
    return true;
  }
  if (ch == '\n' || ch == '\r' || ch == 10 || ch == 13) {
    send_bytes("\r");
    return true;
  }
  if (ch == '\t' || ch == 9) {
    if (is_shift || ch == 1017) {
      send_bytes("\x1b[Z");
      return true;
    }
    send_bytes("\t");
    return true;
  }
  if (ch == 1001) {
    send_bytes("\x1b[3~");
    return true;
  }
  if (ch == 127 || ch == 8) {
    send_bytes("\x7f");
    return true;
  }

  // Handle raw Ctrl keycodes directly when callers pass control bytes.
  if (ch >= 1 && ch <= 26) {
    unsigned char ctrl = (unsigned char)ch;
    write_all(master_fd, (const char *)&ctrl, 1);
    return true;
  }

  if (is_ctrl && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
    // Treat Ctrl+J / Ctrl+M as Enter for shells that expect newline.
    if (ch == 'j' || ch == 'J' || ch == 'm' || ch == 'M') {
      send_bytes("\r");
      return true;
    }
    unsigned char ctrl = (unsigned char)(std::tolower(ch) - 'a' + 1);
    write_all(master_fd, (const char *)&ctrl, 1);
    return true;
  }

  if (is_alt && ch >= 32 && ch < 127) {
    unsigned char esc = 27;
    unsigned char raw = (unsigned char)ch;
    write_all(master_fd, (const char *)&esc, 1);
    write_all(master_fd, (const char *)&raw, 1);
    return true;
  }

  if (ch >= 32 && ch < 127) {
    unsigned char raw = (unsigned char)ch;
    write_all(master_fd, (const char *)&raw, 1);
    return true;
  }

  return false;
}

bool IntegratedTerminal::scroll_lines(int delta, int visible_rows) {
  int total = (int)lines.size() + 1;
  int view = std::max(1, visible_rows);
  int max_offset = std::max(0, total - view);
  int next = std::clamp(scroll_offset + delta, 0, max_offset);
  bool changed = (next != scroll_offset);
  scroll_offset = next;
  return changed;
}

void IntegratedTerminal::reset_scroll() { scroll_offset = 0; }

std::vector<std::string> IntegratedTerminal::get_recent_lines(
    int max_lines) const {
  std::vector<std::string> out;
  if (max_lines <= 0) {
    return out;
  }

  int total = (int)lines.size() + 1;
  int take = std::min(max_lines, total);
  int max_offset = std::max(0, total - take);
  int offset = std::clamp(scroll_offset, 0, max_offset);
  int end_exclusive = std::clamp(total - offset, 0, total);
  int start = std::max(0, end_exclusive - take);

  for (int idx = start; idx < end_exclusive; idx++) {
    if (idx < (int)lines.size()) {
      out.push_back(lines[idx]);
    } else {
      out.push_back(current_line);
    }
  }
  return out;
}

std::vector<std::vector<IntegratedTerminal::StyledCell>>
IntegratedTerminal::get_recent_styled_lines(int max_lines) const {
  std::vector<std::vector<StyledCell>> out;
  if (max_lines <= 0) {
    return out;
  }

  int total = (int)styled_lines.size() + 1;
  int take = std::min(max_lines, total);
  int max_offset = std::max(0, total - take);
  int offset = std::clamp(scroll_offset, 0, max_offset);
  int end_exclusive = std::clamp(total - offset, 0, total);
  int start = std::max(0, end_exclusive - take);

  for (int idx = start; idx < end_exclusive; idx++) {
    std::vector<StyledCell> row;
    if (idx < (int)styled_lines.size()) {
      row.reserve(styled_lines[idx].size());
      for (const auto &c : styled_lines[idx]) {
        row.push_back({c.ch, c.fg, c.bg});
      }
    } else {
      row.reserve(current_styled_line.size());
      for (const auto &c : current_styled_line) {
        row.push_back({c.ch, c.fg, c.bg});
      }
    }
    out.push_back(std::move(row));
  }
  return out;
}
