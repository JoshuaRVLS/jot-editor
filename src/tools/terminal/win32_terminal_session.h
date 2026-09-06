#ifndef WIN32_TERMINAL_SESSION_H
#define WIN32_TERMINAL_SESSION_H

#include <cstddef>
#include <string>

#include "tools/terminal/terminal_session.h"

// ConPTY-backed terminal session for Windows. Modeled on Neovim's
// pty_conpty_win.c: the pseudo console and process APIs are loaded
// dynamically so older Windows (pre-1809) degrade gracefully, input/output
// travel over anonymous pipes, and the session is polled from the editor's
// timer loop because Windows handles cannot feed the POSIX uv_poll path.
class Win32TerminalSession final : public TerminalSession
{
public:
  Win32TerminalSession();
  ~Win32TerminalSession() override;

  Win32TerminalSession(const Win32TerminalSession &) = delete;
  Win32TerminalSession &operator=(const Win32TerminalSession &) = delete;

  bool open(const std::string &cwd, int rows, int cols) override;
  void close() override;
  void close_after_exit() override;
  bool read_available(std::string &out, size_t max_bytes) override;
  size_t write(const char *data, size_t size) override;
  bool resize(int rows, int cols) override;
  bool process_exited() const override;
  bool active() const override;

  // Windows cannot expose a pollable file descriptor; returns -1 so callers
  // fall back to timer-driven polling.
  int input_fd() const override
  {
    return -1;
  }

  int process_id() const;

private:
  void close_handles();
  void close_pseudo_console();

  void *pseudo_console_ = nullptr;
  void *process_handle_ = nullptr;
  void *input_write_ = nullptr;
  void *output_read_ = nullptr;
  int process_id_ = -1;
};

#endif
