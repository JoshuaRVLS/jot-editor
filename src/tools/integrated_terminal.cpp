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
      current_column(0), escape_state(ESC_NONE), osc_escape_pending(false) {}

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
  lines.push_back(line);
  while ((int)lines.size() > 2000) {
    lines.pop_front();
  }
}

void IntegratedTerminal::handle_csi_sequence(char final_char) {
  std::string params = csi_buffer;
  csi_buffer.clear();

  if (!params.empty() && (params[0] == '?' || params[0] == '>' ||
                          params[0] == '!')) {
    params.erase(params.begin());
  }

  int first = 1;
  size_t sep = params.find(';');
  if (sep == std::string::npos) {
    first = parse_csi_number(params, 1);
  } else {
    first = parse_csi_number(params.substr(0, sep), 1);
  }

  switch (final_char) {
  case 'A':
  case 'B':
    break;
  case 'C':
    current_column += (size_t)std::max(1, first);
    if (current_column > current_line.size()) {
      current_line.resize(current_column, ' ');
    }
    break;
  case 'D': {
    size_t amount = (size_t)std::max(1, first);
    current_column = (amount > current_column) ? 0 : current_column - amount;
    break;
  }
  case 'G':
    current_column = (size_t)std::max(0, first - 1);
    if (current_column > current_line.size()) {
      current_line.resize(current_column, ' ');
    }
    break;
  case 'H':
  case 'f':
    current_column = 0;
    break;
  case 'J':
    // This terminal view is line-based, not a full VT screen grid.
    // Treat erase-display requests as a complete visible clear so
    // commands like `clear` and Ctrl+L behave as expected.
    lines.clear();
    current_line.clear();
    current_column = 0;
    break;
  case 'K':
    if (first == 2) {
      current_line.clear();
      current_column = 0;
    } else if (first == 1) {
      size_t erase_to = std::min(current_column, current_line.size());
      current_line.erase(0, erase_to);
      current_column = 0;
    } else {
      if (current_column < current_line.size()) {
        current_line.erase(current_column);
      }
    }
    break;
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
    setenv("NO_COLOR", "1", 1);
    setenv("CLICOLOR", "0", 1);
    setenv("CLICOLOR_FORCE", "0", 1);
    unsetenv("PROMPT_COMMAND");

      if (shell && *shell) {
      std::string shell_path(shell);
      std::string shell_name = shell_path;
      size_t slash = shell_name.find_last_of('/');
      if (slash != std::string::npos) {
        shell_name = shell_name.substr(slash + 1);
      }

      if (shell_name == "zsh") {
        setenv("PS1", "%n %# ", 1);
        setenv("PROMPT", "%n %# ", 1);
        setenv("RPROMPT", "", 1);
        setenv("PROMPT_EOL_MARK", "", 1);
        execlp(shell, shell, "-f", "-i", nullptr);
      }

      if (shell_name == "bash") {
        setenv("PS1", "\\u \\$ ", 1);
        execlp(shell, shell, "--noprofile", "--norc", "-i", nullptr);
      }

      execlp(shell, shell, "-i", nullptr);
    }
    setenv("PS1", "jot $ ", 1);
    execlp("/bin/bash", "bash", "--noprofile", "--norc", "-i", nullptr);
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
  current_line.clear();
  current_column = 0;

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
  escape_state = ESC_NONE;
  osc_escape_pending = false;
  csi_buffer.clear();
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
        current_line.clear();
        current_column = 0;
      } else if (c == '\n') {
        push_line(current_line);
        current_line.clear();
        current_column = 0;
      } else if (c == '\b' || c == 127) {
        if (current_column > 0) {
          current_column--;
          if (current_column < current_line.size()) {
            current_line.erase(current_column, 1);
          }
        }
      } else if (c == '\t') {
        for (int j = 0; j < 2; j++) {
          if (current_column < current_line.size()) {
            current_line[current_column] = ' ';
          } else {
            current_line.push_back(' ');
          }
          current_column++;
        }
      } else if (c >= 32) {
        if (current_column > current_line.size()) {
          current_line.resize(current_column, ' ');
        }
        if (current_column < current_line.size()) {
          current_line[current_column] = (char)c;
        } else {
          current_line.push_back((char)c);
        }
        current_column++;
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
    send_bytes("\n");
    return true;
  }
  if (ch == '\t' || ch == 9) {
    send_bytes("\t");
    return true;
  }
  if (ch == 127 || ch == 8) {
    send_bytes("\x7f");
    return true;
  }

  if (is_ctrl && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
    // Treat Ctrl+J / Ctrl+M as Enter for shells that expect newline.
    if (ch == 'j' || ch == 'J' || ch == 'm' || ch == 'M') {
      send_bytes("\n");
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

std::vector<std::string> IntegratedTerminal::get_recent_lines(
    int max_lines) const {
  std::vector<std::string> out;
  if (max_lines <= 0) {
    return out;
  }

  int available = (int)lines.size() + 1;
  int take = std::min(max_lines, available);
  int take_from_deque = std::max(0, take - 1);
  int start = std::max(0, (int)lines.size() - take_from_deque);

  for (int i = start; i < (int)lines.size(); i++) {
    out.push_back(lines[i]);
  }
  out.push_back(current_line);
  while ((int)out.size() > max_lines) {
    out.erase(out.begin());
  }
  return out;
}
