#include "integrated_terminal.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vterm.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__) || defined(__DragonFly__)
#include <pty.h>
#endif

namespace {
constexpr int kDefaultRows = 24;
constexpr int kDefaultCols = 80;
constexpr int kMaxScrollbackLines = 2000;
constexpr int kPollReadLimit = 16;
constexpr size_t kPollByteLimit = 64 * 1024;

void write_all(int fd, const char *data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    ssize_t n = write(fd, data + sent, size - sent);
    if (n <= 0) {
      if (errno == EAGAIN || errno == EINTR)
        continue;
      break;
    }
    sent += (size_t)n;
  }
}

termios build_shell_termios() {
  termios tio {};
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

  tio.c_cc[VINTR] = 3;
  tio.c_cc[VQUIT] = 28;
  tio.c_cc[VERASE] = 127;
  tio.c_cc[VKILL] = 21;
  tio.c_cc[VEOF] = 4;
  tio.c_cc[VMIN] = 1;
  tio.c_cc[VTIME] = 0;
  return tio;
}

std::string utf8_from_codepoint(uint32_t cp) {
  std::string out;
  if (cp == 0) {
    return " ";
  }
  if (cp <= 0x7f) {
    out.push_back((char)cp);
  } else if (cp <= 0x7ff) {
    out.push_back((char)(0xc0 | ((cp >> 6) & 0x1f)));
    out.push_back((char)(0x80 | (cp & 0x3f)));
  } else if (cp <= 0xffff) {
    out.push_back((char)(0xe0 | ((cp >> 12) & 0x0f)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back((char)(0x80 | (cp & 0x3f)));
  } else if (cp <= 0x10ffff) {
    out.push_back((char)(0xf0 | ((cp >> 18) & 0x07)));
    out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back((char)(0x80 | (cp & 0x3f)));
  } else {
    out = "?";
  }
  return out;
}

std::string cell_text(const VTermScreenCell &cell) {
  std::string out;
  for (uint32_t ch : cell.chars) {
    if (ch == 0)
      break;
    out += utf8_from_codepoint(ch);
  }
  return out.empty() ? " " : out;
}

int color_to_index(const VTermScreen *screen, VTermColor color, int fallback) {
  if (VTERM_COLOR_IS_DEFAULT_FG(&color) || VTERM_COLOR_IS_DEFAULT_BG(&color)) {
    return fallback;
  }
  if (VTERM_COLOR_IS_INDEXED(&color)) {
    return std::clamp((int)color.indexed.idx, 0, 255);
  }

  VTermColor rgb = color;
  vterm_screen_convert_color_to_rgb(screen, &rgb);
  if (!VTERM_COLOR_IS_RGB(&rgb)) {
    return fallback;
  }

  const int r = rgb.rgb.red;
  const int g = rgb.rgb.green;
  const int b = rgb.rgb.blue;
  if (r == g && g == b) {
    if (r < 8)
      return 16;
    if (r > 248)
      return 231;
    return std::clamp(232 + ((r - 8) * 24) / 247, 232, 255);
  }

  auto cube = [](int v) { return std::clamp((v * 5 + 127) / 255, 0, 5); };
  return 16 + 36 * cube(r) + 6 * cube(g) + cube(b);
}

IntegratedTerminal::StyledCell styled_from_vterm_cell(
    const VTermScreen *screen, const VTermScreenCell &cell) {
  int fg = color_to_index(screen, cell.fg, 7);
  int bg = color_to_index(screen, cell.bg, 0);
  if (cell.attrs.reverse) {
    std::swap(fg, bg);
  }
  return {cell_text(cell), fg, bg};
}

std::string row_text(const std::vector<IntegratedTerminal::StyledCell> &cells) {
  std::string text;
  for (const auto &cell : cells) {
    text += cell.ch;
  }
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
  return text;
}

int screen_damage(VTermRect, void *) { return 1; }
int screen_moverect(VTermRect, VTermRect, void *) { return 1; }
int screen_movecursor(VTermPos pos, VTermPos, int, void *user) {
  auto *term = static_cast<IntegratedTerminal *>(user);
  if (term) {
    // Public resize/render code reads cursor via accessors; use the screen query
    // as source of truth after libvterm has processed input.
    (void)pos;
  }
  return 1;
}
int screen_settermprop(VTermProp, VTermValue *, void *) { return 1; }
int screen_bell(void *) { return 1; }
int screen_resize(int, int, void *) { return 1; }
int screen_sb_popline(int, VTermScreenCell *, void *) { return 0; }
int screen_sb_clear(void *user) {
  auto *term = static_cast<IntegratedTerminal *>(user);
  if (term) {
    term->reset_scroll();
  }
  return 1;
}

} // namespace

IntegratedTerminal::IntegratedTerminal()
    : master_fd(-1), child_pid(-1), active(false), focused(false), label(""),
      vterm(nullptr), screen(nullptr), rows(kDefaultRows), cols(kDefaultCols),
      cursor_row(0), cursor_col(0), scroll_offset(0) {}

IntegratedTerminal::~IntegratedTerminal() { close_shell(); }

void IntegratedTerminal::ensure_vterm(int new_rows, int new_cols) {
  new_rows = std::max(1, new_rows);
  new_cols = std::max(1, new_cols);
  if (!vterm) {
    rows = new_rows;
    cols = new_cols;
    vterm = vterm_new(rows, cols);
    vterm_set_utf8(vterm, 1);
    vterm_output_set_callback(vterm, vterm_output_callback, this);
    screen = vterm_obtain_screen(vterm);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_enable_reflow(screen, true);

    static const VTermScreenCallbacks callbacks = {
        screen_damage,      screen_moverect, screen_movecursor,
        screen_settermprop, screen_bell,     screen_resize,
        [](int cols, const VTermScreenCell *cells, void *user) -> int {
          auto *term = static_cast<IntegratedTerminal *>(user);
          if (!term || !term->screen || !cells || cols <= 0) {
            return 0;
          }
          std::vector<StyledCell> row;
          row.reserve((size_t)cols);
          for (int i = 0; i < cols; i++) {
            row.push_back(styled_from_vterm_cell(term->screen, cells[i]));
          }
          term->scrollback.push_back(std::move(row));
          while (term->scrollback.size() > kMaxScrollbackLines) {
            term->scrollback.pop_front();
          }
          return 1;
        },
        screen_sb_popline, screen_sb_clear};
    vterm_screen_set_callbacks(screen, &callbacks, this);

    VTermColor fg;
    VTermColor bg;
    vterm_color_indexed(&fg, 7);
    vterm_color_indexed(&bg, 0);
    vterm_screen_set_default_colors(screen, &fg, &bg);
    vterm_screen_reset(screen, 1);
  } else if (rows != new_rows || cols != new_cols) {
    rows = new_rows;
    cols = new_cols;
    vterm_set_size(vterm, rows, cols);
  }
}

void IntegratedTerminal::destroy_vterm() {
  if (vterm) {
    vterm_free(vterm);
  }
  vterm = nullptr;
  screen = nullptr;
  rows = kDefaultRows;
  cols = kDefaultCols;
  cursor_row = 0;
  cursor_col = 0;
  current_line.clear();
  output_buffer.clear();
  scrollback.clear();
  scroll_offset = 0;
}

void IntegratedTerminal::append_output(const char *s, size_t len) {
  if (!s || len == 0) {
    return;
  }
  output_buffer.append(s, len);
}

void IntegratedTerminal::vterm_output_callback(const char *s, size_t len,
                                               void *user) {
  auto *term = static_cast<IntegratedTerminal *>(user);
  if (term) {
    term->append_output(s, len);
  }
}

void IntegratedTerminal::write_output_buffer() {
  if (!active || master_fd < 0 || output_buffer.empty()) {
    output_buffer.clear();
    return;
  }
  write_all(master_fd, output_buffer.data(), output_buffer.size());
  output_buffer.clear();
}

void IntegratedTerminal::refresh_current_line() {
  if (!screen) {
    current_line.clear();
    cursor_row = 0;
    cursor_col = 0;
    return;
  }

  VTermState *state = vterm_obtain_state(vterm);
  VTermPos pos {0, 0};
  vterm_state_get_cursorpos(state, &pos);
  cursor_row = std::clamp(pos.row, 0, std::max(0, rows - 1));
  cursor_col = std::clamp(pos.col, 0, std::max(0, cols - 1));

  current_line.clear();
  for (int col = 0; col < cols; col++) {
    VTermScreenCell cell {};
    VTermPos cell_pos {cursor_row, col};
    if (vterm_screen_get_cell(screen, cell_pos, &cell)) {
      current_line += cell_text(cell);
    }
  }
  while (!current_line.empty() && current_line.back() == ' ') {
    current_line.pop_back();
  }
}

bool IntegratedTerminal::open_shell(const std::string &cwd) {
#if defined(JOT_PLATFORM_WINDOWS)
  (void)cwd;
  return false;
#else
  if (active) {
    focused = true;
    return true;
  }

  termios shell_termios = build_shell_termios();
  winsize shell_ws {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &shell_ws) != 0) {
    shell_ws.ws_col = (unsigned short)cols;
    shell_ws.ws_row = (unsigned short)rows;
  }

  int fd = -1;
  pid_t pid = forkpty(&fd, nullptr, &shell_termios, &shell_ws);
  if (pid < 0) {
    return false;
  }

  if (pid == 0) {
    const char *shell = getenv("SHELL");
    setenv("TERM", "xterm-256color", 1);
    if (!cwd.empty()) {
      int rc = chdir(cwd.c_str());
      (void)rc;
    }
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
  scroll_offset = 0;
  current_line.clear();
  scrollback.clear();
  ensure_vterm(rows, cols);
  vterm_screen_reset(screen, 1);

  int flags = fcntl(master_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
  }
  return true;
#endif
}

void IntegratedTerminal::close_shell() {
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
  destroy_vterm();
}

bool IntegratedTerminal::poll_output() {
  if (!active || master_fd < 0) {
    return false;
  }

  ensure_vterm(rows, cols);

  bool changed = false;
  char buf[4096];
  int reads = 0;
  size_t bytes_read = 0;
  while (reads < kPollReadLimit && bytes_read < kPollByteLimit) {
    ssize_t n = read(master_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    changed = true;
    reads++;
    bytes_read += (size_t)n;
    vterm_input_write(vterm, buf, (size_t)n);
  }

  if (changed) {
    vterm_screen_flush_damage(screen);
    refresh_current_line();
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
      std::vector<StyledCell> row;
      const std::string exited = "[terminal exited]";
      row.reserve(exited.size());
      for (char ch : exited) {
        row.push_back({std::string(1, ch), 7, 0});
      }
      scrollback.push_back(std::move(row));
      changed = true;
    }
  }

  return changed;
}

void IntegratedTerminal::resize(int new_rows, int new_cols) {
  new_rows = std::max(1, new_rows);
  new_cols = std::max(1, new_cols);
  if (vterm && rows == new_rows && cols == new_cols) {
    return;
  }
  ensure_vterm(new_rows, new_cols);

  if (master_fd >= 0) {
    winsize ws {};
    ws.ws_row = (unsigned short)new_rows;
    ws.ws_col = (unsigned short)new_cols;
    ioctl(master_fd, TIOCSWINSZ, &ws);
  }
  refresh_current_line();
}

bool IntegratedTerminal::send_key(int ch, bool is_ctrl, bool is_shift,
                                  bool is_alt) {
  if (!active || master_fd < 0) {
    return false;
  }

  reset_scroll();
  ensure_vterm(rows, cols);

  VTermModifier mod = VTERM_MOD_NONE;
  if (is_shift)
    mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
  if (is_alt)
    mod = (VTermModifier)(mod | VTERM_MOD_ALT);
  if (is_ctrl)
    mod = (VTermModifier)(mod | VTERM_MOD_CTRL);

  bool handled = true;
  switch (ch) {
  case 1008:
    vterm_keyboard_key(vterm, VTERM_KEY_UP, mod);
    break;
  case 1009:
    vterm_keyboard_key(vterm, VTERM_KEY_DOWN, mod);
    break;
  case 1010:
    vterm_keyboard_key(vterm, VTERM_KEY_RIGHT, mod);
    break;
  case 1011:
    vterm_keyboard_key(vterm, VTERM_KEY_LEFT, mod);
    break;
  case 1012:
    vterm_keyboard_key(vterm, VTERM_KEY_HOME, mod);
    break;
  case 1013:
    vterm_keyboard_key(vterm, VTERM_KEY_END, mod);
    break;
  case 1017:
    vterm_keyboard_key(vterm, VTERM_KEY_TAB, VTERM_MOD_SHIFT);
    break;
  case 1001:
    vterm_keyboard_key(vterm, VTERM_KEY_DEL, mod);
    break;
  case 127:
  case 8:
    vterm_keyboard_key(vterm, VTERM_KEY_BACKSPACE, mod);
    break;
  case '\n':
  case '\r':
    vterm_keyboard_key(vterm, VTERM_KEY_ENTER, mod);
    break;
  case '\t':
    vterm_keyboard_key(vterm, VTERM_KEY_TAB, mod);
    break;
  default:
    handled = false;
    break;
  }

  if (!handled) {
    if (ch >= 1 && ch <= 26) {
      unsigned char ctrl = (unsigned char)ch;
      write_all(master_fd, (const char *)&ctrl, 1);
      return true;
    }
    if (is_ctrl && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
      unsigned char ctrl = (unsigned char)(std::tolower(ch) - 'a' + 1);
      write_all(master_fd, (const char *)&ctrl, 1);
      return true;
    }
    if (ch >= 32) {
      if (is_alt) {
        unsigned char esc = 27;
        write_all(master_fd, (const char *)&esc, 1);
      }
      vterm_keyboard_unichar(vterm, (uint32_t)ch, VTERM_MOD_NONE);
    } else {
      return false;
    }
  }

  write_output_buffer();
  return true;
}

bool IntegratedTerminal::send_text(const std::string &text) {
  if (!active || master_fd < 0) {
    return false;
  }
  reset_scroll();
  write_all(master_fd, text.data(), text.size());
  return true;
}

bool IntegratedTerminal::scroll_lines(int delta, int visible_rows) {
  int total = (int)scrollback.size() + rows;
  int view = std::max(1, visible_rows);
  int max_offset = std::max(0, total - view);
  int next = std::clamp(scroll_offset + delta, 0, max_offset);
  bool changed = (next != scroll_offset);
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

  std::vector<OutputRow> all;
  all.reserve(scrollback.size() + (size_t)rows);
  for (const auto &cells : scrollback) {
    all.push_back({row_text(cells), cells});
  }

  if (screen) {
    for (int row = 0; row < rows; row++) {
      OutputRow out_row;
      out_row.cells.reserve((size_t)cols);
      for (int col = 0; col < cols; col++) {
        VTermScreenCell cell {};
        VTermPos pos {row, col};
        if (vterm_screen_get_cell(screen, pos, &cell)) {
          out_row.cells.push_back(styled_from_vterm_cell(screen, cell));
        } else {
          out_row.cells.push_back({" ", 7, 0});
        }
      }
      out_row.text = row_text(out_row.cells);
      all.push_back(std::move(out_row));
    }
  } else if (!current_line.empty()) {
    all.push_back({current_line, {}});
  }

  int total = (int)all.size();
  int take = std::min(max_lines, total);
  int max_offset = std::max(0, total - take);
  int offset = std::clamp(scroll_offset, 0, max_offset);
  int end_exclusive = std::clamp(total - offset, 0, total);
  int start = std::max(0, end_exclusive - take);
  out.reserve((size_t)std::max(0, end_exclusive - start));
  for (int i = start; i < end_exclusive; i++) {
    out.push_back(std::move(all[(size_t)i]));
  }
  return out;
}

std::vector<std::string> IntegratedTerminal::get_recent_lines(
    int max_lines) const {
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
